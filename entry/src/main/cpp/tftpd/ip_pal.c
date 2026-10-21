//////////////////////////////////////////////////////////
// ip_pal.c —— _services/ip_util.c 的 POSIX 移植（getifaddrs 版）
//   GetIPv4Address / FindNearestServerAddress / AddrInc / haddrtoa / timetoa 等
// 另提供 UdpSend（_libs/mini_tcp4u/udp4u.c 的 POSIX 版，同签名）
//        ScanDir（_libs/scandir 的 opendir 版，同签名）
//////////////////////////////////////////////////////////
#include "headers.h"
#include <dirent.h>
#include <ifaddrs.h>
#include <net/if.h>

/* ============================================================
 * _services/ip_util.c 的 POSIX 移植（DHCP 依赖）
 * 原版用 gethostbyname 枚举本机地址；鸿蒙上用 getifaddrs 更可靠
 * ============================================================ */
BOOL PalGetPrimaryIPv4 (struct in_addr *pAddr)
{
struct ifaddrs *pList = NULL, *pIt;
BOOL bFound = FALSE;

    pAddr->s_addr = htonl (INADDR_LOOPBACK);
    if (getifaddrs (& pList) != 0)  return FALSE;
    for (pIt = pList ; pIt != NULL ; pIt = pIt->ifa_next)
    {
        if (pIt->ifa_addr == NULL || pIt->ifa_addr->sa_family != AF_INET)  continue;
        if ((pIt->ifa_flags & IFF_LOOPBACK) != 0)  continue;
        pAddr->s_addr = ((struct sockaddr_in *) pIt->ifa_addr)->sin_addr.s_addr;
        if (pAddr->s_addr != 0)
        { bFound = TRUE;  break; }
    }
    freeifaddrs (pList);
return bFound;
}

/* 在给定网段内找本机地址；bExact=FALSE 时找不到则回退第一个非回环地址 */
struct in_addr *FindNearestServerAddress (struct in_addr *pHost, struct in_addr *pMask, BOOL bExact)
{
static struct in_addr sAddr;
struct ifaddrs *pList = NULL, *pIt;
BOOL bFirst = TRUE;

    memset (& sAddr, 0, sizeof sAddr);
    if (getifaddrs (& pList) != 0)  return bExact ? NULL : & sAddr;
    for (pIt = pList ; pIt != NULL ; pIt = pIt->ifa_next)
    {
        struct in_addr tAddr;
        if (pIt->ifa_addr == NULL || pIt->ifa_addr->sa_family != AF_INET)  continue;
        if ((pIt->ifa_flags & IFF_LOOPBACK) != 0)  continue;
        tAddr = ((struct sockaddr_in *) pIt->ifa_addr)->sin_addr;
        if (bFirst)  { sAddr = tAddr;  bFirst = FALSE; }
        if (bIPSameSubnet (& tAddr, pHost, pMask))
        { sAddr = tAddr;  break; }
    }
    freeifaddrs (pList);

    if (sAddr.s_addr == 0)
    {
        static struct in_addr sLoopback = { 0x0100007F };   /* 127.0.0.1（网络序） */
        return bExact ? NULL : & sLoopback;
    }
return & sAddr;
} /* FindNearestServerAddress */

/* 取网卡地址：szIf 为空 → 首个非回环地址；否则按 IP 字面量或网卡名匹配 */
int GetIPv4Address (const char *szIf, char *szIP)
{
struct ifaddrs *pList = NULL, *pIt;
int  nRc = -1;

    szIP [0] = 0;
    if (getifaddrs (& pList) != 0)  return -1;
    for (pIt = pList ; pIt != NULL ; pIt = pIt->ifa_next)
    {
        if (pIt->ifa_addr == NULL || pIt->ifa_addr->sa_family != AF_INET)  continue;
        if ((pIt->ifa_flags & IFF_LOOPBACK) != 0)  continue;
        if (szIf != NULL && szIf [0] != 0
            && strcmp (pIt->ifa_name, szIf) != 0
            && strcmp (inet_ntoa (((struct sockaddr_in *) pIt->ifa_addr)->sin_addr), szIf) != 0)
            continue;
        strcpy (szIP, inet_ntoa (((struct sockaddr_in *) pIt->ifa_addr)->sin_addr));
        nRc = 0;
        break;
    }
    freeifaddrs (pList);
return nRc;
}

