/* Derived from Tftpd32/Tftpd64 (Copyright (C) Philippe Jounin, EUPL-1.2),
 * modified for HarmonyOS. This file is distributed under GPLv2 as part
 * of the Tftpd32 for HarmonyOS project. */
//////////////////////////////////////////////////////////
// tftp_client.c —— TFTP 客户端（移植自 _gui/tftp_cli.c 的协议语义）
// 原版基于 WSAAsyncSelect + 窗口定时器；此处改写为阻塞 poll() 状态机，
// 选项协商、块号回绕、超时重传（Timeout / Retransmit）语义与原版一致。
// 一次传输一个工作线程，进度经 PalEmitEvent("client", JSON) 上报。
//////////////////////////////////////////////////////////
#include "headers.h"
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>

struct S_PalCli
{
    int   nOpcode;                  /* TFTP_RRQ / TFTP_WRQ */
    char  szHost [256];
    int   nPort;
    char  szRemote [256];
    char  szLocal  [_MAX_PATH];
    int   nBlkSize;
};

static volatile int s_bClientBusy = FALSE;

static void PalCliEmit (const char *szState, __int64 qwBytes, __int64 qwSize,
                        unsigned uBlk, int nRc, const char *szMsg, const char *szMd5)
{
char szBuf [768];
char szMsg2 [256];
char szMd52 [40];

    PalJsonEscape (szMsg2,  sizeof szMsg2,  szMsg == NULL ? "" : szMsg);
    PalJsonEscape (szMd52,  sizeof szMd52,  szMd5 == NULL ? "" : szMd5);
    snprintf (szBuf, sizeof szBuf,
              "{\"state\":\"%s\",\"bytes\":%lld,\"size\":%lld,\"blk\":%u,\"rc\":%d,"
              "\"msg\":\"%s\",\"md5\":\"%s\"}",
              szState, (long long) qwBytes, (long long) qwSize,
              uBlk, nRc, szMsg2, szMd52);
    PalEmitEvent ("client", szBuf);
}

static int PalCliResolve (const char *szHost, int nPort,
                          struct sockaddr_storage *pSrv, int *pnLen)
{
struct addrinfo hints, *res = NULL;
char szServ [16];
int Rc;

    memset (& hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf (szServ, sizeof szServ, "%d", nPort);

    Rc = getaddrinfo (szHost, szServ, & hints, & res);
    if (Rc != 0 || res == NULL)  return FALSE;

    if (res->ai_addrlen > sizeof * pSrv)
    { freeaddrinfo (res);  return FALSE; }

    memset (pSrv, 0, sizeof * pSrv);
    memcpy (pSrv, res->ai_addr, res->ai_addrlen);
    * pnLen = (int) res->ai_addrlen;
    freeaddrinfo (res);
    return TRUE;
}

static int PalCliSamePeer (const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family)  return FALSE;
    if (a->ss_family == AF_INET)
    {
        const struct sockaddr_in *x = (const struct sockaddr_in *) a;
        const struct sockaddr_in *y = (const struct sockaddr_in *) b;
        return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    else
    {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *) a;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *) b;
        return x->sin6_port == y->sin6_port
            && memcmp (& x->sin6_addr, & y->sin6_addr, sizeof x->sin6_addr) == 0;
    }
}

static void PalCliMd5Hex (MD5_CTX *pCtx, char *szOut)
{
unsigned char id [16];
int Ark;

    MD5Final (id, pCtx);
    for (Ark = 0; Ark < 16; Ark++)
        snprintf (szOut + 2 * Ark, 3, "%02x", id[Ark]);
    szOut[32] = 0;
}

