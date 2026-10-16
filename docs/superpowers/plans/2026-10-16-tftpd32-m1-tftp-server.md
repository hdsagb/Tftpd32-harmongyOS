# Tftpd32 鸿蒙适配 —— 里程碑1：TFTP 服务器 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 HarmonyOS（PC/手机）上运行移植自 Tftpd64 v4.74 原生 C 源码的 TFTP 服务器，ArkUI 界面提供 设置/TFTP/日志 三页签，与原生 Tftpd32 行为逐字节一致（兼容性强原则）。

**Architecture:** 协议核心（tftpd_main.c / tftpd_thread.c）**原样拷贝不重写**，通过新写的 PAL 兼容层（pal_compat.h/.c）将 Win32/WinSock API 映射到 POSIX(musl)；`to_console.c`/`async_log.c`/`settings.c`/`start_threads.c` 用同签名替代实现（console_pal.c / async_log_pal.c / settings_pal.c / service_main.c），日志与进度经 Node-API 线程安全函数上报 ArkTS；UI 用 ArkUI Tabs 忠实复刻原版布局。

**Tech Stack:** DevEco Studio 5 / HarmonyOS 6.1.1(API 24)，NDK + CMake + Node-API，ArkTS/ArkUI，backgroundTaskManager 长时任务。

**参照文档:** [Tftpd32-HarmonyOS适配方案](../../Tftpd32-HarmonyOS适配方案.md)（§3.1 四条原则，含 **兼容性强**；§5 风险 R1 端口映射）

---

## 文件结构（本计划产出的全部文件）

```
entry/src/main/cpp/                     【新建】native 模块
├── CMakeLists.txt                      构建脚本
├── napi_init.cpp                       Node-API 桥（TSF 事件回调）
├── types/libtftpd32/Index.d.ts         ArkTS 类型声明
├── types/libtftpd32/oh-package.json5
└── tftpd/                              移植的 Tftpd32 核心
    ├── pal_compat.h / pal_compat.c     【新写】Win32→POSIX 兼容层
    ├── headers.h                       【新写】替代原 headers.h 的聚合头
    ├── service_main.c                  【新写】替代 start_threads.c（仅 TFTP）
    ├── console_pal.c                   【新写】替代 to_console.c（消息→NAPI）
    ├── async_log_pal.c                 【新写】替代 async_log.c（LOG 同签名）
    ├── settings_pal.c                  【新写】替代 settings.c+registry.c（KEY_* 同名）
    ├── ip_pal.c / eventlog_pal.c       【新写】GetIPv4Address 等桩
    ├── tftpd_main.c                    【拷贝+补丁】唯一改动的原文件（poll 替换 WSAEventSelect）
    ├── tftpd_thread.c tftpd_util.c tftp_dbg.c threading.h tftpd_functions.h   【原样拷贝】
    ├── settings.h Tftp.h tftp_struct.h custom.h dialog_socket.h async_log.h  【原样拷贝】
    └── libs/                           【原样拷贝】md5/ linked_list/ lasterr/
entry/src/main/ets/
├── services/TftpdService.ets           native 封装 + 设置持久化
└── pages/Index.ets                     三页签 UI
docs/superpowers/plans/                 本计划
```

**不拷贝**：`_gui/` 全部、`_libs/GUI`、`_libs/registry`、`_libs/scandir`、`_libs/log`（LogToMonitor 由 console_pal.c 重实现）、`_libs/dump`（仅 DEBUG 用）、`_services/` 其余服务（里程碑 2/3）。

---

### Task 0: 仓库初始化

- [ ] **Step 0.1: git init 并提交基线**

```powershell
git init
```

编辑根目录 `.gitignore`，追加一行（原生上游代码不入库，保持 GPLv2 补丁式分离）：

```
/reference/
```

```powershell
git add -A; git commit -m "chore: baseline HarmonyOS empty project + Tftpd64 v4.74 reference"
```

---

### Task 1: Native 工程骨架（编译通过的空 .so）

**Files:**
- Create: `entry/src/main/cpp/CMakeLists.txt`
- Create: `entry/src/main/cpp/napi_init.cpp`
- Create: `entry/src/main/cpp/types/libtftpd32/Index.d.ts`
- Create: `entry/src/main/cpp/types/libtftpd32/oh-package.json5`
- Modify: `entry/build-profile.json5`
- Modify: 根 `oh-package.json5`

- [ ] **Step 1.1: 写 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.5.1)
project(Tftpd32)
set(CMAKE_C_STANDARD 11)
# -fcommon 必须：threading.h 中 tThreads[] 为公共符号（原工程 C89 语义）
add_compile_options(-fcommon -O2 -Wall -Wno-format -Wno-pointer-sign -Wno-unused-result)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tftpd ${CMAKE_CURRENT_SOURCE_DIR}/tftpd/libs)
add_library(tftpd32 SHARED
    napi_init.cpp
    tftpd/tftpd_main.c tftpd/tftpd_thread.c tftpd/tftpd_util.c tftpd/tftp_dbg.c
    tftpd/pal_compat.c tftpd/settings_pal.c tftpd/console_pal.c tftpd/async_log_pal.c
    tftpd/service_main.c tftpd/ip_pal.c tftpd/eventlog_pal.c
    tftpd/libs/linked_list/linked_list.c tftpd/libs/md5/md5.c tftpd/libs/lasterr/lasterr.c)
target_link_libraries(tftpd32 PUBLIC libace_napi.z.so)
```

- [ ] **Step 1.2: 写临时 napi_init.cpp（Task 7 替换为完整版）**

```cpp
#include "napi/native_api.h"
EXTERN_C_START static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {};
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END
static napi_module demoModule = { .nm_version = 1, .nm_flags = 0, .nm_filename = nullptr,
    .nm_register_func = Init, .nm_modname = "tftpd32", .nm_priv = nullptr, .reserved = { 0 } };
