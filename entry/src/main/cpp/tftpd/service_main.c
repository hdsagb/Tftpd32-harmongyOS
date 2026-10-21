//////////////////////////////////////////////////////////
// service_main.c —— 表驱动的多服务管理器
// 替代原 start_threads.c 的 TFTP 部分：TFTP / Syslog / SNTP / DNS
// 每个服务独立 UDP socket + 独立 pthread，端口取自 sSettings
// tThreads[] 表与 TH_* 枚举沿用 threading.h（-fcommon 保证符号合一）
//////////////////////////////////////////////////////////
#include "headers.h"
#include <pthread.h>

extern void TftpdMain (void *param);
extern void SyslogProc (void *param);
extern void SntpdProc (void *param);
extern void ListenDNSMessage (void *param);

struct S_PalService
{
    int             nThread;
    unsigned        uMask;
    void          (*pfnProc) (void *);
    unsigned       *pPort;
    const char     *szName;
};

static struct S_PalService sServices [] =
{
    { TH_TFTP,   SERVICE_TFTP,   TftpdMain,         & sSettings.Port,       "TFTP"   },
    { TH_SYSLOG, SERVICE_SYSLOG, SyslogProc,        & sSettings.SyslogPort, "Syslog" },
    { TH_SNTP,   SERVICE_SNTP,   SntpdProc,         & sSettings.SntpPort,   "SNTP"   },
    { TH_DNS,    SERVICE_DNS,    ListenDNSMessage,  & sSettings.DnsPort,    "DNS"    },
};

/* 每个表项的线程是否真的创建过（pthread_t 不宜直接与 0 比较） */
static int s_bSvcThreadStarted [sizeof sServices / sizeof sServices [0]];

static void *PalSvcTramp (void *param)
{
    struct S_PalService *p = (struct S_PalService *) param;
    p->pfnProc (NULL);
    return NULL;
}

static void PalSvcNotify (struct S_PalService *p, int status)
{
    struct S_Chg_Service chg;
    chg.service = (int) p->uMask;
    chg.status  = status;
    SendMsgRequest (C_CHG_SERVICE, & chg, sizeof chg, FALSE, FALSE);
}

static int PalSvcStartOne (int i)
{
    struct S_PalService *p = & sServices [i];
    struct S_ThreadMonitoring *pT = & tThreads [p->nThread];
    struct sockaddr_in tAddr;
    int True = 1;

    if (pT->gRunning)  return TRUE;

    pT->bSoftReset = FALSE;
    pT->bInit      = FALSE;
    pT->skt        = INVALID_SOCKET;
    pT->hEv        = INVALID_HANDLE_VALUE;
    pT->hThread    = (pthread_t) 0;
    s_bSvcThreadStarted [i] = FALSE;

    /* 仅 TFTP 的 poll 循环需要唤醒管道；其它服务靠 shutdown 唤醒 recvfrom */
    if (p->nThread == TH_TFTP)
    {
        pT->hEv = PalCreatePipeEvent ();
        if (pT->hEv == INVALID_HANDLE_VALUE)
        {
            LogToMonitor ("%s: PalCreatePipeEvent failed", p->szName);
            return FALSE;
        }
    }

    pT->skt = socket (AF_INET, SOCK_DGRAM, 0);
    if (pT->skt == INVALID_SOCKET)
    {
        LogToMonitor ("%s: socket() failed (errno %d)", p->szName, errno);
        goto error;
    }

    if (setsockopt (pT->skt, SOL_SOCKET, SO_REUSEADDR, (char *) & True, sizeof True) != 0)
        LogToMonitor ("%s: setsockopt SO_REUSEADDR failed (errno %d)", p->szName, errno);

    memset (& tAddr, 0, sizeof tAddr);
    tAddr.sin_family      = AF_INET;
    tAddr.sin_addr.s_addr = htonl (INADDR_ANY);
    tAddr.sin_port        = htons ((unsigned short) * p->pPort);

    if (bind (pT->skt, (struct sockaddr *) & tAddr, sizeof tAddr) != 0)
    {
        LogToMonitor ("%s: bind port %u failed (errno %d)", p->szName, * p->pPort, errno);
        goto error;
    }

    pT->gRunning = TRUE;
    if (pthread_create (& pT->hThread, NULL, PalSvcTramp, p) != 0)
    {
        LogToMonitor ("%s: pthread_create failed", p->szName);
        pT->gRunning = FALSE;
        goto error;
    }
    s_bSvcThreadStarted [i] = TRUE;

    LogToMonitor ("%s service started on port %u", p->szName, * p->pPort);
    PalSvcNotify (p, 1 /*RUNNING*/);
    return TRUE;

error:
    if (pT->skt != INVALID_SOCKET)
    { closesocket (pT->skt);  pT->skt = INVALID_SOCKET; }
    if (pT->hEv != INVALID_HANDLE_VALUE)
    { CloseHandle (pT->hEv);  pT->hEv = INVALID_HANDLE_VALUE; }
    return FALSE;
}

