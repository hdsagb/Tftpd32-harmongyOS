//////////////////////////////////////////////////////////
// host_stubs.c —— 宿主测试替身（不属于被测逻辑）
// 只为让 unit under test 链接通过：配置读写、日志、网卡枚举、
// 以及 compat 头里声明的 POSIX 网络函数（Windows 上返回失败即可）。
// 测试不经过这些路径。
//////////////////////////////////////////////////////////
#include "headers.h"

#include <stdarg.h>
#include <stdio.h>

/* async_log_pal.c 的日志桩：输出到 stderr，便于测试中看到被测代码日志 */
void __cdecl LOG (int DebugLevel, const char *szFmt, ...)
{
va_list ap;
    if (DebugLevel > 1)  return;
    va_start (ap, szFmt);
    vfprintf (stderr, szFmt, ap);
    va_end (ap);
}

void LogToMonitor (char *sz, ...)
{
va_list ap;
    if (sz == NULL)  return;
    va_start (ap, sz);
    vfprintf (stderr, sz, ap);
    va_end (ap);
}

static void PalHostVLog (const char *szFmt, va_list ap)
{
    if (szFmt == NULL)  return;
    vfprintf (stderr, szFmt, ap);
    fputc ('\n', stderr);
}

void __cdecl SVC_ERROR (const char *szFmt, ...)
{ va_list ap;  va_start (ap, szFmt);  PalHostVLog (szFmt, ap);  va_end (ap); }

void __cdecl SVC_WARNING (const char *szFmt, ...)
{ va_list ap;  va_start (ap, szFmt);  PalHostVLog (szFmt, ap);  va_end (ap); }

/* linked_list.c 的消息队列：宿主测试单线程，直接返回 */
void WaitForMsgQueueToFinish (int id) { (void) id; }

/* 键读写由生产实现 registry_pal.c 提供：保存路径要真落盘才能验证
   “未登记键（Windows/N-A）保存后保留”这一台账结论 */

/* registry_pal.c 的 ini 互斥量：宿主测试单线程，恒成功 */
int pthread_mutex_lock (pthread_mutex_t *m)   { (void) m;  return 0; }
int pthread_mutex_unlock (pthread_mutex_t *m) { (void) m;  return 0; }

/* ip_pal.c 依赖的 POSIX 接口：宿主上无网卡枚举需求 */
int getifaddrs (struct ifaddrs **ifap)
{ if (ifap != NULL)  * ifap = NULL;  return -1; }
void freeifaddrs (struct ifaddrs *ifa) { (void) ifa; }

/* ---------- 字节序（宿主 x86 为小端） ---------- */
unsigned int htonl (unsigned int x)
{ return ((x & 0xFFu) << 24) | ((x & 0xFF00u) << 8) | ((x >> 8) & 0xFF00u) | (x >> 24); }
unsigned int ntohl (unsigned int x) { return htonl (x); }
unsigned short htons (unsigned short x)
{ return (unsigned short) (((x & 0xFFu) << 8) | ((x >> 8) & 0xFFu)); }
unsigned short ntohs (unsigned short x) { return htons (x); }

/* ---------- 地址转换（等价 POSIX 实现，足以支撑 AddrInc 等用例） ---------- */
unsigned int inet_addr (const char *cp)
{
unsigned int  o [4];
int i;

    if (cp == NULL)  return INADDR_NONE;
    for (i = 0 ; i < 4 ; i++)
    {
        char *pEnd = NULL;
        unsigned long v = strtoul (cp, & pEnd, 10);
        if (pEnd == cp || v > 255)  return INADDR_NONE;
        o [i] = (unsigned int) v;
        cp = pEnd;
        if (* cp == '.')  cp++;
    }
    return htonl ((o [0] << 24) | (o [1] << 16) | (o [2] << 8) | o [3]);
}

char *inet_ntoa (struct in_addr in)
{
static char buf [16];
unsigned int h = ntohl (in.s_addr);
    sprintf (buf, "%u.%u.%u.%u", (h >> 24) & 0xFFu, (h >> 16) & 0xFFu,
             (h >> 8) & 0xFFu, h & 0xFFu);
    return buf;
}

/* ---------- 套接字函数：宿主测试不建立连接 ---------- */
int socket (int domain, int type, int protocol)
{ (void) domain; (void) type; (void) protocol;  return -1; }
int bind (int s, const struct sockaddr *addr, int addrlen)
{ (void) s; (void) addr; (void) addrlen;  return -1; }
int connect (int s, const struct sockaddr *addr, int addrlen)
{ (void) s; (void) addr; (void) addrlen;  return -1; }
int sendto (int s, const void *buf, size_t len, int flags,
            const struct sockaddr *to, int tolen)
{ (void) s; (void) buf; (void) len; (void) flags; (void) to; (void) tolen;  return -1; }
int setsockopt (int s, int level, int optname, const void *optval, int optlen)
{ (void) s; (void) level; (void) optname; (void) optval; (void) optlen;  return -1; }
int getaddrinfo (const char *node, const char *service,
                 const struct addrinfo *hints, struct addrinfo **res)
{ (void) node; (void) service; (void) hints; if (res != NULL)  * res = NULL;  return -1; }
void freeaddrinfo (struct addrinfo *res) { (void) res; }

/* sys/ioctl.h / poll.h 替身实现（被测路径未使用） */
int ioctl (int fd, long cmd, ...) { (void) fd; (void) cmd;  return -1; }
int poll (struct pollfd *fds, unsigned long nfds, int timeout)
{ (void) fds; (void) nfds; (void) timeout;  return 0; }

/* scandir（ip_pal.c 的 ScanDir）：被测路径未使用 */
DIR *opendir (const char *name) { (void) name;  return NULL; }
struct dirent *readdir (DIR *d) { (void) d;  return NULL; }
int closedir (DIR *d) { (void) d;  return 0; }
