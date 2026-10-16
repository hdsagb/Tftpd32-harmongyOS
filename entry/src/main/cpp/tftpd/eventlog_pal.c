//////////////////////////////////////////////////////////
// eventlog_pal.c —— 替代 _libs/event_log/eventlog.c（Windows 事件日志 → 日志流）
//////////////////////////////////////////////////////////
#include "headers.h"

int WriteIntoEventLog (const char *txt, WORD evId)
{ LogToMonitor ("EventLog[%d]: %s", evId, txt);  return 0; }
