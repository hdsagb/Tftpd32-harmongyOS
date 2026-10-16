//////////////////////////////////////////////////////////
// console_pal.c —— 替代 to_console.c（GUI 消息层 → NAPI 事件）
// 同签名 SendMsgRequest / ReportNewTrf / LogToMonitor
//////////////////////////////////////////////////////////
#include "headers.h"

typedef void (*PAL_EMITTER) (const char *szType, const char *szJson);
static PAL_EMITTER s_pEmitter = NULL;

void Pal_SetEmitter (PAL_EMITTER fn) { s_pEmitter = fn; }

static void Emit (const char *szType, const char *szJson)
{ if (s_pEmitter != NULL)  s_pEmitter (szType, szJson); }

/* threading.h 同名声明，tftpd_main.c / tftpd_thread.c 原样调用 */
int SendMsgRequest (int type, const void *msg_stuff, int size, BOOL bBlocking, BOOL bRetain)
{
char szBuf [1024];
    switch (type)
    {
        case C_TFTP_TRF_STAT :
        {
            const struct S_TftpTrfStat *p = (const struct S_TftpTrfStat *) msg_stuff;
            int Ark, n = p->nbTrf;  char one [160];
            strcpy (szBuf, "{\"nbTrf\":");
            sprintf (szBuf + strlen (szBuf), "%d,\"t\":[", n);
            for (Ark = 0 ; Ark < n && Ark < 100 ; Ark ++)
            {
                const struct subStats *s = & p->t [Ark];
                sprintf (one, "{\"id\":%u,\"bytes\":%lld,\"size\":%lld,\"ret\":%u,\"dur\":%d}%s",
                         s->dwTransferId, (__int64) s->stat.qwTotalBytes,
                         (__int64) s->stat.qwTransferSize, s->stat.ret_code,
                         (int)(time (NULL) - s->stat.StartTime),
                         Ark + 1 < n ? "," : "");
                strcat (szBuf, one);
            }
            strcat (szBuf, "]}");
            Emit ("stat", szBuf);
            break;
        }
        case C_CHG_SERVICE :
        {
            const struct S_Chg_Service *p = (const struct S_Chg_Service *) msg_stuff;
            sprintf (szBuf, "{\"service\":%d,\"status\":%d}", p->service, p->status);
            Emit ("service", szBuf);
            break;
        }
        case C_SERVICES_STARTED : Emit ("started", "{}");  break;
        default : break;   /* 其余消息类型里程碑 2/3 再接 */
    }
return TRUE;
}

int ReportNewTrf (const struct LL_TftpInfo *pTftp)
{
char szBuf [256];
    sprintf (szBuf, "{\"id\":%u,\"bytes\":%lld,\"size\":%lld,\"ret\":%u}",
             pTftp->tm.dwTransferId,
             (__int64) pTftp->st.qwTotalBytes,
             (__int64) pTftp->st.qwTransferSize,
             pTftp->st.ret_code);
    Emit ("trf", szBuf);
return TRUE;
}

/* _libs/log/LogToMonitor.c 的替代实现（原文件不移植） */
void LogToMonitor (char *sz, ...)
{
char buf [LOGSIZE];
va_list ap;
    va_start (ap, sz);
    vsnprintf (buf, sizeof buf, sz, ap);
    va_end (ap);
    Emit ("log", buf);
}