static int PalCliBuildRequest (struct S_PalCli *p, char *pBuf, __int64 qwSize)
{
struct tftphdr *tp = (struct tftphdr *) pBuf;
char *q = tp->th_stuff;

    tp->th_opcode = htons ((unsigned short) p->nOpcode);
    strcpy (q, p->szRemote);  q += strlen (q) + 1;
    strcpy (q, "octet");      q += sizeof "octet";

    if (p->nBlkSize >= TFTP_MINSEGSIZE)
    {
        strcpy (q, TFTP_OPT_BLKSIZE);  q += sizeof TFTP_OPT_BLKSIZE;
        q += sprintf (q, "%d", p->nBlkSize) + 1;
    }
    strcpy (q, TFTP_OPT_TSIZE);  q += sizeof TFTP_OPT_TSIZE;
    q += sprintf (q, "%lld", (long long) qwSize) + 1;

    return (int)(q - pBuf);
}

/* OACK 选项串不保证以 0 结尾，全部以 pEnd 为界解析 */
static void PalCliParseOack (const char *pBuf, int nLen, int *pnBlk, __int64 *pqwSize)
{
const char *p = pBuf + TFTP_DATA_HEADERSIZE;
const char *pEnd = pBuf + nLen;

    while (p < pEnd && *p != 0)
    {
        const char *pOpt = p;
        const char *pVal;
        size_t n;

        n = strnlen (p, (size_t)(pEnd - p));
        if (p + n >= pEnd)  break;
        pVal = p + n + 1;
        if (pVal >= pEnd)  break;

        if (lstrcmpi ((char *) pOpt, TFTP_OPT_BLKSIZE) == 0)
        {
            int nBlk = atoi (pVal);
            if (nBlk >= TFTP_MINSEGSIZE && nBlk <= TFTP_MAXSEGSIZE)  * pnBlk = nBlk;
        }
        if (lstrcmpi ((char *) pOpt, TFTP_OPT_TSIZE) == 0)
            * pqwSize = _atoi64 (pVal);

        n = strnlen (pVal, (size_t)(pEnd - pVal));
        p = pVal + n;
        if (p < pEnd)  p++;                 /* skip NUL */
    }
}

