//////////////////////////////////////////////////////////
// ip_pal.c —— GetIPv4Address 等由 _services/ip_util.c 提供
// （依赖 Windows IPHlpApi，不移植；里程碑1 用桩，里程碑3 经 getifaddrs 实现）
//////////////////////////////////////////////////////////
#include "headers.h"

int GetIPv4Address (const char *szIf, char *szIP) { szIP [0] = 0;  return -1; }
int PoolNetworkInterfaces (void) { return TRUE; }   /* Scheduler 未移植，桩即可 */
int AnswerIPList (void) { return TRUE; }
void SendDirectoryContent (void) {}