/* 枚举本机 IPv4（非回环），输出 "ip,ip,..."；返回个数。LocalIP 下拉用 */
int Pal_ListIPv4Addresses (char *szOut, int nOut)
{
struct ifaddrs *pList = NULL, *pIt;
int  n = 0, len = 0;

    if (szOut == NULL || nOut < 2)  return 0;
    szOut [0] = 0;
    if (getifaddrs (& pList) != 0)  return 0;
    for (pIt = pList ; pIt != NULL ; pIt = pIt->ifa_next)
    {
        const char *szIP;
        if (pIt->ifa_addr == NULL || pIt->ifa_addr->sa_family != AF_INET)  continue;
        if ((pIt->ifa_flags & IFF_LOOPBACK) != 0)  continue;
        szIP = inet_ntoa (((struct sockaddr_in *) pIt->ifa_addr)->sin_addr);
        if (szIP == NULL || szIP [0] == 0 || strcmp (szIP, "0.0.0.0") == 0)  continue;
        if (len + (int) strlen (szIP) + 2 >= nOut)  break;
        if (len > 0)  szOut [len++] = ',';
        strcpy (szOut + len, szIP);
        len += (int) strlen (szIP);
        n++;
    }
    freeifaddrs (pList);
return n;
}

unsigned int AddrInc (struct in_addr in)
{
   unsigned int addr = ntohl (in.s_addr);
   if ((addr & 0xff) == 254)  return addr + 3;   /* 跳过 .0/.1 */
   if ((addr & 0xff) == 255)  return addr + 2;
   return addr + 1;
}

int AddrFitsPool (struct in_addr *addr)
{
   return ntohl (addr->s_addr) >= ntohl (inet_addr (sParamDHCP.szAddr))
       && ntohl (addr->s_addr) <  ntohl (inet_addr (sParamDHCP.szAddr)) + sParamDHCP.nPoolSize;
}

BOOL bIPSameSubnet (struct in_addr *p1, struct in_addr *p2, struct in_addr *pMask)
{ return (p1->s_addr & pMask->s_addr) == (p2->s_addr & pMask->s_addr); }

#define MAXHADDRLEN 16
char * haddrtoa (const unsigned char *haddr, int hlen, char cSep)
{
static char haddrbuf [3 * MAXHADDRLEN + 1];
char *bufptr;

    if (hlen > MAXHADDRLEN)  hlen = MAXHADDRLEN;
    bufptr = haddrbuf;
    while (hlen > 0)
    {
        sprintf (bufptr, "%02X%c", (unsigned) (* haddr ++ & 0xFF), cSep);
        bufptr += 3;
        hlen --;
    }
    bufptr [-1] = 0;
return haddrbuf;
}

void atohaddr (const unsigned char *addrstr, unsigned char *haddr, int haddrlen)
{
    if (haddrlen == 6)
    {
        int b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0;
        sscanf ((const char *) addrstr, "%x:%x:%x:%x:%x:%x", & b0, & b1, & b2, & b3, & b4, & b5);
        haddr [0] = (unsigned char) b0;  haddr [1] = (unsigned char) b1;
        haddr [2] = (unsigned char) b2;  haddr [3] = (unsigned char) b3;
        haddr [4] = (unsigned char) b4;  haddr [5] = (unsigned char) b5;
    }
}