static int PalCliGet (struct S_PalCli *p)
{
SOCKET s = INVALID_SOCKET;
struct sockaddr_storage srv, from;
struct pollfd pfd;
char  szLast [MAXPKTSIZE];
char *pBuf = NULL;
int   nPkt, nLast = 0, nRc, nAddrLen = 0, nTimeOut = 0;
int   nCount = 0, iRc = -1, bPeer = FALSE;
__int64 qwTotal = 0, qwSize = 0;
HANDLE hFile = INVALID_HANDLE_VALUE;
MD5_CTX ctx;
char  szMd5 [40] = "";
time_t tLast = 0;

    nPkt = (p->nBlkSize >= TFTP_MINSEGSIZE) ? p->nBlkSize : TFTP_SEGSIZE;

    if (! PalCliResolve (p->szHost, p->nPort, & srv, & nAddrLen))
    { PalCliEmit ("error", 0, 0, 0, -1, "主机名解析失败（网络不可达或 DNS 不可用）", "");  return -1; }

    s = socket (srv.ss_family, SOCK_DGRAM, 0);
    pBuf = (char *) malloc (MAXPKTSIZE);
    if (s == INVALID_SOCKET || pBuf == NULL)
    { PalCliEmit ("error", 0, 0, 0, -1, "创建套接字或分配内存失败", "");  iRc = -1;  goto Exit; }

    hFile = CreateFile (p->szLocal, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    { PalCliEmit ("error", 0, 0, 0, -1, "无法创建本地文件（请检查存储权限或磁盘空间）", "");  iRc = -1;  goto Exit; }

    MD5Init (& ctx);

    nLast = PalCliBuildRequest (p, szLast, 0);
    sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);

    pfd.fd = s;
    pfd.events = POLLIN;

    for (;;)
    {
        struct tftphdr *tp;
        unsigned short uOp, uBlk;
        int nWait;

        nWait = (int) sSettings.Timeout * 1000;
        nRc = poll (& pfd, 1, nWait);

        if (nRc <= 0)                                   /* 超时：重发最后一个已发包 */
        {
            if (++ nTimeOut > (int) sSettings.Retransmit)
            { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "传输超时（对端无响应）", "");  iRc = -1;  break; }
            if (nLast > 0)
                sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);
            continue;
        }

        {
            socklen_t nFromLen = sizeof from;
            nRc = (int) recvfrom (s, pBuf, MAXPKTSIZE, 0,
                                  (struct sockaddr *) & from, & nFromLen);
        }
        if (nRc < TFTP_ACK_HEADERSIZE)  continue;

        /* 服务器 TID 在首个响应中确定（RRQ 发给 69，响应来自临时端口） */
        if (! bPeer)
        { srv = from;  bPeer = TRUE; }
        else if (! PalCliSamePeer (& from, & srv))  continue;

        tp = (struct tftphdr *) pBuf;
        uOp = ntohs ((unsigned short) tp->th_opcode);
        uBlk = ntohs ((unsigned short) tp->th_block);
        nTimeOut = 0;

        if (uOp == TFTP_ERROR)
        { PalCliEmit ("error", qwTotal, qwSize, uBlk, (int) ntohs ((unsigned short) tp->th_code),
                      "服务器返回错误", "");  iRc = -1;  break; }

        if (uOp == TFTP_OACK && nCount == 0)
        {
            struct tftphdr *tps = (struct tftphdr *) szLast;

            PalCliParseOack (pBuf, nRc, & nPkt, & qwSize);
            tps->th_opcode = htons (TFTP_ACK);
            tps->th_block  = htons (0);
            nLast = TFTP_ACK_HEADERSIZE;
            sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);
            continue;
        }

        if (uOp != TFTP_DATA)  continue;

        if (uBlk == (unsigned short)(nCount + 1))
        {
            int   nData = nRc - TFTP_DATA_HEADERSIZE;
            DWORD nWrt  = 0;
            int   bFinal;
            struct tftphdr *tps = (struct tftphdr *) szLast;

            if (! WriteFile (hFile, tp->th_data, (DWORD) nData, & nWrt, NULL) || (int) nWrt != nData)
            { PalCliEmit ("error", qwTotal, qwSize, uBlk, -1, "写入本地文件失败", "");  iRc = -1;  break; }

            MD5Update (& ctx, (unsigned char *) tp->th_data, (unsigned) nData);
            qwTotal += nData;
            nCount ++;

            tps->th_opcode = htons (TFTP_ACK);
            tps->th_block  = htons ((unsigned short) nCount);
            nLast = TFTP_ACK_HEADERSIZE;

            bFinal = (nData < nPkt);
            if (bFinal)  PalCliMd5Hex (& ctx, szMd5);

            sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);

            if (time (NULL) != tLast)
            { tLast = time (NULL);  PalCliEmit ("progress", qwTotal, qwSize, nCount, 0, "", ""); }

            if (bFinal)                             /* 末块：ACK 已发，收尾 */
            {
                PalCliEmit ("done", qwTotal, qwTotal, nCount, 0, "", szMd5);
                iRc = 0;
                break;
            }
        }
        else if (nCount > 0 && uBlk == (unsigned short) nCount)
            sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);  /* 重复块：重发 ACK */
    }

Exit:
    if (hFile != INVALID_HANDLE_VALUE)  CloseHandle (hFile);
    if (s != INVALID_SOCKET)            closesocket (s);
    if (pBuf != NULL)                   free (pBuf);
    return iRc;
}