extern "C" __attribute__((constructor)) void RegisterTftpd32Module(void) { napi_module_register(&demoModule); }
```

- [ ] **Step 1.3: 写类型声明与包描述**

`entry/src/main/cpp/types/libtftpd32/Index.d.ts`：

```ts
export const setEventCallback: (cb: (e: string) => void) => void;
export const setConfigPath: (path: string) => void;
export const startService: () => boolean;
export const stopService: () => void;
export const setSettingInt: (key: string, val: number) => void;
export const setSettingStr: (key: string, val: string) => void;
export const saveSettings: () => boolean;
export const readSettings: () => boolean;
```

`entry/src/main/cpp/types/libtftpd32/oh-package.json5`：

```json5
{ "name": "libtftpd32.so", "types": "./Index.d.ts", "version": "", "description": "" }
```

- [ ] **Step 1.4: 挂接构建配置**

读 `entry/build-profile.json5`，在 `targets[0]` 同级增加（若已有 `buildOption` 则合并键）：

```json5
"buildOption": {
  "externalNativeOptions": {
    "path": "./src/main/cpp/CMakeLists.txt",
    "abiFilters": ["arm64-v8a", "x86_64"]
  }
}
```

读根 `oh-package.json5`，`dependencies` 增加：

```json5
"libtftpd32.so": "file:./entry/src/main/cpp/types/libtftpd32"
```

- [ ] **Step 1.5: 构建验证**

```powershell
hvigorw --mode module -p module=entry@default -p product=default assembleHap
```

Expected: BUILD SUCCESSFUL，产物含 `libtftpd32.so`。

- [ ] **Step 1.6: Commit**

```powershell
git add -A; git commit -m "feat(native): NDK skeleton builds empty libtftpd32.so"
```

---

### Task 2: 拷贝原生源码 + PAL 兼容层（核心）

**Files:**
- Copy: `reference/tftpd64/src/_services/{tftpd_main.c,tftpd_thread.c,tftpd_util.c,tftp_dbg.c,threading.h,tftpd_functions.h,async_log.h}` → `entry/src/main/cpp/tftpd/`
- Copy: `reference/tftpd64/src/_common/{settings.h,Tftp.h,tftp_struct.h,custom.h,dialog_socket.h}` → 同上
- Copy: `reference/tftpd64/src/_libs/{linked_list/linked_list.c,linked_list/linked_list.h,md5/md5.c,md5/md5.h,md5/global.h,lasterr/lasterr.c,lasterr/lasterr.h}` → `entry/src/main/cpp/tftpd/libs/`（保持相对目录）
- Create: `entry/src/main/cpp/tftpd/pal_compat.h`、`pal_compat.c`、`headers.h`

- [ ] **Step 2.1: 拷贝命令**

```powershell
New-Item -ItemType Directory -Force entry\src\main\cpp\tftpd\libs\md5, entry\src\main\cpp\tftpd\libs\linked_list, entry\src\main\cpp\tftpd\libs\lasterr | Out-Null
Copy-Item reference\tftpd64\src\_services\{tftpd_main.c,tftpd_thread.c,tftpd_util.c,tftp_dbg.c,threading.h,tftpd_functions.h,async_log.h} entry\src\main\cpp\tftpd\
Copy-Item reference\tftpd64\src\_common\{settings.h,Tftp.h,tftp_struct.h,custom.h,dialog_socket.h} entry\src\main\cpp\tftpd\
Copy-Item reference\tftpd64\src\_libs\linked_list\* entry\src\main\cpp\tftpd\libs\linked_list\
Copy-Item reference\tftpd64\src\_libs\md5\* entry\src\main\cpp\tftpd\libs\md5\
Copy-Item reference\tftpd64\src\_libs\lasterr\* entry\src\main\cpp\tftpd\libs\lasterr\
# 拷贝件不做任何修改（tftpd_main.c 除外，Task 6）
```

- [ ] **Step 2.2: 写 pal_compat.h（完整）**

```c
//////////////////////////////////////////////////////////
// Tftpd32 HarmonyOS PAL —— Win32/WinSock → POSIX(musl)
// 原则：与原生行为一致，本文件只做 API 形状映射，不改逻辑
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

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 基本类型 ---------- */
typedef int BOOL; typedef unsigned char BYTE; typedef unsigned short WORD;
typedef unsigned int DWORD; typedef unsigned int UINT; typedef int INT;
typedef long LONG; typedef void *LPVOID; typedef const char *LPCSTR; typedef char *LPSTR;
typedef long long __int64;
typedef int SOCKET;
typedef unsigned long u_long;
typedef const void *LPCVOID;
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
#define WSAGetLastErrorText() (strerror(errno))
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
#define MessageBox(...) (0)
#define OutputDebugString(x) ((void)0)

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
/* WSACreateEvent/WSAEventSelect 等仅在未打补丁路径出现 → 空实现，见 pal_compat.c */

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
typedef union { struct { DWORD LowPart; LONG HighPart; } u; __int64 QuadPart; } LARGE_INTEGER, *PLARGE_INTEGER;
typedef struct { DWORD dwFileAttributes; FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
                 DWORD nFileSizeHigh, nFileSizeLow; } WIN32_FILE_ATTRIBUTE_DATA;
#define GetFileExInfoStandard 0

/* ---------- 句柄体系（tag 区分 event/file/thread/pipe-event） ---------- */
enum { PAL_TAG_EVENT = 1, PAL_TAG_FILE, PAL_TAG_THREAD, PAL_TAG_PIPEEV };
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

/* ---------- API ---------- */
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

/* ---------- WinSock 空实现 ---------- */
#define WSAStartup(a,b)   (0)
#define WSACleanup()      (0)
#define WSACreateEvent()  (CreateEvent(NULL, FALSE, FALSE, NULL))
#define WSACloseEvent(e)  (CloseHandle(e))
#define WSAEventSelect(...) (0)
#define WSAIsBlocking()   (0)
#define WSACancelBlockingCall() (0)

#ifdef __cplusplus
}
#endif
#endif /* PAL_COMPAT_H */
```

- [ ] **Step 2.3: 写 pal_compat.c（完整）**

```c
#include "headers.h"

HANDLE PalMake (int tag) { HANDLE h = calloc (1, sizeof *h); h->tag = tag; return h; }

HANDLE CreateFile (LPCSTR szName, DWORD access, DWORD share, LPVOID sa,
                   DWORD disp, DWORD flags, HANDLE hTemplate)
{
int fl = 0;
    if (access & GENERIC_READ)   fl = O_RDONLY;
    if (access & GENERIC_WRITE)  fl = (access & GENERIC_READ) ? O_RDWR : O_WRONLY;
    switch (disp)
    {
        case CREATE_NEW        : fl |= O_CREAT | O_EXCL;  break;
        case CREATE_ALWAYS     : fl |= O_CREAT | O_TRUNC; break;
        case OPEN_ALWAYS       : fl |= O_CREAT;           break;
        case TRUNCATE_EXISTING : fl |= O_TRUNC;           break;
        default : break;
    }
    if (flags & 0x40000000L /* FILE_FLAG_WRITE_THROUGH n/a */) { /* ignore */ }
    int fd = open (szName, fl, 0644);
    if (fd < 0)  return INVALID_HANDLE_VALUE;
    HANDLE h = PalMake (PAL_TAG_FILE);
    h->fd = fd;
return h;
}

BOOL ReadFile (HANDLE h, LPVOID buf, DWORD nToRead, DWORD *pRead, LPVOID ov)
{ *pRead = (DWORD) read (h->fd, buf, nToRead);  return *pRead != (DWORD)-1; }

BOOL WriteFile (HANDLE h, LPCVOID buf, DWORD nToWrite, DWORD *pWritten, LPVOID ov)
{ *pWritten = (DWORD) write (h->fd, buf, nToWrite);  return *pWritten != (DWORD)-1; }

BOOL SetFilePointerEx (HANDLE h, LARGE_INTEGER liDist, PLARGE_INTEGER pNew, DWORD whence)
{ off_t rc = lseek (h->fd, (off_t) liDist.QuadPart, (int) whence);
  if (pNew != NULL)  pNew->QuadPart = (__int64) rc;
return rc != (off_t)-1; }

BOOL GetFileSizeEx (HANDLE h, PLARGE_INTEGER pSize)
{ struct stat st;  if (fstat (h->fd, & st) != 0)  return FALSE;
  pSize->QuadPart = (__int64) st.st_size;  return TRUE; }

BOOL CloseHandle (HANDLE h)
{
    if (h == NULL || h == INVALID_HANDLE_VALUE)  return FALSE;
    switch (h->tag)
    {
        case PAL_TAG_FILE    : close (h->fd);  break;
        case PAL_TAG_PIPEEV  : close (h->fd); close (h->rfd); break;
        case PAL_TAG_EVENT   : pthread_mutex_destroy (& h->mx); pthread_cond_destroy (& h->cv); break;
        case PAL_TAG_THREAD  : /* 不 join，仅丢弃 */ break;
    }
    free (h);
return TRUE;
}

