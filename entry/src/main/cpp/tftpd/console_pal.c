/* Derived from Tftpd32/Tftpd64 (Copyright (C) Philippe Jounin, EUPL-1.2),
 * modified for HarmonyOS. This file is distributed under GPLv2 as part
 * of the Tftpd32 for HarmonyOS project. */
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

void PalJsonEscape (char *dst, int nDst, const char *src)
{
int i, n = 0;

    if (src == NULL)  src = "";
    for (i = 0; src[i] != 0 && n < nDst - 7; i++)
    {
        unsigned char c = (unsigned char) src[i];
        if (c == '"' || c == '\\')  { dst[n++] = '\\';  dst[n++] = (char) c; }
        else if (c < 0x20)          { n += snprintf (dst + n, nDst - n, "\\u%04x", c); }
        else                        { dst[n++] = (char) c; }
    }
    dst[n] = 0;
}

void PalEmitEvent (const char *szType, const char *szJson)
{
    Emit (szType, szJson);
}

static void lstrcpyn_cpal (char *dst, int n, const char *src)
{ strncpy (dst, src, n - 1);  dst [n - 1] = 0; }

/* threading.h 同名声明，tftpd_main.c / tftpd_thread.c 原样调用 */
int SendMsgRequest (int type, const void *msg_stuff, int size, BOOL bBlocking, BOOL bRetain)
{
char szBuf [8192];
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
                /* ret = 超时/重传累计次数，对应原版 GUI 的 FD_TIMEOUT 列；
                   原先误用 ret_code（传输状态），导致运行中恒为 0。 */
                sprintf (one, "{\"id\":%u,\"bytes\":%lld,\"size\":%lld,\"ret\":%u,\"dur\":%d}%s",
                         s->dwTransferId, (__int64) s->stat.qwTotalBytes,
                         (__int64) s->stat.qwTransferSize, s->stat.dwTotalTimeOut,
                         (int)(time (NULL) - s->stat.StartTime),
                         Ark + 1 < n ? "," : "");
                if (strlen (szBuf) + strlen (one) + 4 >= sizeof szBuf)  break;
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
        case C_TFTP_TRF_NEW :   /* 原生 ReportNewTrf 发出：含文件名、对端地址、启动时刻 */
        {
            const struct S_TftpTrfNew *p = (const struct S_TftpTrfNew *) msg_stuff;
            char szFile [_MAX_PATH], szPeer [64];
            char szAddr [MAXLEN_IPv6], szServ [12];
            time_t st = p->stat.StartTime;
            struct tm ltime;
            int Ark;
            lstrcpyn_cpal (szFile, sizeof szFile, p->szFile);
            for (Ark = 0 ; szFile [Ark] ; Ark ++)   /* 最小 JSON 转义：引号与反斜杠 */
                if (szFile [Ark] == '"' || szFile [Ark] == '\\')  szFile [Ark] = '/';
            /* 与原版 gui_tftpd.c AddNewTftpItem 一致：getnameinfo 数值地址 → "ip:port" */
            if (getnameinfo ((struct sockaddr *) & p->from_addr, sizeof p->from_addr,
                             szAddr, sizeof szAddr, szServ, sizeof szServ,
                             NI_NUMERICHOST | AI_NUMERICSERV) != 0)
            { szAddr [0] = 0;  szServ [0] = 0; }
            sprintf (szPeer, "%s:%s", szAddr, szServ);
            localtime_r (& st, & ltime);
            sprintf (szBuf, "{\"id\":%u,\"opcode\":%d,\"file\":\"%s\",\"peer\":\"%s\",\"start\":\"%02d:%02d:%02d\"}",
                     p->dwTransferId, p->opcode, szFile, szPeer,
                     ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
            Emit ("trf", szBuf);
            break;
        }
        case C_SYSLOG :
        {
            const struct S_SyslogMsg *pMsg = (const struct S_SyslogMsg *) msg_stuff;
            char szFrom [64], szTxt [SYSLOG_MAXMSG + 8], szJson [SYSLOG_MAXMSG * 2 + 200];

            PalJsonEscape (szFrom, sizeof szFrom, pMsg->from);
            PalJsonEscape (szTxt,  sizeof szTxt,  pMsg->txt);
            snprintf (szJson, sizeof szJson, "{\"from\":\"%s\",\"txt\":\"%s\"}", szFrom, szTxt);
            PalEmitEvent ("syslog", szJson);
            break;
        }
        case C_DNS_NEW_ENTRY :
        {
            const struct S_DNS_NewEntry *pMsg = (const struct S_DNS_NewEntry *) msg_stuff;
            char szName [NI_MAXHOST + 8], szV4 [64], szV6 [128], szJson [NI_MAXHOST * 2 + 300];

            PalJsonEscape (szName, sizeof szName, pMsg->name);
            PalJsonEscape (szV4,   sizeof szV4,   pMsg->ipv4);
            PalJsonEscape (szV6,   sizeof szV6,   pMsg->ipv6);
            snprintf (szJson, sizeof szJson,
                      "{\"name\":\"%s\",\"ipv4\":\"%s\",\"ipv6\":\"%s\"}",
                      szName, szV4, szV6);
            PalEmitEvent ("dns", szJson);
            break;
        }
        case C_SERVICES_STARTED : Emit ("started", "{}");  break;
        case C_TFTP_TRF_END :
        {
            /* 原版 GUI 保留已结束的传输行（显示 100%/ERR/STPD 与超时次数），
               这里把终止统计单独发出，避免 stat 只含活动传输导致行被移除。 */
            const struct S_TftpTrfEnd *e = (const struct S_TftpTrfEnd *) msg_stuff;
            char one [192];
            sprintf (one, "{\"id\":%u,\"bytes\":%lld,\"size\":%lld,\"ret\":%u,\"dur\":%d,\"rc\":%u}",
                     e->dwTransferId, (__int64) e->stat.qwTotalBytes,
                     (__int64) e->stat.qwTransferSize, e->stat.dwTotalTimeOut,
                     (int)(time (NULL) - e->stat.StartTime), e->stat.ret_code);
            Emit ("end", one);
            break;
        }
        default : break;   /* 其余消息类型里程碑 2/3 再接 */
    }
return TRUE;
}

int ReportNewTrf (const struct LL_TftpInfo *pTftp);   /* 原生实现在 tftpd_thread.c（含文件名），勿在此重复定义 */

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