static int PalCliPut (struct S_PalCli *p)
{
SOCKET s = INVALID_SOCKET;
struct sockaddr_storage srv, from;
struct pollfd pfd;
char  szLast [MAXPKTSIZE];
char *pBuf = NULL;
int   nPkt, nLast = 0, nRc, nAddrLen = 0, nTimeOut = 0;
int   nCount = 0, iRc = -1, bPeer = FALSE;
int   bFinalSent = 0, nFinalRetry = 0;
__int64 qwTotal = 0, qwSize = 0;
HANDLE hFile = INVALID_HANDLE_VALUE;
MD5_CTX ctx;
char  szMd5 [40] = "";
struct stat st;
time_t tLast = 0;

    if (stat (p->szLocal, & st) != 0)
    { PalCliEmit ("error", 0, 0, 0, -1, "本地文件不存在", "");  return -1; }
    qwSize = (__int64) st.st_size;

    nPkt = (p->nBlkSize >= TFTP_MINSEGSIZE) ? p->nBlkSize : TFTP_SEGSIZE;

    if (! PalCliResolve (p->szHost, p->nPort, & srv, & nAddrLen))
    { PalCliEmit ("error", 0, 0, 0, -1, "主机名解析失败（网络不可达或 DNS 不可用）", "");  return -1; }

    s = socket (srv.ss_family, SOCK_DGRAM, 0);
    pBuf = (char *) malloc (MAXPKTSIZE);
    if (s == INVALID_SOCKET || pBuf == NULL)
    { PalCliEmit ("error", 0, 0, 0, -1, "创建套接字或分配内存失败", "");  iRc = -1;  goto Exit; }

    hFile = CreateFile (p->szLocal, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    { PalCliEmit ("error", 0, 0, qwSize, -1, "无法打开本地文件", "");  iRc = -1;  goto Exit; }

    MD5Init (& ctx);
    nLast = PalCliBuildRequest (p, szLast, qwSize);
    sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);

    pfd.fd = s;
    pfd.events = POLLIN;

    for (;;)
    {
        struct tftphdr *tp;
        unsigned short uOp, uBlk;

        nRc = poll (& pfd, 1, (int) sSettings.Timeout * 1000);
        if (nRc <= 0)
        {
            if (bFinalSent)                          /* 末块已发：重发直到收到最后 ACK */
            {
                if (++ nFinalRetry > (int) sSettings.Retransmit)
                { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "等待最后一帧确认超时", "");  iRc = -1;  break; }
                sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);
                continue;
            }
            if (++ nTimeOut > (int) sSettings.Retransmit)
            { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "传输超时（对端无响应）", "");  iRc = -1;  break; }
            sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);
            continue;
        }

        {
            socklen_t nFromLen = sizeof from;
            nRc = (int) recvfrom (s, pBuf, MAXPKTSIZE, 0,
                                  (struct sockaddr *) & from, & nFromLen);
        }
        if (nRc < TFTP_ACK_HEADERSIZE)  continue;

        /* 服务器 TID 在首个响应中确定（WRQ 发给 69，响应来自临时端口） */
        if (! bPeer)
        { srv = from;  bPeer = TRUE; }
        else if (! PalCliSamePeer (& from, & srv))  continue;

        tp = (struct tftphdr *) pBuf;
        uOp = ntohs ((unsigned short) tp->th_opcode);
        uBlk = ntohs ((unsigned short) tp->th_block);
        nTimeOut = 0;

        if (uOp == TFTP_ERROR)
        { PalCliEmit ("error", qwTotal, qwSize, uBlk,
                      (int) ntohs ((unsigned short) tp->th_code), "服务器返回错误", "");  iRc = -1;  break; }

        if (bFinalSent)                              /* 等最后 ACK：只处理 ACK==nCount */
        {
            if (uOp == TFTP_ACK && uBlk == (unsigned short) nCount)
            {
                PalCliMd5Hex (& ctx, szMd5);
                PalCliEmit ("done", qwTotal, qwSize, nCount, 0, "", szMd5);
                iRc = 0;
                break;
            }
            if (uOp == TFTP_ACK && nCount > 0 && uBlk == (unsigned short)(nCount - 1))
                sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);
            continue;
        }

        if (uOp == TFTP_OACK)
        {
            if (nCount != 0)  continue;              /* 已开始传数据，忽略迟到的 OACK */
            PalCliParseOack (pBuf, nRc, & nPkt, & qwSize);
            goto SendBlock;                          /* OACK 后直接发首块 */
        }

        if (uOp != TFTP_ACK)  continue;

        if (uBlk == (unsigned short) nCount)         /* ACK n（含无 OACK 时的 ACK 0）→ 发下一块 */
            goto SendBlock;

        if (nCount > 0 && uBlk == (unsigned short)(nCount - 1))
            sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);  /* 重复 ACK：重发 */
        continue;