BOOL GetFileAttributesEx (LPCSTR szFile, DWORD level, LPVOID pBuf)
{
struct stat st;
WIN32_FILE_ATTRIBUTE_DATA *fad = (WIN32_FILE_ATTRIBUTE_DATA *) pBuf;
    if (stat (szFile, & st) != 0)  return FALSE;
    fad->dwFileAttributes = (st.st_mode & S_IFDIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    fad->nFileSizeHigh = (DWORD) ((unsigned long long) st.st_size >> 32);
    fad->nFileSizeLow  = (DWORD) (st.st_size & 0xFFFFFFFFu);
    fad->ftLastWriteTime.dwLowDateTime  = (DWORD) st.st_mtime;
    fad->ftLastWriteTime.dwHighDateTime = 0;
return TRUE;
}

HANDLE CreateEvent (LPVOID sa, BOOL manual, BOOL init, LPCSTR name)
{
    HANDLE h = PalMake (PAL_TAG_EVENT);
    h->manual = manual;  h->state = init;
    pthread_mutex_init (& h->mx, NULL);
    pthread_cond_init  (& h->cv, NULL);
return h;
}

BOOL SetEvent (HANDLE h)
{
    pthread_mutex_lock (& h->mx);
    h->state = 1;
    if (h->manual)  pthread_cond_broadcast (& h->cv);
    else            pthread_cond_signal   (& h->cv);
    pthread_mutex_unlock (& h->mx);
return TRUE;
}

BOOL ResetEvent (HANDLE h)
{
    if (h->tag == PAL_TAG_PIPEEV)
    { char buf[64];  while (read (h->rfd, buf, sizeof buf) > 0) ;  return TRUE; }
    pthread_mutex_lock (& h->mx);  h->state = 0;  pthread_mutex_unlock (& h->mx);
return TRUE;
}

HANDLE PalCreatePipeEvent (void)
{
int p[2];
    if (pipe (p) != 0)  return INVALID_HANDLE_VALUE;
    HANDLE h = PalMake (PAL_TAG_PIPEEV);
    h->fd = p[1];   /* 写端：SetEvent 用 */
    h->rfd = p[0];  /* 读端：tftpd_main poll 用 */
return h;
}

int PalWakeFd (HANDLE h) { return h->rfd; }

DWORD WaitForSingleObject (HANDLE h, DWORD ms)
{
    if (h->tag == PAL_TAG_THREAD)
    {  /* 关停路径专用：忽略超时（musl 无 timedjoin），线程必已/即将退出 */
       if (h->th_valid)  pthread_join (h->th, NULL),  h->th_valid = 0;
       return 0;
    }
    pthread_mutex_lock (& h->mx);
    while (! h->state)
    {
        if (ms == INFINITE)  pthread_cond_wait (& h->cv, & h->mx);
        else
        {
            struct timespec ts;  clock_gettime (CLOCK_REALTIME, & ts);
            ts.tv_sec += ms / 1000;  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L)  ts.tv_sec ++,  ts.tv_nsec -= 1000000000L;
            if (pthread_cond_timedwait (& h->cv, & h->mx, & ts) != 0)
            { pthread_mutex_unlock (& h->mx);  return 0x102 /*WAIT_TIMEOUT*/; }
        }
    }
    if (! h->manual)  h->state = 0;   /* auto-reset */
    pthread_mutex_unlock (& h->mx);
return 0;
}

HANDLE CreateThread (LPVOID sa, DWORD stack, DWORD (WINAPI *fn)(LPVOID), LPVOID arg, DWORD fl, DWORD *pid)
{
    HANDLE h = PalMake (PAL_TAG_THREAD);
    /* 原生线程函数为 DWORD WINAPI f(LPVOID)；经联合体转型，musl/x86_64/aarch64 ABI 下安全 */
    if (pthread_create (& h->th, NULL, (void * (*)(void *)) fn, arg) == 0)
    { h->th_valid = 1;  if (pid != NULL)  *pid = (DWORD) h->th; }
    else { free (h);  return INVALID_HANDLE_VALUE; }
return h;
}

DWORD SuspendThread (HANDLE h) { return 0; }   /* 仅 TftpdCleanup 关停路径使用 */

BOOL TerminateThread (HANDLE h, DWORD code)
{
    if (h->th_valid)  pthread_cancel (h->th);  /* musl 支持 */
return TRUE;
}

void PalFreeThreadHandle (HANDLE h)
{
    if (h != NULL && h != INVALID_HANDLE_VALUE && h->tag == PAL_TAG_THREAD)
    {  if (h->th_valid)  { pthread_detach (h->th);  h->th_valid = 0; }  free (h); }
}

void Sleep (DWORD ms) { usleep (ms * 1000u); }
```

- [ ] **Step 2.4: 写新 headers.h（聚合头，替代原 headers.h）**

```c
//////////////////////////////////////////////////////////
// Tftpd32 on HarmonyOS —— 聚合头（替代原 _common/headers.h）
// 被 tftpd_main.c / tftpd_thread.c / tftpd_util.c / tftp_dbg.c 原样 include
//////////////////////////////////////////////////////////
#ifndef PAL_HEADERS_H
#define PAL_HEADERS_H

#include "pal_compat.h"          /* 全部 Win32 兼容定义，必须在最前 */

#include "_hdr_settings.h"       /* 拷贝件改名，避免与目录内同名冲突 */
#include "_hdr_Tftp.h"
#include "_hdr_tftp_struct.h"
#include "_hdr_dialog_socket.h"
#include "_hdr_custom.h"

#include "threading.h"           /* 内含 async_log.h */
#include "lasterr.h"             /* libs/lasterr/lasterr.h（include_directories 已指到 libs/） */
#include "md5.h"                 /* libs/md5/md5.h */

#include "LogToMonitor.h"        /* console_pal.c 提供实现 */

#endif
```

**注意 include 解析**：CMake 已 `include_directories(tftpd tftpd/libs)`，因此 `lasterr.h`/`md5.h`/`LogToMonitor.h` 按文件名解析到 `libs/` 下；`threading.h` 解析到 `tftpd/`。**拷贝件重命名**（避免拷贝件 include 原路径失效）：

```powershell
Rename-Item entry\src\main\cpp\tftpd\settings.h _hdr_settings.h
Rename-Item entry\src\main\cpp\tftpd\Tftp.h _hdr_Tftp.h
Rename-Item entry\src\main\cpp\tftpd\tftp_struct.h _hdr_tftp_struct.h
Rename-Item entry\src\main\cpp\tftpd\dialog_socket.h _hdr_dialog_socket.h
Rename-Item entry\src\main\cpp\tftpd\custom.h _hdr_custom.h
Copy-Item reference\tftpd64\src\_libs\log\LogToMonitor.h entry\src\main\cpp\tftpd\
```

`_hdr_custom.h` 顶部原样保留，但需手工把 `#define APPLICATION_NAME` 分支改为：

```c
#define APPLICATION_NAME   "Tftpd32"
```

（原文件按 `_M_X64` 判定，鸿蒙编译器无此宏；其余内容不动。）

- [ ] **Step 2.5: Commit**

```powershell
git add -A; git commit -m "feat(pal): port Tftpd64 TFTP core verbatim + Win32->POSIX compat layer"
```

---

### Task 3: settings_pal.c（键名兼容原版）

**Files:**
- Create: `entry/src/main/cpp/tftpd/settings_pal.c`

- [ ] **Step 3.1: 写 settings_pal.c（完整）**

键名与原版注册表**完全一致**（兼容性强原则 b/d）；默认端口按适配方案 §5-R1 用 **6900**，其余默认值逐项对照原 `settings.c` 初始化器。