static void PalSvcStopOne (int i)
{
    struct S_PalService *p = & sServices [i];
    struct S_ThreadMonitoring *pT = & tThreads [p->nThread];

    if (! pT->gRunning)  return;

    pT->gRunning = FALSE;                   /* 服务循环退出条件 */
    if (p->nThread == TH_TFTP)
        pT->bSoftReset = TRUE;              /* 不向 worker 发 nak，直接回收 */

    if (pT->skt != INVALID_SOCKET)  shutdown (pT->skt, SHUT_RDWR);  /* 唤醒 recvfrom */
    if (p->nThread == TH_TFTP && pT->hEv != INVALID_HANDLE_VALUE)
        SetEvent (pT->hEv);                 /* 唤醒 poll */

    if (s_bSvcThreadStarted [i])
    {
        pthread_join (pT->hThread, NULL);
        s_bSvcThreadStarted [i] = FALSE;
    }
    pT->hThread = (pthread_t) 0;

    if (pT->hEv != INVALID_HANDLE_VALUE)
    { CloseHandle (pT->hEv);  pT->hEv = INVALID_HANDLE_VALUE; }

    /* TftpdMain 退出时已自行 closesocket 并把 skt 置 INVALID_SOCKET，
       此处只在 socket 仍有效（如线程未启动）时才关闭，避免误关复用的 fd */
    if (pT->skt != INVALID_SOCKET)
    { closesocket (pT->skt);  pT->skt = INVALID_SOCKET; }

    pT->bInit = FALSE;
    LogToMonitor ("%s service stopped", p->szName);
    PalSvcNotify (p, 0 /*STOPPED*/);
}

static void PalSvcSyncRunning (void)
{
    unsigned uMask = 0;
    int i;

    for (i = 0 ; i < (int) (sizeof sServices / sizeof sServices [0]) ; i ++)
        if (tThreads [sServices [i].nThread].gRunning)
            uMask |= sServices [i].uMask;
    sSettings.uRunningServices = uMask;
}

int PalServiceStartMask (unsigned uMask)
{
    int i, nStarted = 0;

    for (i = 0 ; i < (int) (sizeof sServices / sizeof sServices [0]) ; i ++)
        if ((uMask & sServices [i].uMask) && PalSvcStartOne (i))
            nStarted ++;

    PalSvcSyncRunning ();
    return nStarted;
}

void PalServiceStopMask (unsigned uMask)
{
    int i;

    for (i = 0 ; i < (int) (sizeof sServices / sizeof sServices [0]) ; i ++)
        if (uMask & sServices [i].uMask)
            PalSvcStopOne (i);

    PalSvcSyncRunning ();
}

/* ---------- 兼容入口（napi_init.cpp / pal_compat.h 签名不可变） ---------- */
int TftpdServiceStart (void)
{
    unsigned uSupported = SERVICE_TFTP | SERVICE_SYSLOG | SERVICE_SNTP | SERVICE_DNS;
    return PalServiceStartMask (sSettings.uServices & uSupported) > 0;
}

void TftpdServiceStop (void)
{
    PalServiceStopMask (SERVICE_TFTP | SERVICE_SYSLOG | SERVICE_SNTP | SERVICE_DNS);
}

/* ==================================================================
 * 临时链接桩：syslogd.c / sntpd.c / dns.c 尚未移植（TASK4-6 将删除本块，
 * 届时由真实实现覆盖）。weak 保证后续加入强定义时不冲突。
 * ================================================================== */
__attribute__((weak)) void SyslogProc (void *param)        { (void) param; }
__attribute__((weak)) void SntpdProc  (void *param)        { (void) param; }
__attribute__((weak)) void ListenDNSMessage (void *param)  { (void) param; }
