//////////////////////////////////////////////////////////
// async_log_pal.c —— 替代 async_log.c（与原签名一致：LOG/SVC_ERROR/SVC_WARNING）
//////////////////////////////////////////////////////////
#include "headers.h"

void __cdecl LOG (int DebugLevel, const char *szFmt, ...)
{
char buf [LOGSIZE], out [LOGSIZE + 16];
va_list ap;
    va_start (ap, szFmt);
    vsnprintf (buf, sizeof buf, szFmt, ap);
    va_end (ap);
    if (DebugLevel <= sSettings.LogLvl || DebugLevel <= 1)
    { sprintf (out, "[%d] %s", DebugLevel, buf);  LogToMonitor (out); }
}

void __cdecl SVC_ERROR (const char *szFmt, ...)
{
char buf [LOGSIZE], out [LOGSIZE + 16];
va_list ap;
    va_start (ap, szFmt);
    vsnprintf (buf, sizeof buf, szFmt, ap);
    va_end (ap);
    sprintf (out, "ERROR: %s", buf);
    LogToMonitor (out);
}

void __cdecl SVC_WARNING (const char *szFmt, ...)
{
char buf [LOGSIZE], out [LOGSIZE + 16];
va_list ap;
    va_start (ap, szFmt);
    vsnprintf (buf, sizeof buf, szFmt, ap);
    va_end (ap);
    sprintf (out, "WARNING: %s", buf);
    LogToMonitor (out);
}
