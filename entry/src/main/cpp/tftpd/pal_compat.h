//////////////////////////////////////////////////////////
// Tftpd32 HarmonyOS PAL —— Win32/WinSock → POSIX(musl)
// 原则：与原生行为一致，本文件只做 API 形状映射，不改协议逻辑
//////////////////////////////////////////////////////////
#ifndef PAL_COMPAT_H
#define PAL_COMPAT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <pthread.h>
#include <time.h>
#include <dirent.h>
#include <assert.h>

/* Windows CRT/安全函数 → POSIX 等价 */
#define strnlen_s(s,n)        strnlen((s),(n))
#define _atoi64(s)            strtoll((s),NULL,10)
#define GetCurrentThreadId()  (0)

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 基本类型 ---------- */
typedef int BOOL; typedef unsigned char BYTE; typedef unsigned short WORD;
typedef unsigned int DWORD; typedef unsigned int UINT; typedef int INT;
typedef long LONG; typedef void *LPVOID; typedef const char *LPCSTR; typedef char *LPSTR;
typedef const void *LPCVOID;
typedef void *HWND;          /* GUI 句柄：仅结构体占位（tftp_struct.h），永不解引用 */
typedef void *HINSTANCE;     /* GUI 句柄：仅声明占位（custom.h GuiMain），永不解引用 */
typedef long long __int64;
typedef int SOCKET;
typedef unsigned long u_long;
#define TRUE 1
#define FALSE 0
#define WINAPI
#define __cdecl
#define MAX_PATH 256
#define _MAX_PATH MAX_PATH
#define MAX_ADAPTER_DESCRIPTION_LENGTH 256
#define ERROR_SUCCESS 0
#ifndef max
#define max(a,b) (((a)>(b))?(a):(b))
#endif
#define min(a,b) (((a)<(b))?(a):(b))
#define LOWORD(x) ((WORD)((DWORD)(x) & 0xFFFF))
#define HIWORD(x) ((WORD)(((DWORD)(x) >> 16) & 0xFFFF))

/* ---------- 错误码 ---------- */
#define GetLastError()   (errno)
#define WSAGetLastError() (errno)
#define SetLastError(e)  ((void)((errno) = (e)))
#define WSAEADDRINUSE      EADDRINUSE
#define WSAEADDRNOTAVAIL   EADDRNOTAVAIL
#define WSAEINVAL          EINVAL
#define WSAEWOULDBLOCK     EAGAIN
#define WSAHOST_NOT_FOUND  ENOENT
#define ERROR_DIRECTORY    ENOTDIR
#define ERROR_BAD_LENGTH   EINVAL
#define ERROR_ACCESS_DENIED EACCES

/* ---------- 字符串杂项 ---------- */
#define wsprintf   sprintf
#define lstrcpy    strcpy
#define lstrcat    strcat
#define lstrlen(x) ((int)strlen(x))
#define lstrcmpi(a,b) strcasecmp(a,b)
#define MB_OK               0x00000000L
#define MB_ICONSTOP         0x00000010L
#define MB_ICONEXCLAMATION  0x00000030L
#define MB_ICONASTERISK     0x00000040L
#define MB_ICONINFORMATION  0x00000040L
#define MessageBox(...) (0)
#define MessageBeep(...) (0)
#define OutputDebugString(x) ((void)0)
#define _endthread() pthread_exit(NULL)
/* 线程优先级：POSIX 侧不做优先级区分，全部为空操作 */
#define THREAD_PRIORITY_ABOVE_NORMAL   1
#define THREAD_PRIORITY_BELOW_NORMAL  -1
#define THREAD_PRIORITY_TIME_CRITICAL  15
#define GetCurrentThread() ((HANDLE)NULL)
#define SetThreadPriority(...) (1)

/* ---------- Socket ---------- */
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket close
#define ioctlsocket  ioctl
typedef struct sockaddr_storage SOCKADDR_STORAGE;
typedef struct sockaddr SOCKADDR, *PSOCKADDR, *LPSOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN, *PSOCKADDR_IN;
typedef struct addrinfo ADDRINFO;
#define FD_READ 1
/* WSA 事件系列仅在未打补丁路径出现 → 空实现 */
#define WSAStartup(a,b)   (0)
#define WSACleanup()      (0)
#define WSACreateEvent()  (CreateEvent(NULL, FALSE, FALSE, NULL))
#define WSACloseEvent(e)  (CloseHandle(e))
#define WSAEventSelect(...) (0)
#define WSAIsBlocking()   (0)
#define WSACancelBlockingCall() (0)

/* ---------- 文件 API ---------- */
#define GENERIC_READ              0x80000000L
#define GENERIC_WRITE             0x40000000L
#define FILE_SHARE_READ           1
#define FILE_SHARE_WRITE          2
#define CREATE_NEW                1
#define CREATE_ALWAYS             2
#define OPEN_EXISTING             3
#define OPEN_ALWAYS               4
#define TRUNCATE_EXISTING         5
#define FILE_ATTRIBUTE_NORMAL     0x80
#define FILE_ATTRIBUTE_ARCHIVE    0x20
#define FILE_ATTRIBUTE_DIRECTORY  0x10
#define FILE_FLAG_SEQUENTIAL_SCAN 0
#define FILE_BEGIN   0   /* 与 POSIX SEEK_SET/SEEK_CUR/SEEK_END 数值一致 */
#define FILE_CURRENT 1
#define FILE_END     2
#define INVALID_SET_FILE_POINTER ((DWORD)-1)