SendBlock:
        {
            DWORD nWrt = 0;
            int   nRead;
            struct tftphdr *tps = (struct tftphdr *) szLast;

            nTimeOut = 0;
            if (! ReadFile (hFile, szLast + TFTP_DATA_HEADERSIZE, (DWORD) nPkt, & nWrt, NULL))
            { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "读取本地文件失败", "");  iRc = -1;  break; }
            nRead = (int) nWrt;

            nCount ++;
            tps->th_opcode = htons (TFTP_DATA);
            tps->th_block  = htons ((unsigned short) nCount);
            nLast = TFTP_DATA_HEADERSIZE + nRead;

            MD5Update (& ctx, (unsigned char *) szLast + TFTP_DATA_HEADERSIZE, (unsigned) nRead);
            qwTotal += nRead;

            sendto (s, szLast, nLast, 0, (struct sockaddr *) & srv, nAddrLen);

            if (time (NULL) != tLast)
            { tLast = time (NULL);  PalCliEmit ("progress", qwTotal, qwSize, nCount, 0, "", ""); }

            if (nRead < nPkt)                        /* 末块（含空文件 nRead==0）已发 */
            { bFinalSent = TRUE;  nFinalRetry = 0; }
        }
    }

Exit:
    if (hFile != INVALID_HANDLE_VALUE)  CloseHandle (hFile);
    if (s != INVALID_SOCKET)            closesocket (s);
    if (pBuf != NULL)                   free (pBuf);
    return iRc;
}

static void *PalClientThread (void *pArg)
{
struct S_PalCli *p = (struct S_PalCli *) pArg;

    PalCliEmit ("start", 0, 0, 0, 0, "", "");
    if (p->nOpcode == TFTP_RRQ)  PalCliGet (p);
    else                         PalCliPut (p);
    free (p);
    s_bClientBusy = FALSE;
    return NULL;
}

int PalClientStart (int nOpcode, const char *szHost, int nPort,
                    const char *szRemote, const char *szLocal, int nBlkSize)
{
struct S_PalCli *p;
pthread_t th;

    if (s_bClientBusy)
    { PalCliEmit ("error", 0, 0, 0, -1, "客户端忙（已有传输在进行）", "");  return FALSE; }

    if (nBlkSize != 0 && (nBlkSize < TFTP_MINSEGSIZE || nBlkSize > TFTP_MAXSEGSIZE))
        nBlkSize = 0;                                     /* 0 = 默认 512 */

    p = (struct S_PalCli *) calloc (1, sizeof * p);
    if (p == NULL)  return FALSE;

    p->nOpcode = nOpcode;
    snprintf (p->szHost,   sizeof p->szHost,   "%s", szHost   != NULL ? szHost   : "");
    snprintf (p->szRemote, sizeof p->szRemote, "%s", szRemote != NULL ? szRemote : "");
    snprintf (p->szLocal,  sizeof p->szLocal,  "%s", szLocal  != NULL ? szLocal  : "");
    p->nPort    = nPort > 0 ? nPort : 69;
    p->nBlkSize = nBlkSize;

    s_bClientBusy = TRUE;
    if (pthread_create (& th, NULL, PalClientThread, p) != 0)
    { s_bClientBusy = FALSE;  free (p);  return FALSE; }
    pthread_detach (th);
    return TRUE;
}