```c
//////////////////////////////////////////////////////////
// settings_pal.c —— 替代 settings.c + registry.c
// 文件格式：KEY=value 行（KEY 名与原版注册表键一致）
//////////////////////////////////////////////////////////
#include "headers.h"

struct S_Tftpd32Settings sSettings =
{
    ".",                    // BaseDirectory
    TFTPD32_DEF_LOG_LEVEL,  // LogLvl
    TFTP_TIMEOUT,           // Timeout
    TFTP_RETRANSMIT,        // Retransmit
    0,                      // WinSize
    SECURITY_STD,           // SecurityLvl
    6900,                   // Port（R1：应用禁止绑 <1024；原版默认 69，UI 可改回）
    FALSE,                  // bHide
    TRUE,                   // bNegociate
    FALSE,                  // bPXECompatibility
    TRUE,                   // bProgressBar
    FALSE,                  // bDirText
    FALSE,                  // bMD5
    FALSE,                  // bResumeOption
    TRUE,                   // bUnixStrings
    FALSE,                  // bBeep
    FALSE,                  // bVirtualRoot
    "",                     // szTftpLocalIP
    TFTPD32_TFTP_SERVER,    // uServices（里程碑1 仅 TFTP）
    0, 0,                   // nTftpLowPort / nTftpHighPort
    "",                     // szTftpLogFile
    "",                     // szSyslogFile
    FALSE,                  // bSyslogPipe
    "",                     // szMCastAddr
    0,                      // dwMCastPort
    1,                      // bPersLeases
    0,                      // bUnicastBOOTP
    "",                     // szDHCPLocalIP
    FALSE,                  // bEventLog
    "tftpd32",              // szConsolePwd
    FALSE,                  // bPortOption
    0,                      // nGuiRemanence
    FALSE,                  // bIgnoreLastBlockAck
    TRUE,                   // bIPv4
    TRUE,                   // bIPv6
    FALSE,                  // bReduceTFTPPath
    "",                     // szHttpDirectory
    100,                    // dwMaxTftpTransfers
    "",                     // szWorkingDirectory
    1000,                   // dwRefreshInterval
    0,                      // uConsolePort
    FALSE,                  // bTftpOnPhysicalIf
    0,                      // uRunningServices
};

static char s_szCfgPath [MAX_PATH] = "";

static void lstrcpyn_pal (char *dst, int n, const char *src)
{ strncpy (dst, src, n - 1);  dst [n - 1] = 0; }

void Pal_SetConfigPath (const char *szPath)   /* napi_init 传入沙箱路径 */
{ lstrcpyn_pal (s_szCfgPath, sizeof s_szCfgPath, szPath); }
```

同文件追加（键值表驱动，读写同一张表；KEY_* 宏来自 `_hdr_settings.h`，直接可用）：

```c
enum { PAL_T_INT, PAL_T_STR };
struct PalKey { const char *name; void *p; int type; int size; };
static const struct PalKey tKeys [] =
{
    { KEY_BASEDIR,              & sSettings.szBaseDirectory,        PAL_T_STR, sizeof sSettings.szBaseDirectory },
    { KEY_DEFSECURITY,          & sSettings.SecurityLvl,            PAL_T_INT, 0 },
    { KEY_TIMEOUT,              & sSettings.Timeout,                PAL_T_INT, 0 },
    { KEY_MAXRETRANSMIT,        & sSettings.Retransmit,             PAL_T_INT, 0 },
    { KEY_PORT,                 & sSettings.Port,                   PAL_T_INT, 0 },
    { KEY_WINSIZE,              & sSettings.WinSize,                PAL_T_INT, 0 },
    { KEY_NEGOCIATE,            & sSettings.bNegociate,             PAL_T_INT, 0 },
    { KEY_PXE,                  & sSettings.bPXECompatibility,      PAL_T_INT, 0 },
    { KEY_DIRTEXT,              & sSettings.bDirText,               PAL_T_INT, 0 },
    { KEY_PROGRESS,             & sSettings.bProgressBar,           PAL_T_INT, 0 },
    { KEY_MD5,                  & sSettings.bMD5,                   PAL_T_INT, 0 },
    { KEY_UNIX,                 & sSettings.bUnixStrings,           PAL_T_INT, 0 },
    { KEY_BEEP,                 & sSettings.bBeep,                  PAL_T_INT, 0 },
    { KEY_VROOT,                & sSettings.bVirtualRoot,           PAL_T_INT, 0 },
    { KEY_TFTPLOCALIP,          & sSettings.szTftpLocalIP,          PAL_T_STR, sizeof sSettings.szTftpLocalIP },
    { KEY_SERVICES,             & sSettings.uServices,              PAL_T_INT, 0 },
    { KEY_TFTPLOGFILE,          & sSettings.szTftpLogFile,          PAL_T_STR, sizeof sSettings.szTftpLogFile },
    { KEY_LOWEST_PORT,          & sSettings.nTftpLowPort,           PAL_T_INT, 0 },
    { KEY_HIGHEST_PORT,         & sSettings.nTftpHighPort,          PAL_T_INT, 0 },
    { KEY_MAX_TRANSFERS,        & sSettings.dwMaxTftpTransfers,     PAL_T_INT, 0 },
    { KEY_REDUCE_PATH,          & sSettings.bReduceTFTPPath,        PAL_T_INT, 0 },
    { KEY_IGNORE_LASTBLOCK_ACK, & sSettings.bIgnoreLastBlockAck,    PAL_T_INT, 0 },
    { KEY_GUI_REMANENCE,        & sSettings.nGuiRemanence,          PAL_T_INT, 0 },
};

BOOL Tftpd32ReadSettings (void)
{
FILE *f;
char line [768];
    if (s_szCfgPath [0] == 0)  return TRUE;
    f = fopen (s_szCfgPath, "r");
    if (f == NULL)  return TRUE;      // 无配置 → 用默认值
    while (fgets (line, sizeof line, f) != NULL)
    {
        char *pEq = strchr (line, '=');
        int Ark;
        if (pEq == NULL)  continue;
        * pEq = 0;
        line [strcspn (line, "\r\n")] = 0;
        for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
            if (strcmp (tKeys [Ark].name, line) == 0)
            {
                if (tKeys [Ark].type == PAL_T_INT)
                    * (unsigned *) tKeys [Ark].p = (unsigned) strtoul (pEq + 1, NULL, 10);
                else
                    lstrcpyn_pal ((char *) tKeys [Ark].p, tKeys [Ark].size, pEq + 1);
                break;
            }
    }
    fclose (f);
return TRUE;
}

BOOL Tftpd32SaveSettings (void)
{
FILE *f;
int Ark;
    if (s_szCfgPath [0] == 0)  return FALSE;
    f = fopen (s_szCfgPath, "w");
    if (f == NULL)  return FALSE;
    for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    {
        if (tKeys [Ark].type == PAL_T_INT)
            fprintf (f, "%s=%u\n", tKeys [Ark].name, * (unsigned *) tKeys [Ark].p);
        else
            fprintf (f, "%s=%s\n", tKeys [Ark].name, (char *) tKeys [Ark].p);
    }
    fclose (f);
return TRUE;
}

BOOL Tftpd32DestroySettings (void)
{ if (s_szCfgPath [0])  remove (s_szCfgPath);  return TRUE; }

/* napi 桥按键名写入（值已校验） */
BOOL Pal_SetSettingInt (const char *key, unsigned val)
{ int Ark;  for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    if (strcmp (tKeys [Ark].name, key) == 0 && tKeys [Ark].type == PAL_T_INT)
    { * (unsigned *) tKeys [Ark].p = val;  return TRUE; }
return FALSE; }

BOOL Pal_SetSettingStr (const char *key, const char *val)
{ int Ark;  for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    if (strcmp (tKeys [Ark].name, key) == 0 && tKeys [Ark].type == PAL_T_STR)
    { lstrcpyn_pal ((char *) tKeys [Ark].p, tKeys [Ark].size, val);  return TRUE; }
return FALSE; }
```