#define MAXTIMESTR  20      /* "mm/dd/yyyy/00:00:00" + 结束符 */
char * timetoa (time_t t)
{
static char timebuf [MAXTIMESTR];
struct tm *tt = localtime (& t);
    if (tt == NULL)  timebuf [0] = 0;
    else sprintf (timebuf, "%02d/%02d/%04d/%02d:%02d:%02d",
                  tt->tm_mon + 1, tt->tm_mday, tt->tm_year + 1900,
                  tt->tm_hour, tt->tm_min, tt->tm_sec);
return timebuf;
}

time_t atotime (char *str)
{
struct tm t;
    memset (& t, 0, sizeof t);
    t.tm_isdst = -1;
    sscanf (str, "%d/%d/%d/%d:%d:%d", & t.tm_mon, & t.tm_mday, & t.tm_year,
             & t.tm_hour, & t.tm_min, & t.tm_sec);
    t.tm_mon -= 1;
    t.tm_year -= 1900;
return mktime (& t);
}
int PoolNetworkInterfaces (void) { return TRUE; }   /* Scheduler 未移植，桩即可 */
int AnswerIPList (void) { return TRUE; }
void SendDirectoryContent (void) {}

/* ---------- UdpSend：等价移植 udp4u.c（仅 udpport 选项路径使用） ---------- */
#define TCP4U_ERROR      (-1)
#define TCP4U_BINDERROR  (-2)

int UdpSend (int nFromPort, struct sockaddr *sa_to, int sa_len, const char *data, int len)
{
SOCKET              s;
int                 Rc;
int                 True = 1;
char                szServ [NI_MAXSERV];
ADDRINFO            Hints, *res;

    memset (& Hints, 0, sizeof Hints);
    Hints.ai_family   = sa_to->sa_family;
    Hints.ai_flags    = AI_PASSIVE;      /* 原文件 NI_NUMERICSERV 系笔误残留，此处取被动绑定语义 */
    Hints.ai_socktype = SOCK_DGRAM;
    Hints.ai_protocol = IPPROTO_UDP;
    wsprintf (szServ, "%d", nFromPort);
    if (getaddrinfo (NULL, szServ, & Hints, & res) != 0)  return TCP4U_ERROR;

    s = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET)  { freeaddrinfo (res);  return TCP4U_ERROR; }
    Rc = setsockopt (s, SOL_SOCKET, SO_REUSEADDR, (char *) & True, sizeof True);
    LogToMonitor (Rc == 0 ? "UdpSend: Port %d may be reused" : "setsockopt error", nFromPort);

    Rc = bind (s, res->ai_addr, res->ai_addrlen);
    freeaddrinfo (res);
    LogToMonitor ("UdpSend bind returns %d (error %d)", Rc, GetLastError ());
    if (Rc < 0)  { closesocket (s);  return TCP4U_BINDERROR; }

    Rc = sendto (s, data, len, 0, sa_to, sa_len);
    LogToMonitor ("sendto returns %d", Rc);
    closesocket (s);
return Rc;
} // UdpSend

/* ---------- ScanDir：等价 scandir.c（遍历目录，回调每一项） ---------- */
static void lstrcpyn_pal_dir (char *dst, int n, const char *src)
{ strncpy (dst, src, n - 1);  dst [n - 1] = 0; }

int ScanDir (int (*Cbk) (char *, DWORD), DWORD dwParam, const char *szDir)
{
DIR *pDir;
struct dirent *pEnt;
char szLine [_MAX_PATH];

    pDir = opendir (szDir);
    if (pDir == NULL)  return -1;
    while ((pEnt = readdir (pDir)) != NULL)
    {
        if (strcmp (pEnt->d_name, ".") == 0  ||  strcmp (pEnt->d_name, "..") == 0)  continue;
        lstrcpyn_pal_dir (szLine, sizeof szLine, pEnt->d_name);
        if (Cbk (szLine, dwParam) != 0)  break;
    }
    closedir (pDir);
return 0;
}
