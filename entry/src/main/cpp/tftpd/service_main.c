//////////////////////////////////////////////////////////
// service_main.c —— 替代 start_threads.c 的 TFTP 部分
// tThreads[] 表与 TH_TFTP 沿用 threading.h（-fcommon 保证符号合一）
//////////////////////////////////////////////////////////
#include "headers.h"

static pthread_t s_hTftpThread;
static int       s_bThreadStarted = FALSE;

static void *TftpThreadTramp (void *param) { TftpdMain (param);  return NULL; }

int TftpdServiceStart (void)
{
    if (tThreads [TH_TFTP].gRunning)  return TRUE;

    tThreads [TH_TFTP].bSoftReset = FALSE;
    tThreads [TH_TFTP].bInit      = FALSE;
    tThreads [TH_TFTP].skt        = INVALID_SOCKET;
    tThreads [TH_TFTP].hEv        = PalCreatePipeEvent ();   /* poll 唤醒管道 */
    if (tThreads [TH_TFTP].hEv == INVALID_HANDLE_VALUE)  return FALSE;

    tThreads [TH_TFTP].gRunning = TRUE;
    if (pthread_create (& s_hTftpThread, NULL, TftpThreadTramp, NULL) != 0)
    {
        tThreads [TH_TFTP].gRunning = FALSE;
        CloseHandle (tThreads [TH_TFTP].hEv);
        tThreads [TH_TFTP].hEv = INVALID_HANDLE_VALUE;
        return FALSE;
    }
    s_bThreadStarted = TRUE;
    SendMsgRequest (C_CHG_SERVICE,
                    & (struct S_Chg_Service) { TFTPD32_TFTP_SERVER, 1 /*RUNNING*/ },
                    sizeof (struct S_Chg_Service), FALSE, FALSE);
return TRUE;
}

void TftpdServiceStop (void)
{
    if (! tThreads [TH_TFTP].gRunning)  return;
    tThreads [TH_TFTP].gRunning = FALSE;    /* TftpdMain 循环退出条件 */
    tThreads [TH_TFTP].bSoftReset = TRUE;   /* 不向 worker 发 nak，直接回收 */
    if (s_bThreadStarted) { pthread_join (s_hTftpThread, NULL);  s_bThreadStarted = FALSE; }
    if (tThreads [TH_TFTP].hEv != INVALID_HANDLE_VALUE)
    { CloseHandle (tThreads [TH_TFTP].hEv);  tThreads [TH_TFTP].hEv = INVALID_HANDLE_VALUE; }
    if (tThreads [TH_TFTP].skt != INVALID_SOCKET)
    { closesocket (tThreads [TH_TFTP].skt);  tThreads [TH_TFTP].skt = INVALID_SOCKET; }
    SendMsgRequest (C_CHG_SERVICE,
                    & (struct S_Chg_Service) { TFTPD32_TFTP_SERVER, 0 /*STOPPED*/ },
                    sizeof (struct S_Chg_Service), FALSE, FALSE);
}