- [ ] **Step 3.2: Commit**

```powershell
git add -A; git commit -m "feat(pal): settings storage with original registry key names"
```

---

### Task 4: console_pal.c / async_log_pal.c / ip_pal.c / eventlog_pal.c

**Files:**
- Create: `entry/src/main/cpp/tftpd/console_pal.c`、`async_log_pal.c`、`ip_pal.c`、`eventlog_pal.c`

- [ ] **Step 4.1: 写 console_pal.c（完整）**

替代 `to_console.c`：同签名 `SendMsgRequest`/`ReportNewTrf`，把消息转为 JSON 交由注册进来的发射器（napi_init 注入 TSF）。

```c
//////////////////////////////////////////////////////////
// console_pal.c —— 替代 to_console.c（GUI 消息层 → NAPI 事件）
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
            int Ark, n = p->nbTrf;  char one [128];
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
```

`S_Chg_Service` 结构体位于 `_hdr_dialog_socket.h`（原文件 L190 附近），已随拷贝件引入；若编译报未定义，打开该头文件确认字段名（`int service; int status;`）后按原样使用。

- [ ] **Step 4.2: 写 async_log_pal.c（完整，与原 async_log.c 同签名）**

```c
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
```

- [ ] **Step 4.3: 写 ip_pal.c 与 eventlog_pal.c（完整）**

```c
// ip_pal.c —— GetIPv4Address 等由 _services/ip_util.c 提供（依赖 IPHlpApi，不移植）
#include "headers.h"
int GetIPv4Address (const char *szIf, char *szIP) { szIP [0] = 0;  return -1; }
int PoolNetworkInterfaces (void) { return TRUE; }   /* Scheduler 未移植，桩即可 */
int AnswerIPList (void) { return TRUE; }
void SendDirectoryContent (void) {}
```

```c
// eventlog_pal.c —— 替代 _libs/event_log/eventlog.c
#include "headers.h"
int WriteIntoEventLog (const char *txt, WORD evId)
{ LogToMonitor ("EventLog[%d]: %s", evId, txt);  return 0; }
```

- [ ] **Step 4.4: Commit**

```powershell
git add -A; git commit -m "feat(pal): console/log/ip adapters forwarding to NAPI emitter"
```

---

### Task 5: service_main.c（TFTP 服务启停）

**Files:**
- Create: `entry/src/main/cpp/tftpd/service_main.c`

- [ ] **Step 5.1: 写 service_main.c（完整）**

替代 `start_threads.c` 的 TFTP 部分；`tThreads[]` 表与 `TH_TFTP` 沿用 `threading.h`（`-fcommon` 保证符号合一）。

```c
#include "headers.h"

static pthread_t s_hTftpThread;
static int       s_bThreadStarted = FALSE;

static void *TftpThreadTramp (void *param) { TftpdMain (param);  return NULL; }

int TftpdServiceStart (void)
{
    if (tThreads [TH_TFTP].gRunning)  return TRUE;
    if (sSettings.uServices & TFTPD32_TFTP_SERVER)
        sSettings.uServices |= TFTPD32_TFTP_SERVER;   /* 语义保持 */

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
```

在 `pal_compat.h` 末尾补两行声明（Task 2 文件同步更新）：

```c
int  TftpdServiceStart (void);
void TftpdServiceStop  (void);
```

- [ ] **Step 5.2: Commit**

```powershell
git add -A; git commit -m "feat(pal): TFTP service start/stop replaces start_threads.c"
```

---

### Task 6: tftpd_main.c 补丁（唯一修改的原生文件）

**Files:**
- Modify: `entry/src/main/cpp/tftpd/tftpd_main.c`

**改动原则（兼容性强）：只替换等待机制（WSAEventSelect → poll + 唤醒管道），协议逻辑零改动。**

- [ ] **Step 6.1: 删除 `ResetSockEvent` 函数（L516-523 整段）** —— 仅供旧事件机制使用。

- [ ] **Step 6.2: 用下述代码整体替换 `TftpdMain`（原 L529-665）**

```c
// ---------------------------------------------------------------
// Main（HarmonyOS PAL：poll(监听socket) + poll(唤醒管道) 等价替换
//       原 WSAEventSelect/WaitForMultipleObjects 双事件等待）
// ---------------------------------------------------------------
void TftpdMain (void *param)
{
int Rc;
int parse;
struct LL_TftpInfo *pTftp;
int nWakeFd = -1;
int dwRefreshInterval = sSettings.dwRefreshInterval;

    // creates socket and starts permanent threads
        if (pTftpFirst==NULL)  CreatePermanentThreads ();

    if (tThreads[TH_TFTP].hEv != INVALID_HANDLE_VALUE)
        nWakeFd = PalWakeFd (tThreads[TH_TFTP].hEv);

    tThreads [TH_TFTP].bInit = TRUE;  // inits OK

    while ( tThreads[TH_TFTP].gRunning  )
    {
        if (tThreads[TH_TFTP].skt == INVALID_SOCKET)
        {
            tThreads[TH_TFTP].skt = TftpBindLocalInterface ();
        }
        if (tThreads[TH_TFTP].skt == INVALID_SOCKET)
        {
            break;
        }

        { struct pollfd tPfd [2];
          tPfd [0].fd = tThreads[TH_TFTP].skt;   tPfd [0].events = POLLIN;  tPfd [0].revents = 0;
          tPfd [1].fd = nWakeFd;                 tPfd [1].events = POLLIN;  tPfd [1].revents = 0;
          Rc = poll (tPfd, nWakeFd >= 0 ? 2 : 1, dwRefreshInterval);
        }
        if (! tThreads[TH_TFTP].gRunning ) break;

        if (Rc == 0)      // 超时：上报统计（原 WAIT_TIMEOUT 分支）
        {
            SendStatsToGui (gSendFullStat);
            gSendFullStat = FALSE;
            if (dwRefreshInterval * 2 < sSettings.dwRefreshInterval)
                dwRefreshInterval *= 2;
            else  dwRefreshInterval = sSettings.dwRefreshInterval;
        }
        else
        {
            if (nWakeFd >= 0)
            {
                /* 唤醒管道可读：回收已结束 worker（原 E_TFTP_WAKE 分支）或停机 */
                char szDummy [16];
                struct pollfd pChk = { nWakeFd, POLLIN, 0 };
                poll (& pChk, 1, 0);
                if (pChk.revents & POLLIN)
                {
                    while (read (nWakeFd, szDummy, sizeof szDummy) > 0) ;
                    if (! tThreads[TH_TFTP].gRunning ) break;
                    do
                    {
                        parse = FALSE;
                        for ( pTftp=pTftpFirst ; pTftp!=NULL ; pTftp=pTftp->next )
                        {
                            if  (! pTftp->tm.bPermanentThread && ! pTftp->tm.bActive )
                            {
                                PalFreeThreadHandle (pTftp->tm.dwThreadHandle);
                                pTftp = TftpdDestroyThreadItem (pTftp);
                                parse = TRUE;
                                break;
                            }
                        }
                    }
                    while (parse);
                    continue;
                }
            }
            /* 监听 socket 可读：接受新传输（原 E_TFTP_SOCK 分支） */
            TftpdChooseNewThread (tThreads[TH_TFTP].skt);
            dwRefreshInterval = sSettings.dwRefreshInterval / 4 + 100;
        }
    } // while

    // 关停：与原版语义一致（bSoftReset 时不惊动 worker）
    if ( tThreads[TH_TFTP].bSoftReset )
        LogToMonitor ("do NOT signal worker threads\n");
    else
    {
        LogToMonitor ("signalling worker threads\n");
        for (pTftp=pTftpFirst ; pTftp!=NULL  ; pTftp=pTftp->next)
        {
            if (pTftp->tm.bActive)                nak (pTftp, ECANCELLED);
            else if (pTftp->tm.bPermanentThread)  SetEvent (pTftp->tm.hEvent);
        }
        LogToMonitor ("waiting for worker threads\n");
        while ( pTftpFirst != NULL )
        {
            WaitForSingleObject (pTftpFirst->tm.dwThreadHandle, 10000);
            LogToMonitor ("End of thread %d\n", pTftpFirst->tm.dwThreadHandleId);
            pTftpFirst->tm.bPermanentThread = FALSE;
            TftpdDestroyThreadItem (pTftpFirst);
        }
    }

    closesocket (tThreads[TH_TFTP].skt);
    tThreads[TH_TFTP].skt = INVALID_SOCKET;
    LogToMonitor ("main TFTP thread ends here\n");
    _endthread ();
} // Tftpd main thread
```