typedef struct { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;
/* 匿名结构体成员：原生代码按 li.LowPart / li.HighPart / li.QuadPart 访问 */
typedef union { struct { DWORD LowPart; LONG HighPart; }; __int64 QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
                 DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;
#define GetFileExInfoStandard 0

/* ---------- 句柄体系（tag 区分 event/file/thread/pipe-event） ---------- */
enum { PAL_TAG_EVENT = 1, PAL_TAG_FILE, PAL_TAG_THREAD, PAL_TAG_PIPEEV, PAL_TAG_MUTEX };
struct PAL_HANDLE__
{
    int  tag;
    int  fd;                 /* file fd；pipe-event 的写端 */
    int  rfd;                /* pipe-event 的读端 */
    pthread_t th;  int th_valid;
    pthread_mutex_t mx;  pthread_cond_t cv;
    int state;  int manual;
};
typedef struct PAL_HANDLE__ *HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)(long)-1)
#define INFINITE 0xFFFFFFFF

/* ---------- 文件/线程/事件 API ---------- */
HANDLE CreateFile (LPCSTR szName, DWORD access, DWORD share, LPVOID sa,
                   DWORD disp, DWORD flags, HANDLE hTemplate);
BOOL   ReadFile   (HANDLE h, LPVOID buf, DWORD nToRead, DWORD *pRead, LPVOID ov);
BOOL   WriteFile  (HANDLE h, LPCVOID buf, DWORD nToWrite, DWORD *pWritten, LPVOID ov);
BOOL   SetFilePointerEx (HANDLE h, LARGE_INTEGER liDist, PLARGE_INTEGER pNew, DWORD whence);
BOOL   GetFileSizeEx (HANDLE h, PLARGE_INTEGER pSize);
BOOL   CloseHandle (HANDLE h);
BOOL   GetFileAttributesEx (LPCSTR szFile, DWORD level, LPVOID pBuf);

HANDLE CreateEvent (LPVOID sa, BOOL manual, BOOL init, LPCSTR name);
BOOL   SetEvent (HANDLE h);
BOOL   ResetEvent (HANDLE h);
HANDLE CreateThread (LPVOID sa, DWORD stack, DWORD (WINAPI *fn)(LPVOID), LPVOID arg, DWORD fl, DWORD *pid);
DWORD  SuspendThread (HANDLE h);
BOOL   TerminateThread (HANDLE h, DWORD code);
DWORD  WaitForSingleObject (HANDLE h, DWORD ms);
void   Sleep (DWORD ms);

HANDLE PalCreatePipeEvent (void);   /* SetEvent=写唤醒管道，供 tftpd_main poll 用 */
int    PalWakeFd (HANDLE h);
void   PalFreeThreadHandle (HANDLE h);  /* 已退出线程的 HANDLE 回收 */

/* Mutex（linked_list.c 用） */
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT  0x102
HANDLE CreateMutex (LPVOID sa, BOOL initOwner, LPCSTR name);
BOOL   ReleaseMutex (HANDLE h);

/* ---------- 服务启停（service_main.c） ---------- */
int  TftpdServiceStart (void);
void TftpdServiceStop  (void);

/* ---------- SYSTEMTIME（sntpd.c 使用） ---------- */
typedef struct
{
    WORD  wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

void  GetSystemTime (SYSTEMTIME *pst);
DWORD GetTickCount (void);
DWORD SetFilePointer (HANDLE h, LONG dist, LONG *pHigh, DWORD method);

/* ---------- 套接字别名（syslogd.c / dns.c 原样使用） ---------- */
/* LPSOCKADDR 已在上方 Socket 段定义，此处不重复 typedef */
typedef struct in6_addr IN6_ADDR;

#ifndef __isascii
#define __isascii(c) (((unsigned char)(c)) < 0x80)
#endif

/* ---------- 扩展事件（console_pal.c） ---------- */
void PalEmitEvent (const char *szType, const char *szJson);
void PalJsonEscape (char *dst, int nDst, const char *src);

/* ---------- M2 服务与工具 ---------- */
int  PalServiceStartMask (unsigned uMask);
void PalServiceStopMask  (unsigned uMask);
int  PalClientStart (int opcode, const char *szHost, int nPort,
                     const char *szRemote, const char *szLocal, int nBlkSize);
BOOL PalComputeMd5 (const char *szPath, char *szOut, int nOut);
int  PalListDirectory (const char *szPath, char *szJson, int nJson);
BOOL PalDnsHostsLookup (const char *szName, long *plIpv4, IN6_ADDR *pIpv6,
                        int *pnNbV6, char *szCname);

#ifdef __cplusplus
}
#endif
#endif /* PAL_COMPAT_H */
