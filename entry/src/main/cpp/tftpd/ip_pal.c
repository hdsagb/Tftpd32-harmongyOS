//////////////////////////////////////////////////////////
// ip_pal.c —— GetIPv4Address 等由 _services/ip_util.c 提供
// （依赖 Windows IPHlpApi，不移植；里程碑1 用桩，里程碑3 经 getifaddrs 实现）
// 另提供 UdpSend（_libs/mini_tcp4u/udp4u.c 的 POSIX 版，同签名）
//        ScanDir（_libs/scandir 的 opendir 版，同签名）
//////////////////////////////////////////////////////////
#include "headers.h"
#include <dirent.h>

int GetIPv4Address (const char *szIf, char *szIP) { szIP [0] = 0;  return -1; }
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