同时在该文件顶部（`#define min` 之后）加：

```c
#include <poll.h>
#include <unistd.h>
```

- [ ] **Step 6.3: `_endthread` 兼容**

`_endthread()` 在 worker 线程（tftpd_thread.c L1139）与 TftpdMain 中出现。在 `pal_compat.h` 补：

```c
#include <pthread.h>
#define _endthread() pthread_exit(NULL)
```

- [ ] **Step 6.4: 编译修复循环**

```powershell
hvigorw --mode module -p module=entry@default -p product=default assembleHap 2>&1 | Select-String "error"
```

按错误逐个修（允许的修法仅限 pal_compat.h 补宏/typedef，**不得改协议逻辑**）。预期余量错误与修法：
- `INVALID_HANDLE_VALUE` 赋给 `hFile` 比较告警 → 已由 tag 体系覆盖；
- `GetFileSizeEx` 参数类型 → 已定义；
- 若报 `BinDump`/`scandir` 未定义 → 未被引用（DEBUG 宏未开），忽略链接即可；若真报链接错误，把 `reference/tftpd64/src/_libs/dump/dump.c` 拷入并加入 CMake。

Expected: BUILD SUCCESSFUL。

- [ ] **Step 6.5: Commit**

```powershell
git add -A; git commit -m "feat(tftpd): poll-based main loop, WSAEventSelect removed, protocol untouched"
```

---

### Task 7: napi_init.cpp 完整桥

**Files:**
- Modify: `entry/src/main/cpp/napi_init.cpp`（替换 Task 1 临时版）

- [ ] **Step 7.1: 写完整 napi_init.cpp**

```cpp
#include <cstring>
#include <cstdlib>
#include "napi/native_api.h"

extern "C" {
// ---- 由 PAL 提供（settings_pal.c / service_main.c / console_pal.c）----
void   Pal_SetEmitter     (void (*fn)(const char *, const char *));
void   Pal_SetConfigPath  (const char *szPath);
int    TftpdServiceStart  (void);
void   TftpdServiceStop   (void);
BOOL   Tftpd32ReadSettings (void);
BOOL   Tftpd32SaveSettings (void);
BOOL   Pal_SetSettingInt  (const char *key, unsigned val);
BOOL   Pal_SetSettingStr  (const char *key, const char *val);
}

// ---------- 线程安全发射器 ----------
static napi_threadsafe_function g_tsf = nullptr;

struct EmitMsg { char type[16]; char json[1024]; };

static void EmitterTramp (const char *szType, const char *szJson)
{
    if (g_tsf == nullptr)  return;
    EmitMsg *m = new EmitMsg ();
    strncpy (m->type, szType, sizeof m->type - 1);
    strncpy (m->json, szJson, sizeof m->json - 1);
    napi_call_threadsafe_function (g_tsf, m, napi_tsfn_nonblocking);
}

static void CallJs (napi_env env, napi_value jsCb, void *ctx, void *data)
{
    EmitMsg *m = (EmitMsg *) data;
    if (m != nullptr)
    {
        napi_value undef, argv[2];
        napi_get_undefined (env, & undef);
        napi_create_string_utf8 (env, m->type, NAPI_AUTO_LENGTH, & argv[0]);
        napi_create_string_utf8 (env, m->json, NAPI_AUTO_LENGTH, & argv[1]);
        napi_call_function (env, undef, jsCb, 2, argv, nullptr);
        delete m;
    }
}

// ---------- 导出函数 ----------
static napi_value SetEventCb (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    if (g_tsf != nullptr)  napi_release_threadsafe_function (g_tsf, napi_tsfn_abort);
    napi_value name;
    napi_create_string_utf8 (env, "tftpdEvent", NAPI_AUTO_LENGTH, & name);
    napi_create_threadsafe_function (env, argv[0], nullptr, name, 0, 1, nullptr,
                                     nullptr, nullptr, CallJs, & g_tsf);
    Pal_SetEmitter (EmitterTramp);
    return nullptr;
}

static napi_value SetConfigPath (napi_env env, napi_callback_info info)
{
    size_t argc = 1;  napi_value argv[1];  char buf[512];  size_t n;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], buf, sizeof buf, & n);
    Pal_SetConfigPath (buf);
    return nullptr;
}

static napi_value StartService (napi_env env, napi_callback_info info)
{
    napi_value result;
    Tftpd32ReadSettings ();
    napi_get_boolean (env, TftpdServiceStart () ? true : false, & result);
    return result;
}

static napi_value StopService (napi_env env, napi_callback_info info)
{
    napi_value result;
    TftpdServiceStop ();
    napi_get_boolean (env, true, & result);
    return result;
}

static napi_value SetSettingInt (napi_env env, napi_callback_info info)
{
    size_t argc = 2;  napi_value argv[2];  char key[64];  size_t n;  double v;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], key, sizeof key, & n);
    napi_get_value_double (env, argv[1], & v);
    napi_value r;  napi_get_boolean (env, Pal_SetSettingInt (key, (unsigned) v), & r);
    return r;
}

static napi_value SetSettingStr (napi_env env, napi_callback_info info)
{
    size_t argc = 2;  napi_value argv[2];  char key[64], val[512];  size_t n1, n2;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], key, sizeof key, & n1);
    napi_get_value_string_utf8 (env, argv[1], val, sizeof val, & n2);
    napi_value r;  napi_get_boolean (env, Pal_SetSettingStr (key, val), & r);
    return r;
}

static napi_value SaveSettings (napi_env env, napi_callback_info info)
{
    napi_value r;  napi_get_boolean (env, Tftpd32SaveSettings () ? true : false, & r);
    return r;
}

EXTERN_C_START
static napi_value Init (napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] =
    {
        { "setEventCallback", nullptr, SetEventCb,    nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setConfigPath",    nullptr, SetConfigPath, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "startService",     nullptr, StartService,  nullptr, nullptr, nullptr, napi_default, nullptr },
        { "stopService",      nullptr, StopService,   nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setSettingInt",    nullptr, SetSettingInt, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "setSettingStr",    nullptr, SetSettingStr, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "saveSettings",     nullptr, SaveSettings,  nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties (env, exports, sizeof (desc) / sizeof (desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module g_module =
{ .nm_version = 1, .nm_flags = 0, .nm_filename = nullptr, .nm_register_func = Init,
  .nm_modname = "tftpd32", .nm_priv = nullptr, .reserved = { 0 } };

extern "C" __attribute__((constructor)) void RegisterTftpd32Module (void)
{ napi_module_register (& g_module); }
```

- [ ] **Step 7.2: 构建验证**

```powershell
hvigorw --mode module -p module=entry@default -p product=default assembleHap
```

Expected: BUILD SUCCESSFUL。

- [ ] **Step 7.3: Commit**

```powershell
git add -A; git commit -m "feat(napi): thread-safe event emitter + settings/service bridges"
```

---

### Task 8: ArkTS 服务封装

**Files:**
- Create: `entry/src/main/ets/services/TftpdService.ets`

- [ ] **Step 8.1: 写 TftpdService.ets（完整）**

```typescript
import tftpd from 'libtftpd32.so';
import { preferences } from '@kit.ArkData';

export interface TrfStat {
  id: number;
  bytes: number;
  size: number;
  ret: number;
  dur?: number;
}

export type TftpdEvent =
  | { type: 'log'; text: string }
  | { type: 'trf' | 'stat'; trfs: TrfStat[] }
  | { type: 'service' | 'started'; raw: string };

const CFG_KEY = 'tftpd32_cfg_path';

export class TftpdService {
  private static ctx: Context | null = null;
  private static cfgPath: string = '';
  private static store: preferences.Preferences | null = null;
  private static listeners: ((e: TftpdEvent) => void)[] = [];
  static readonly logs: string[] = [];
  static readonly transfers: TrfStat[] = [];

  static async init(ctx: Context): Promise<void> {
    TftpdService.cfgPath = `${ctx.filesDir}/tftpd32.ini`;
    // TFTP 根目录：沙箱内固定目录（对齐原版开箱即用；用户选目录授权放里程碑3）
    TftpdService.store = await preferences.getPreferences(ctx, CFG_KEY);
    await TftpdService.store.put(CFG_KEY, TftpdService.cfgPath);
    await TftpdService.store.flush();
    tftpd.setConfigPath(TftpdService.cfgPath);
    tftpd.setEventCallback((type: string, json: string) => TftpdService.onEvent(type, json));
  }

  private static onEvent(type: string, json: string): void {
    const payload = json;
    if (type === 'log') {
      TftpdService.logs.push(payload);
      if (TftpdService.logs.length > 500) TftpdService.logs.shift();
    } else if (type === 'trf' || type === 'stat') {
      try {
        const obj = JSON.parse(payload) as Record<string, Object>;
        const list = (obj['t'] ?? []) as TrfStat[];
        TftpdService.transfers.splice(0, TftpdService.transfers.length, ...list);
      } catch (_e) { /* 忽略坏帧 */ }
    }
    TftpdService.listeners.forEach(fn => fn(TftpdService.parseEvent(type, payload)));
  }

  private static parseEvent(type: string, payload: string): TftpdEvent {
    if (type === 'log') return { type: 'log', text: payload };
    if (type === 'trf' || type === 'stat') return { type, trfs: TftpdService.transfers };
    return { type: 'service', raw: payload };
  }

  static subscribe(fn: (e: TftpdEvent) => void): void {
    TftpdService.listeners.push(fn);
  }

  static async setInt(key: string, val: number): Promise<void> {
    tftpd.setSettingInt(key, val);
  }

  static async setStr(key: string, val: string): Promise<void> {
    tftpd.setSettingStr(key, val);
  }

  static async save(): Promise<boolean> {
    return tftpd.saveSettings();
  }

  static async start(): Promise<boolean> {
    return tftpd.startService();
  }

  static stop(): void {
    tftpd.stopService();
  }
}
```

（native 侧 `CallJs` 以 `(type, json)` 两参数回调，与 `onEvent(type, json)` 一致。）

- [ ] **Step 8.2: Commit**

```powershell
git add -A; git commit -m "feat(arkts): TftpdService wrapper with sandbox config path"
```

---

### Task 9: ArkUI 三页签 + 权限

**Files:**
- Modify: `entry/src/main/ets/pages/Index.ets`（整体替换模板）
- Modify: `entry/src/main/module.json5`

- [ ] **Step 9.1: 写 Index.ets（完整；忠实复刻原版：紧凑、无装饰）**

```typescript
import { TftpdService, TrfStat } from '../services/TftpdService';

const KEY_PORT = 'TftpPort';
const KEY_BASEDIR = 'BaseDirectory';
const KEY_SECURITY = 'SecurityLevel';
const KEY_TIMEOUT = 'Timeout';
const KEY_RETRANSMIT = 'MaxRetransmit';
const KEY_NEGOCIATE = 'Negociate';
const KEY_WINSIZE = 'WinSize';

@Entry
@Component
struct Index {
  @State port: string = '6900';
  @State baseDir: string = '';
  @State security: number = 1;      // SECURITY_STD，与原版一致
  @State timeout: string = '3';
  @State retransmit: string = '6';
  @State negociate: boolean = true;
  @State running: boolean = false;
  @State logs: string[] = [];
  @State transfers: TrfStat[] = [];
  private logScroller: Scroller = new Scroller();

  aboutToAppear(): void {
    TftpdService.init(getContext(this));
    this.baseDir = `${getContext(this).filesDir}/tftpboot`;
    TftpdService.subscribe(() => {
      this.logs = [...TftpdService.logs];
      this.transfers = [...TftpdService.transfers];
      this.logScroller.scrollEdge(Edge.Bottom);
    });
  }

  build() {
    Column() {
      Text('Tftpd32 for HarmonyOS').fontSize(16).fontWeight(FontWeight.Bold).margin(8)
      Tabs() {
        TabContent() { this.SettingsPage() }.tabBar('设置')
        TabContent() { this.TftpPage() }.tabBar('TFTP')
        TabContent() { this.LogPage() }.tabBar('日志')
      }.layoutWeight(1)
    }.height('100%')
  }

  @Builder
  SettingsPage() {
    Column({ space: 8 }) {
      Row() {
        Text('服务').width(80)
        Toggle({ type: ToggleType.Switch, isOn: this.running })
          .onChange((on: boolean) => this.toggleService(on))
        Text(this.running ? '运行中' : '已停止').margin({ left: 8 })
      }.width('100%')

      Row() {
        Text('端口').width(80)
        TextInput({ text: this.port }).width(120).type(InputType.Number)
          .onChange(v => { this.port = v; TftpdService.setInt(KEY_PORT, parseInt(v) || 6900) })
        Text('（默认 6900，应用无法绑定 69 等特权端口）').fontSize(12).opacity(0.6)
      }.width('100%')

      Row() {
        Text('根目录').width(80)
        Text(this.baseDir).fontSize(12).layoutWeight(1).maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
      }.width('100%')

      Row() {
        Text('安全级别').width(80)
        Select([{ value: '无' }, { value: '标准' }, { value: '高' }, { value: '只读' }])
          .selected(this.security).value(this.securityText())
          .onSelect((i: number) => { this.security = i; TftpdService.setInt(KEY_SECURITY, i) })
      }.width('100%')

      Row() {
        Text('超时(秒)').width(80)
        TextInput({ text: this.timeout }).width(80).type(InputType.Number)
          .onChange(v => { this.timeout = v; TftpdService.setInt(KEY_TIMEOUT, parseInt(v) || 3) })
        Text('重传次数').width(80).margin({ left: 12 })
        TextInput({ text: this.retransmit }).width(80).type(InputType.Number)
          .onChange(v => { this.retransmit = v; TftpdService.setInt(KEY_RETRANSMIT, parseInt(v) || 6) })
      }.width('100%')

      Row() {
        Text('选项协商').width(80)
        Toggle({ type: ToggleType.Switch, isOn: this.negociate })
          .onChange(on => { this.negociate = on; TftpdService.setInt(KEY_NEGOCIATE, on ? 1 : 0) })
        Text('RFC2347/2348/2349').fontSize(12).opacity(0.6).margin({ left: 8 })
      }.width('100%')

      Blank()
      Button('保存配置').width('100%')
        .onClick(() => TftpdService.save().then(ok => {
          this.logs.push(ok ? '配置已保存' : '保存失败');
          this.logs = [...this.logs];
        }))
    }.padding(12).alignItems(HorizontalAlign.Start)
  }

  private securityText(): string {
    return ['无', '标准', '高', '只读'][this.security] ?? '标准';
  }

  private async toggleService(on: boolean): Promise<void> {
    await TftpdService.setInt(KEY_PORT, parseInt(this.port) || 6900);
    await TftpdService.setInt(KEY_SECURITY, this.security);
    await TftpdService.setInt(KEY_TIMEOUT, parseInt(this.timeout) || 3);
    await TftpdService.setInt(KEY_RETRANSMIT, parseInt(this.retransmit) || 6);
    await TftpdService.setStr(KEY_BASEDIR, this.baseDir);
    await TftpdService.save();
    if (on) {
      this.running = await TftpdService.start();
    } else {
      TftpdService.stop();
      this.running = false;
    }
  }

  @Builder
  TftpPage() {
    Column() {
      if (this.transfers.length === 0) {
        Text('当前无传输').margin(20).opacity(0.5)
      } else {
        List({ space: 4 }) {
          ForEach(this.transfers, (t: TrfStat) => {
            ListItem() {
              Column({ space: 2 }) {
                Text(`传输 #${t.id}   ${t.bytes}/${t.size} 字节   ${t.dur ?? 0}s`)
                  .fontSize(13)
                Progress({ value: t.size > 0 ? Number(BigInt(t.bytes) * 100n / BigInt(t.size)) : 0, total: 100 })
                  .width('100%')
              }.padding(6)
            }
          }, (t: TrfStat) => `${t.id}_${t.bytes}`)
        }.layoutWeight(1)
      }
    }.padding(8)
  }

  @Builder
  LogPage() {
    List({ space: 2, scroller: this.logScroller }) {
      ForEach(this.logs, (l: string, i: number) => {
        ListItem() { Text(l).fontSize(12).fontFamily('monospace') }
      }, (l: string, i: number) => `${i}_${l}`)
    }.padding(8).layoutWeight(1)
  }
}
```

- [ ] **Step 9.2: module.json5 加权限**

在 `module.json5` 的 `module` 下追加：

```json5
"requestPermissions": [
  { "name": "ohos.permission.INTERNET" },
  { "name": "ohos.permission.GET_NETWORK_INFO" }
],
```

- [ ] **Step 9.3: 构建验证 + Commit**

```powershell
hvigorw --mode module -p module=entry@default -p product=default assembleHap
git add -A; git commit -m "feat(ui): Settings/TFTP/Log tabs, faithful compact layout"
```

---

### Task 10: 真机端到端验证（与原生对拍）

- [ ] **Step 10.1: 部署并启动**

DevEco 连接真机（PC/2in1 优先）→ Run。UI 打开 → 设置页 Toggle 开启服务。预期日志：`[1] Connection received from ...` 仅在有客户端连接时出现；无 bind 错误。

- [ ] **Step 10.2: 查询设备 IP**

```powershell
hdc shell ifconfig
```

记录 wlan0/eth0 的 inet 地址。

- [ ] **Step 10.3: PC 端 curl 对拍（同一局域网）**

```powershell
"hello tftpd32 on harmony" | Set-Content test.bin
# 上传（写）
curl -T test.bin tftp://<设备IP>:6900/test.bin
# 下载（读）并比对
curl -o got.bin tftp://<设备IP>:6900/test.bin
fc.exe test.bin got.bin
```

Expected: `fc` 输出“无差异”；UI TFTP 页出现传输条目；日志页出现 `Connection received from <IP> on port ...`。

- [ ] **Step 10.4: blksize 协商对拍**

```powershell
curl --tftp-blksize 16384 -o big.bin tftp://<设备IP>:6900/test.bin
curl --tftp-blksize 8 -o tiny.bin tftp://<设备IP>:6900/test.bin
```

Expected: 均成功（OACK 协商生效，与原生 Tftpd32 行为一致）。

- [ ] **Step 10.5: 安全级别对拍**

设置页安全级别切“只读”→ 重启服务 → 重跑 Step 10.3 上传。Expected: 上传失败（EACCESS），下载成功 —— 与原生 Read-only 行为一致。

- [ ] **Step 10.6: Commit（如有修复）**

```powershell
git add -A; git commit -m "fix: milestone1 interop issues found during curl testing"
```

---

### Task 11: 长时任务（后台存活）

**Files:**
- Modify: `entry/src/main/ets/pages/Index.ets`
- Modify: `entry/src/main/module.json5`

- [ ] **Step 11.1: module.json5 ability 内声明后台模式**

```json5
"backgroundModes": ["taskKeeping"],
```

- [ ] **Step 11.2: Index.ets toggleService 内接入 continuous task**

```typescript
import { backgroundTaskManager } from '@kit.BackgroundTaskKit';
import { wantAgent } from '@kit.AbilityKit';

// toggleService 中：
if (on) {
  try {
    const wantAgentObj = await wantAgent.getWantAgent({
      wantAgentInfo: {
        bundleName: 'com.example.tftpd32',   // 以 AppScope/app.json5 实际 bundleName 为准
        abilityName: 'EntryAbility'
      } as wantAgent.WantAgentInfo
    });
    const req: backgroundTaskManager.ContinuousTaskRequest = {
      wantAgent: wantAgentObj,
      backgroundModes: [backgroundTaskManager.BackgroundMode.TASK_KEEPING]
    } as backgroundTaskManager.ContinuousTaskRequest;
    await backgroundTaskManager.startContinuousTask(req);
  } catch (e) {
    // 手机端不支持 taskKeeping 时降级：仅前台可用，日志提示
    this.logs.push('提示：此设备不支持后台常驻，退后台后服务可能中断');
    this.logs = [...this.logs];
  }
  this.running = await TftpdService.start();
} else {
  TftpdService.stop();
  try { await backgroundTaskManager.stopContinuousTask(); } catch (_e) { /* 未申请过 */ }
  this.running = false;
}
```

- [ ] **Step 11.3: 验证 + Commit**

真机开启服务 → Home 退后台 5 分钟 → PC 侧 curl 仍可传输。Expected: 通过。

```powershell
git add -A; git commit -m "feat(bg): continuous task keeps TFTP service alive"
```

---

## 验收清单（对照适配方案 §1.2 TFTP 部分）

| 项 | 验证方式 | 状态 |
|---|---|---|
| RRQ/WRQ 基本传输 | Task 10.3 curl 双向 | ☐ |
| tsize/blksize/timeout 协商 | Task 10.4 | ☐ |
| 4 档安全级别 | Task 10.5 + 手工切档 | ☐ |
| >4GB/64 位长度 | curl 上传 ≥100MB 验证路径即可，4GB 用 dd 生成（可选） | ☐ |
| 并发传输上限 | 5 个并行 curl | ☐ |
| UI 三页签复刻 | 人工核对 | ☐ |
| 后台存活 | Task 11.3 | ☐ |
| 键名兼容 | tftpd32.ini 内容与原版 KEY_* 一致 | ☐ |

## 风险与红线

- **不得改动** `tftpd_thread.c` 协议逻辑（兼容性强原则）；平台差异只允许落在 pal_compat 与 console_pal。
- `tftpd32.ini` 键名即原版注册表键名，未来可从原版导出导入。
- 若编译错误超出 Task 6.4 列表，优先在 pal_compat 补宏，其次回读对应原生文件确认语义，最后才考虑最小改动拷贝件（需在 commit message 说明理由）。
