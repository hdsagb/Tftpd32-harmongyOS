# Tftpd32 鸿蒙适配 —— 里程碑2：客户端与辅助服务 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 M1 的 TFTP 服务器之上，补齐 Tftpd32 v4.74 的 TFTP 客户端、Syslog 服务器、SNTP 服务器、DNS 服务器、MD5 工具与目录列表，并把 ArkUI 界面从 3 页签扩展到原版的 8 页签，行为与原版逐项对齐。

**Architecture:** 服务线程沿用原版 `tThreads[]` 表与 `TftpdMain/SyslogProc/SntpdProc/ListenDNSMessage` 函数签名；`service_main.c` 升级为表驱动的通用服务管理器（每个服务独立 UDP socket + 独立 pthread，端口可配置）。`syslogd.c`/`sntpd.c`/`dns.c` 原样拷贝后仅打 PAL 补丁。TFTP 客户端在原版是 Win32 消息驱动（WSAAsyncSelect + WM_TIMER），**必须改写**为 `poll()` + 状态机的一次性工作线程（`tftp_client.c`），协议顺序与超时重传策略保持原版一致。所有界面事件经 `console_pal.c` 的 `Emit()` 以 JSON 帧上报 ArkTS。

**Tech Stack:** DevEco Studio 5 / HarmonyOS 6.1.1(API 24)，NDK + CMake + Node-API，ArkTS/ArkUI，musl libc。

**基准源码:** `reference/tftpd64/src/_services/{syslogd.c,sntpd.c,dns.c}`、`_gui/tftp_cli.c`

**既有约束（必须遵守）:**
- 管道事件必须 `O_NONBLOCK`（否则 TFTP 监听线程死锁，见 BUG-001）。
- 格式化 `long long` 一律 `%lld`，禁用 `%I64d`。
- 鸿蒙不可绑 <1024 端口：TFTP 6900（M1 已完成），Syslog 5140、SNTP 1230、DNS 5300（新增默认值，可配置）。
- 配置键名沿用原版；新增端口键是本适配的扩展键，需在文档 §2.6 说明。

---

## 文件结构（本计划产出的文件）

```
entry/src/main/cpp/
├── CMakeLists.txt                        【修改】加入 M2 源文件
├── napi_init.cpp                         【修改】新增 7 个导出
├── tftpd/
│   ├── pal_compat.h / pal_compat.c       【修改】SYSTEMTIME/GetSystemTime/GetTickCount/
│   │                                              SetFilePointer/IN6_ADDR/LPSOCKADDR/
│   │                                              __isascii/PalEmitEvent/PalJsonEscape
│   ├── _hdr_settings.h                   【修改】三个端口字段 + 三个 KEY 宏
│   ├── settings_pal.c                    【修改】登记新键 + 默认值 + GetSettingStr
│   ├── console_pal.c                     【修改】C_SYSLOG / C_DNS_NEW_ENTRY / C_TFTP_TRF_END 扩展
│   ├── service_main.c                    【重写】表驱动服务管理器（TFTP/SYSLOG/SNTP/DNS）
│   ├── syslogd.c                         【拷贝+补丁】仅去掉 Windows 命名管道
│   ├── sntpd.c                           【拷贝+补丁】去掉 <process.h> 与 u_int*_t 重定义
│   ├── dns.c                             【拷贝+补丁】ProcessDnsQuery 先查 hosts.txt
│   ├── dns_hosts.c                       【新建】hosts.txt 解析
│   ├── tftp_client.c                     【新建】poll 状态机客户端（替代 _gui/tftp_cli.c）
│   ├── file_tool_pal.c                   【新建】PalComputeMd5 / PalListDirectory
│   └── headers.h                         【修改】聚合新头文件
├── types/libtftpd32/Index.d.ts           【修改】新增导出声明
entry/src/main/ets/
├── services/TftpdService.ets             【修改】新增事件类型、客户端/目录/MD5 API
└── pages/
    ├── Index.ets                         【修改】8 页签
    └── M2Pages.ets                       【新建】Syslog/SNTP/DNS/Client/Directory 五个 @Component
docs/Tftpd32-功能测试方案.md                【修改】追加 §6.5~§6.9 执行记录
tools/m2tool.c                            【新建】设备端协议测试工具
```

---

### Task 1: PAL 扩展与构建接线

**Files:**
- Modify: `entry/src/main/cpp/tftpd/pal_compat.h`
- Modify: `entry/src/main/cpp/tftpd/pal_compat.c`
- Modify: `entry/src/main/cpp/CMakeLists.txt`

- [ ] **Step 1.1: pal_compat.h 增加时间/类型/事件声明**

在 `#include <time.h>` 之后追加 `#include <dirent.h>`。在文件末尾（`#ifdef __cplusplus }` 之前）追加：

```c
/* ---------- SYSTEMTIME（sntpd.c 使用） ---------- */
typedef struct
{
    WORD  wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;

void  GetSystemTime (SYSTEMTIME *pst);
DWORD GetTickCount (void);
DWORD SetFilePointer (HANDLE h, LONG dist, LONG *pHigh, DWORD method);

/* ---------- 套接字别名（syslogd.c / dns.c 原样使用） ---------- */
typedef struct in6_addr IN6_ADDR;
typedef struct sockaddr * LPSOCKADDR;

#ifndef FILE_ATTRIBUTE_ARCHIVE
#define FILE_ATTRIBUTE_ARCHIVE  0x20
#endif
#ifndef FILE_FLAG_SEQUENTIAL_SCAN
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#endif
#ifndef OPEN_ALWAYS
#define OPEN_ALWAYS 4
#endif
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
```

- [ ] **Step 1.2: pal_compat.c 实现**

```c
void GetSystemTime (SYSTEMTIME *pst)
{
struct timespec ts;
struct tm tmv;

    clock_gettime (CLOCK_REALTIME, & ts);
    gmtime_r (& ts.tv_sec, & tmv);
    pst->wYear         = (WORD)(tmv.tm_year + 1900);
    pst->wMonth        = (WORD)(tmv.tm_mon + 1);
    pst->wDayOfWeek    = (WORD) tmv.tm_wday;
    pst->wDay          = (WORD) tmv.tm_mday;
    pst->wHour         = (WORD) tmv.tm_hour;
    pst->wMinute       = (WORD) tmv.tm_min;
    pst->wSecond       = (WORD) tmv.tm_sec;
    pst->wMilliseconds = (WORD)(ts.tv_nsec / 1000000);
}

DWORD GetTickCount (void)
{
struct timespec ts;

    clock_gettime (CLOCK_MONOTONIC, & ts);
    return (DWORD)((long long) ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

DWORD SetFilePointer (HANDLE h, LONG dist, LONG *pHigh, DWORD method)
{
off_t rc;

    rc = lseek (h->fd, (off_t) dist, (int) method);
    if (pHigh != NULL)  * pHigh = 0;
    return (DWORD) rc;
}
```

- [ ] **Step 1.3: CMakeLists.txt 纳入新源文件**

在 `add_library(tftpd32 SHARED ...)` 的源文件列表中加入：

```cmake
    tftpd/syslogd.c tftpd/sntpd.c tftpd/dns.c tftpd/dns_hosts.c
    tftpd/tftp_client.c tftpd/file_tool_pal.c
```

- [ ] **Step 1.4: 编译验证（此时新文件尚未创建，先只验证 PAL 改动）**

```powershell
$env:PATH="D:\Program Files\Huawei\DevEco Studio\jbr\bin;$env:PATH"
$env:DEVECO_SDK_HOME="D:\Program Files\Huawei\DevEco Studio\sdk"
& "D:\Program Files\Huawei\DevEco Studio\tools\node\node.exe" "D:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.js" --mode module -p module=entry@default -p product=default assembleHap
```

Expected: `BUILD SUCCESSFUL`（若已提前加 CMake 源文件则先跳过本步，Task 3 统一验证）。

- [ ] **Step 1.5: Commit**

```powershell
git add entry/src/main/cpp/tftpd/pal_compat.h entry/src/main/cpp/tftpd/pal_compat.c entry/src/main/cpp/CMakeLists.txt
git commit -m "feat(pal): SYSTEMTIME/tick/seek and M2 service declarations"
```

---

### Task 2: 配置键、端口与默认值

**Files:**
- Modify: `entry/src/main/cpp/tftpd/_hdr_settings.h`
- Modify: `entry/src/main/cpp/tftpd/settings_pal.c`

- [ ] **Step 2.1: 结构体字段（放在 `unsigned Port;` 之后）**

```c
    unsigned              SyslogPort;
    unsigned              SntpPort;
    unsigned              DnsPort;
```

- [ ] **Step 2.2: KEY 宏（放在 `KEY_SYSLOGPIPE` 之后）**

```c
#define KEY_SYSLOG_PORT            "SyslogPort"
#define KEY_SNTP_PORT              "SntpPort"
#define KEY_DNS_PORT               "DnsPort"
```

说明：这三个键是鸿蒙适配的扩展键（原版这三项端口固定 514/123/53 且不可配），在 `docs/Tftpd32-HarmonyOS适配方案.md` §2.6 增补一句"新增 SyslogPort/SntpPort/DnsPort 扩展键，缺省 5140/1230/5300"。

- [ ] **Step 2.3: settings_pal.c 登记与默认值**

在 `tKeys[]` 表末尾、`{ NULL, ... }` 哨兵行之前，按现有条目的字段顺序追加（Int 类型条目）：

```c
    { KEY_SYSLOG_PORT, & sSettings.SyslogPort, 1 },
    { KEY_SNTP_PORT,   & sSettings.SntpPort,   1 },
    { KEY_DNS_PORT,    & sSettings.DnsPort,    1 },
```

在加载配置的函数（`Tftpd32ReadSettings` 的 PAL 实现）读完文件后追加：

```c
    if (sSettings.SyslogPort == 0)  sSettings.SyslogPort = 5140;
    if (sSettings.SntpPort   == 0)  sSettings.SntpPort   = 1230;
    if (sSettings.DnsPort    == 0)  sSettings.DnsPort    = 5300;
```

新增读取字符串的导出函数（`napi_init.cpp` Task 9 会调用）：

```c
BOOL Pal_GetSettingStr (const char *szKey, char *szOut, int nOut)
{
int Ark;

    szOut[0] = 0;
    for (Ark = 0; tKeys[Ark].szKey != NULL; Ark++)
        if (lstrcmpi ((char *) tKeys[Ark].szKey, (char *) szKey) == 0)
        {
            if (tKeys[Ark].pStr == NULL)  return FALSE;
            lstrcpyn (szOut, tKeys[Ark].pStr, nOut);
            return TRUE;
        }
    return FALSE;
}
```

> 若 `tKeys[]` 现有条目使用联合体或函数指针而非 `pStr`，按同表既有写法取字符串字段，保持函数语义不变。

- [ ] **Step 2.4: 提交**

```powershell
git add entry/src/main/cpp/tftpd/_hdr_settings.h entry/src/main/cpp/tftpd/settings_pal.c
git commit -m "feat(settings): configurable Syslog/SNTP/DNS ports with defaults"
```

---

### Task 3: 表驱动服务管理器（service_main.c）

**Files:**
- Rewrite: `entry/src/main/cpp/tftpd/service_main.c`

- [ ] **Step 3.1: 完整实现**

```c
//////////////////////////////////////////////////////////
// service_main.c —— 替代原版 start_threads.c 的鸿蒙实现
// 每个服务：独立 UDP socket + 独立 pthread；开关由 sSettings.uServices 位控制
// 位定义与原版 custom.h 一致：
//   0x0001 TFTP  0x0002 TFTP客户端  0x0008 Syslog  0x0010 SNTP  0x0020 DNS
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
    { TH_TFTP,   SERVICE_TFTP,  TftpdMain,         & sSettings.Port,       "TFTP"   },
    { TH_SYSLOG, SERVICE_SYSLOG, SyslogProc,       & sSettings.SyslogPort, "Syslog" },
    { TH_SNTP,   SERVICE_SNTP,   SntpdProc,        & sSettings.SntpPort,   "SNTP"   },
    { TH_DNS,    SERVICE_DNS,    ListenDNSMessage, & sSettings.DnsPort,    "DNS"    },
};

static int PalSvcFind (unsigned uMask)
{
int Ark;

    for (Ark = 0; Ark < SizeOfTab (sServices); Ark++)
        if (sServices[Ark].uMask == uMask)  return Ark;
    return -1;
}

static int PalSvcStartOne (int iSvc)
{
struct S_PalService *p = & sServices[iSvc];
SOCKET s;
struct sockaddr_in addr;
int Rc;

    if (tThreads[p->nThread].gRunning)  return TRUE;      // 已在运行

    memset (& addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_ANY);
    addr.sin_port        = htons ((unsigned short) * p->pPort);

    s = socket (AF_INET, SOCK_DGRAM, 0);
    if (s == INVALID_SOCKET)
    {
        LogToMonitor ("%s: socket() failed errno=%d\n", p->szName, errno);
        return FALSE;
    }

    {
        int nOpt = 1;
        setsockopt (s, SOL_SOCKET, SO_REUSEADDR, & nOpt, sizeof nOpt);
    }

    Rc = bind (s, (struct sockaddr *) & addr, sizeof addr);
    if (Rc != 0)
    {
        LogToMonitor ("%s: bind port %u failed errno=%d\n", p->szName, * p->pPort, errno);
        closesocket (s);
        return FALSE;
    }

    tThreads[p->nThread].skt     = s;
    tThreads[p->nThread].bInit   = FALSE;
    tThreads[p->nThread].gRunning = TRUE;
    tThreads[p->nThread].hEv     = INVALID_HANDLE_VALUE;

    /* 仅 TFTP 需要唤醒事件管道；Syslog/SNTP/DNS 阻塞在 recvfrom */
    if (p->uMask == SERVICE_TFTP)
        tThreads[p->nThread].hEv = PalCreatePipeEvent ();

    if (pthread_create (& tThreads[p->nThread].hThread, NULL, (void *(*)(void *)) p->pfnProc, NULL) != 0)
    {
        LogToMonitor ("%s: pthread_create failed\n", p->szName);
        tThreads[p->nThread].gRunning = FALSE;
        closesocket (s);
        tThreads[p->nThread].skt = INVALID_SOCKET;
        return FALSE;
    }

    LogToMonitor ("%s service started on port %u\n", p->szName, * p->pPort);
    return TRUE;
}

static void PalSvcStopOne (int iSvc)
{
struct S_PalService *p = & sServices[iSvc];
SOCKET s;

    if (! tThreads[p->nThread].gRunning)  return;
    tThreads[p->nThread].gRunning = FALSE;

    s = tThreads[p->nThread].skt;
    if (s != INVALID_SOCKET)
        shutdown (s, SHUT_RDWR);        /* 唤醒阻塞的 recvfrom */

    if (tThreads[p->nThread].hEv != INVALID_HANDLE_VALUE)
        SetEvent (tThreads[p->nThread].hEv);

    pthread_join (tThreads[p->nThread].hThread, NULL);   /* TFTP 线程自行关闭 socket */

    if (s != INVALID_SOCKET)
    {
        closesocket (s);
        tThreads[p->nThread].skt = INVALID_SOCKET;
    }
    if (tThreads[p->nThread].hEv != INVALID_HANDLE_VALUE)
    {
        CloseHandle (tThreads[p->nThread].hEv);
        tThreads[p->nThread].hEv = INVALID_HANDLE_VALUE;
    }
    LogToMonitor ("%s service stopped\n", p->szName);
}

int PalServiceStartMask (unsigned uMask)
{
int Ark, nStarted = 0;

    for (Ark = 0; Ark < SizeOfTab (sServices); Ark++)
        if ((uMask & sServices[Ark].uMask) != 0 && PalSvcStartOne (Ark))
            nStarted++;
    sSettings.uRunningServices |= uMask;
    return nStarted;
}

void PalServiceStopMask (unsigned uMask)
{
int Ark;

    for (Ark = 0; Ark < SizeOfTab (sServices); Ark++)
        if ((uMask & sServices[Ark].uMask) != 0)
            PalSvcStopOne (Ark);

    sSettings.uRunningServices &= ~ uMask;
}

/* 保留 M1 入口，供旧 ArkTS 调用 */
int TftpdServiceStart (void)
{
    return PalServiceStartMask (sSettings.uServices);
}

void TftpdServiceStop (void)
{
    PalServiceStopMask (SERVICE_TFTP | SERVICE_SYSLOG | SERVICE_SNTP | SERVICE_DNS);
}
```

- [ ] **Step 3.2: 补充位宏与线程表**

确认 `_hdr_custom.h` 已有 `SERVICE_TFTP` 等宏；若没有，追加：

```c
#define SERVICE_TFTP          0x0001
#define SERVICE_TFTPCLIENT    0x0002
#define SERVICE_SYSLOG        0x0008
#define SERVICE_SNTP          0x0010
#define SERVICE_DNS           0x0020
```

确认 `threading.h` 的 `enum e_Threads` 含 `TH_TFTP, TH_SYSLOG, TH_SNTP, TH_DNS`，且 `struct S_Thread` 含 `hThread`、`gRunning`、`bInit`、`skt`、`hEv`；缺 `hThread` 时补：

```c
    pthread_t   hThread;
```

- [ ] **Step 3.3: 编译（此时 syslogd/sntpd/dns 尚未拷贝，先注释 CMake 中新文件，仅验证框架）**

Run: 同 Task 1 Step 1.4 的构建命令。
Expected: `BUILD SUCCESSFUL`。

- [ ] **Step 3.4: 提交**

```powershell
git add entry/src/main/cpp/tftpd/service_main.c entry/src/main/cpp/tftpd/_hdr_custom.h entry/src/main/cpp/tftpd/threading.h
git commit -m "feat(services): table-driven multi-service manager with configurable ports"
```

---

### Task 4: Syslog 服务器（SL-01~SL-03）

**Files:**
- Copy + patch: `reference/tftpd64/src/_services/syslogd.c` → `entry/src/main/cpp/tftpd/syslogd.c`
- Modify: `entry/src/main/cpp/tftpd/console_pal.c`

- [ ] **Step 4.1: 拷贝并删除 Windows 命名管道**

```powershell
Copy-Item reference\tftpd64\src\_services\syslogd.c entry\src\main\cpp\tftpd\syslogd.c
```

补丁 1 —— 删除 `const char *sPipeName = ...` 一行。

补丁 2 —— `SyslogProc` 中删除：

```c
   if (sSettings.bSyslogPipe)
   {
       m_hPipe = CreateFile (sPipeName, ...);
   }
```

以及循环内的 `if (m_hPipe != INVALID_HANDLE_VALUE) WriteFile(...)` 与函数末尾的 `CloseHandle (m_hPipe);`，保留 `HANDLE m_hPipe=INVALID_HANDLE_VALUE;` 声明但不再使用（HarmonyOS 无 Windows 命名管道，`PipeSyslogMsg` 配置项保留但不生效，记入平台偏差表）。

- [ ] **Step 4.2: console_pal.c 增加 Syslog 事件帧**

在 `Emit()` 之前加入：

```c
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
```

在 `SendMsgRequest` 的 `switch (dwMsgId)`（或等价的 message 分派处）追加两个分支：

```c
        case C_SYSLOG:
        {
            const struct S_SyslogMsg *pMsg = (const struct S_SyslogMsg *) lpMsgRequest;
            char szFrom [64], szTxt [SYSLOG_MAXMSG + 8], szJson [SYSLOG_MAXMSG * 2 + 200];

            PalJsonEscape (szFrom, sizeof szFrom, pMsg->from);
            PalJsonEscape (szTxt,  sizeof szTxt,  pMsg->txt);
            snprintf (szJson, sizeof szJson, "{\"from\":\"%s\",\"txt\":\"%s\"}",
                      szFrom, szTxt);
            PalEmitEvent ("syslog", szJson);
            break;
        }

        case C_DNS_NEW_ENTRY:
        {
            const struct S_DNS_NewEntry *pMsg = (const struct S_DNS_NewEntry *) lpMsgRequest;
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
```

> `C_SYSLOG`/`C_DNS_NEW_ENTRY` 的原型来源于 `_hdr_custom.h` 中 `S_SyslogMsg`（`char from[64]; char txt[SYSLOG_MAXMSG]`）与 `S_DNS_NewEntry`（`char name[NI_MAXHOST]; char ipv4[16]; char ipv6[64]`），必须在 `headers.h` 中可见。若原版这两个结构体声明在其它头文件，补 `#include` 或按上述字段定义补齐。

- [ ] **Step 4.3: 接收线程收尾修正**

`SyslogProc` 退出前 `CloseHandle (hSysLogFile)` 后，把 `hSysLogFile` 重置为 `INVALID_HANDLE_VALUE`（支持停/启循环）。

- [ ] **Step 4.4: 编译验证**

```powershell
& "D:\Program Files\Huawei\DevEco Studio\tools\node\node.exe" "D:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.js" --mode module -p module=entry@default -p product=default assembleHap
```

Expected: `BUILD SUCCESSFUL`。若 `recvfrom` 第 6 参报类型错误（`int*` vs `socklen_t*`），把 `int nDummy` 改为 `socklen_t nDummy`。

- [ ] **Step 4.5: 提交**

```powershell
git add entry/src/main/cpp/tftpd/syslogd.c entry/src/main/cpp/tftpd/console_pal.c
git commit -m "feat(syslog): port syslogd.c with PAL and emit C_SYSLOG events"
```

---

### Task 5: SNTP 服务器（ST-01~ST-03）

**Files:**
- Copy + patch: `reference/tftpd64/src/_services/sntpd.c` → `entry/src/main/cpp/tftpd/sntpd.c`

- [ ] **Step 5.1: 拷贝并打三处补丁**

```powershell
Copy-Item reference\tftpd64\src\_services\sntpd.c entry\src\main\cpp\tftpd\sntpd.c
```

补丁 1 —— 删除 `#include <process.h>`。

补丁 2 —— 删除 mt 冲突的三个 typedef（musl 已在 `<sys/types.h>` 定义，且 `unsigned long` 与 musl 的 `unsigned int` 不一致）：

```c
typedef unsigned char  u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned long  u_int32_t;
```

补丁 3 —— `SntpdProc` 中 `int nFromLen = sizeof SockFrom;` 改为 `socklen_t nFromLen = sizeof SockFrom;`，并在循环内每次 `recvfrom` 之前重置 `nFromLen = sizeof SockFrom;`。

- [ ] **Step 5.2: 端口解耦**

原文件 `SntpFillMessage` 不涉及端口；监听端口由 `service_main.c` 的 `sSettings.SntpPort` 决定，无需改 `SNTP_PORT` 宏（保留原值仅供参照）。

- [ ] **Step 5.3: 编译验证**

Run: 同 Task 1 Step 1.4。
Expected: `BUILD SUCCESSFUL`；无 `u_int32_t` 重复定义告警。

- [ ] **Step 5.4: 提交**

```powershell
git add entry/src/main/cpp/tftpd/sntpd.c
git commit -m "feat(sntp): port sntpd.c to POSIX with configurable port"
```

---

### Task 6: DNS 服务器 + hosts.txt（DN-01~DN-05）

**Files:**
- Copy + patch: `reference/tftpd64/src/_services/dns.c` → `entry/src/main/cpp/tftpd/dns.c`
- Create: `entry/src/main/cpp/tftpd/dns_hosts.c`

- [ ] **Step 6.1: 拷贝 dns.c 并只改一处**

```powershell
Copy-Item reference\tftpd64\src\_services\dns.c entry\src\main\cpp\tftpd\dns.c
```

把 `ProcessDnsQuery`（`#ifdef IPv6` 分支）开头改为先查本地 hosts 文件：

```c
static int ProcessDnsQuery (const DNS_QUESTION_EXP *req, DNS_ANSWER_EXP *ans)
{
struct addrinfo *info = NULL, *cur;
int Rc;
int n = 0;

	memset (ans, 0, sizeof *ans);
	ans->ipv4 = INADDR_NONE;

	if (PalDnsHostsLookup (req->name, & ans->ipv4, ans->ipv6, & ans->nb_ipv6_address, ans->cname))
	{
		ans->qclass = req->qclass;
		ans->qtype  = req->qtype;
		ans->cname[0] = 0;          /* hosts 文件不提供 CNAME */
		return TRUE;
	}

	Rc = getaddrinfo(req->name, NULL, NULL, &info);
	...（以下保持原样）
```

同时把 `int nDummy` 改为 `socklen_t nDummy`（`ListenDNSMessage`）。

- [ ] **Step 6.2: dns_hosts.c 完整实现**

```c
//////////////////////////////////////////////////////////
// dns_hosts.c —— 本地静态解析表
// 文件：<BaseDirectory>/hosts.txt
// 行格式：主机名  IPv4  IPv6      （任一列可用 "-" 表示缺省）
// 例：myhost 192.168.1.100 -
//////////////////////////////////////////////////////////
#include "headers.h"

BOOL PalDnsHostsLookup (const char *szName, long *plIpv4, IN6_ADDR *pIpv6,
                        int *pnNbV6, char *szCname)
{
char  szPath [_MAX_PATH];
char  szLine [512];
FILE *f;
int   nFound = FALSE;

    * plIpv4   = INADDR_NONE;
    * pnNbV6   = 0;
    szCname[0] = 0;

    snprintf (szPath, sizeof szPath, "%s/hosts.txt", sSettings.szWorkingDirectory);
    f = fopen (szPath, "r");
    if (f == NULL)  return FALSE;

    while (fgets (szLine, sizeof szLine, f) != NULL)
    {
        char szHost [128] = "", szV4 [64] = "", szV6 [128] = "";
        char *p = szLine;

        while (*p == ' ' || *p == '\t')  p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == 0)  continue;

        if (sscanf (p, "%127s %63s %127s", szHost, szV4, szV6) < 2)  continue;
        if (lstrcmpi (szHost, (char *) szName) != 0)  continue;

        if (szV4[0] != '-' && inet_pton (AF_INET, szV4, plIpv4) != 1)
            * plIpv4 = INADDR_NONE;

        if (szV6[0] != 0 && szV6[0] != '-' && * pnNbV6 < 10)
        {
            if (inet_pton (AF_INET6, szV6, & pIpv6[* pnNbV6]) == 1)
                (* pnNbV6) ++;
        }
        nFound = TRUE;
        break;
    }
    fclose (f);
    return nFound;
}
```

注意：`ProcessDnsQuery` 传入的是网络字节序的 `s_addr`，因此 `inet_pton` 结果直接赋给 `long *plIpv4`（`* plIpv4` 即 `s_addr` 存储值）。

- [ ] **Step 6.3: 编译验证 + hosts 目录**

`hosts.txt` 位于 BaseDirectory，用户可通过 Directory 页签看到并编辑（Task 10 提供编辑框）。

Expected: 构建成功；`nm` 检查 `AnswerDnsQuery` 已导出（无需导出，仅供内部）。

- [ ] **Step 6.4: 提交**

```powershell
git add entry/src/main/cpp/tftpd/dns.c entry/src/main/cpp/tftpd/dns_hosts.c
git commit -m "feat(dns): port dns.c and add static hosts.txt lookup"
```

---

### Task 7: TFTP 客户端（TC-01~TC-05）

**Files:**
- Create: `entry/src/main/cpp/tftpd/tftp_client.c`

- [ ] **Step 7.1: 完整实现**

```c
//////////////////////////////////////////////////////////
// tftp_client.c —— TFTP 客户端（移植自 _gui/tftp_cli.c 的协议语义）
// 原版基于 WSAAsyncSelect + 窗口定时器；此处改写为阻塞 poll() 状态机，
// 选项协商、块号回绕、超时重传（Timeout / Retransmit）语义与原版一致。
// 一次传输一个工作线程，进度经 PalEmitEvent("client", JSON) 上报。
//////////////////////////////////////////////////////////
#include "headers.h"
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>

struct S_PalCli
{
    int   nOpcode;                  /* TFTP_RRQ / TFTP_WRQ */
    char  szHost [256];
    int   nPort;
    char  szRemote [256];
    char  szLocal  [_MAX_PATH];
    int   nBlkSize;
};

static volatile int s_bClientBusy = FALSE;

static void PalCliEmit (const char *szState, __int64 qwBytes, __int64 qwSize,
                        unsigned uBlk, int nRc, const char *szMsg, const char *szMd5)
{
char szBuf [768];
char szMsg2 [256];
char szMd52 [40];

    PalJsonEscape (szMsg2,  sizeof szMsg2,  szMsg == NULL ? "" : szMsg);
    PalJsonEscape (szMd52,  sizeof szMd52,  szMd5 == NULL ? "" : szMd5);
    snprintf (szBuf, sizeof szBuf,
              "{\"state\":\"%s\",\"bytes\":%lld,\"size\":%lld,\"blk\":%u,\"rc\":%d,"
              "\"msg\":\"%s\",\"md5\":\"%s\"}",
              szState, (long long) qwBytes, (long long) qwSize,
              uBlk, nRc, szMsg2, szMd52);
    PalEmitEvent ("client", szBuf);
}

static int PalCliResolve (const char *szHost, int nPort,
                          struct sockaddr_storage *pSrv, int *pnLen)
{
struct addrinfo hints, *res = NULL;
char szServ [16];
int Rc;

    memset (& hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf (szServ, sizeof szServ, "%d", nPort);

    Rc = getaddrinfo (szHost, szServ, & hints, & res);
    if (Rc != 0 || res == NULL)  return FALSE;

    memcpy (pSrv, res->ai_addr, res->ai_addrlen);
    * pnLen = (int) res->ai_addrlen;
    freeaddrinfo (res);
    return TRUE;
}

static int PalCliSamePeer (const struct sockaddr_storage *a, const struct sockaddr_storage *b)
{
    if (a->ss_family != b->ss_family)  return FALSE;
    if (a->ss_family == AF_INET)
    {
        const struct sockaddr_in *x = (const struct sockaddr_in *) a;
        const struct sockaddr_in *y = (const struct sockaddr_in *) b;
        return x->sin_port == y->sin_port && x->sin_addr.s_addr == y->sin_addr.s_addr;
    }
    else
    {
        const struct sockaddr_in6 *x = (const struct sockaddr_in6 *) a;
        const struct sockaddr_in6 *y = (const struct sockaddr_in6 *) b;
        return x->sin6_port == y->sin6_port
            && memcmp (& x->sin6_addr, & y->sin6_addr, sizeof x->sin6_addr) == 0;
    }
}

static void PalCliMd5Hex (MD5_CTX *pCtx, char *szOut)
{
unsigned char id [16];
int Ark;

    MD5Final (id, pCtx);
    for (Ark = 0; Ark < 16; Ark++)
        snprintf (szOut + 2 * Ark, 3, "%02x", id[Ark]);
    szOut[32] = 0;
}

static int PalCliBuildRequest (struct S_PalCli *p, char *pBuf, __int64 qwSize)
{
struct tftphdr *tp = (struct tftphdr *) pBuf;
char *q = tp->th_stuff;

    tp->th_opcode = htons ((unsigned short) p->nOpcode);
    strcpy (q, p->szRemote);  q += strlen (q) + 1;
    strcpy (q, "octet");      q += sizeof "octet";

    if (p->nBlkSize >= TFTP_MINSEGSIZE)
    {
        strcpy (q, TFTP_OPT_BLKSIZE);  q += sizeof TFTP_OPT_BLKSIZE;
        q += sprintf (q, "%d", p->nBlkSize) + 1;
    }
    strcpy (q, TFTP_OPT_TSIZE);  q += sizeof TFTP_OPT_TSIZE;
    q += sprintf (q, "%lld", (long long) qwSize) + 1;

    return (int)(q - pBuf);
}

static void PalCliParseOack (const char *pBuf, int nLen, int *pnBlk, __int64 *pqwSize)
{
const char *p = pBuf + TFTP_DATA_HEADERSIZE;
const char *pEnd = pBuf + nLen;

    while (p < pEnd && *p != 0)
    {
        const char *pOpt = p;
        const char *pVal = p + strlen (p) + 1;

        if (pVal >= pEnd)  break;
        if (lstrcmpi ((char *) pOpt, TFTP_OPT_BLKSIZE) == 0)  * pnBlk   = atoi (pVal);
        if (lstrcmpi ((char *) pOpt, TFTP_OPT_TSIZE)   == 0)  * pqwSize = _atoi64 (pVal);
        p = pVal + strlen (pVal) + 1;
    }
}
```

GET 状态机（追加到同文件）：

```c
static int PalCliGet (struct S_PalCli *p)
{
SOCKET s = INVALID_SOCKET;
struct sockaddr_storage srv, from;
struct pollfd pfd;
char  ack [4];
char *pBuf = NULL;
int   nPkt, nReq, nRc, nAddrLen = 0, nTimeOut = 0;
int   nCount = 0, iRc = -1;
__int64 qwTotal = 0, qwSize = 0;
HANDLE hFile = INVALID_HANDLE_VALUE;
MD5_CTX ctx;
char  szMd5 [40] = "";
time_t tLast = 0;

    nPkt = (p->nBlkSize >= TFTP_MINSEGSIZE) ? p->nBlkSize : TFTP_SEGSIZE;

    if (! PalCliResolve (p->szHost, p->nPort, & srv, & nAddrLen))
    { PalCliEmit ("error", 0, 0, 0, -1, "host not resolved", "");  return -1; }

    s = socket (srv.ss_family, SOCK_DGRAM, 0);
    pBuf = (char *) malloc (MAXPKTSIZE);
    if (s == INVALID_SOCKET || pBuf == NULL)
    { PalCliEmit ("error", 0, 0, 0, -1, "socket/malloc failed", "");  iRc = -1;  goto Exit; }

    hFile = CreateFile (p->szLocal, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    { PalCliEmit ("error", 0, 0, 0, -1, "cannot create local file", "");  iRc = -1;  goto Exit; }

    MD5Init (& ctx);

    nReq = PalCliBuildRequest (p, pBuf, 0);
    sendto (s, pBuf, nReq, 0, (struct sockaddr *) & srv, nAddrLen);

    pfd.fd = s;
    pfd.events = POLLIN;

    for (;;)
    {
        struct tftphdr *tp;
        unsigned short uOp, uBlk;
        int nWait;

        nWait = (int) sSettings.Timeout * 1000;
        nRc = poll (& pfd, 1, nWait);

        if (nRc <= 0)                                   /* 超时重传 */
        {
            if (++ nTimeOut > (int) sSettings.Retransmit)
            { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "timeout", "");  iRc = -1;  break; }
            sendto (s, pBuf, nReq, 0, (struct sockaddr *) & srv, nAddrLen);
            continue;
        }

        {
            socklen_t nFromLen = sizeof from;
            nRc = (int) recvfrom (s, pBuf, MAXPKTSIZE, 0,
                                  (struct sockaddr *) & from, & nFromLen);
        }
        if (nRc <= TFTP_DATA_HEADERSIZE)  continue;
        if (! PalCliSamePeer (& from, & srv))  continue;   /* 丢弃陌生 TID */

        tp = (struct tftphdr *) pBuf;
        uOp = ntohs (tp->th_opcode);
        uBlk = ntohs (tp->th_block);

        if (uOp == TFTP_ERROR)
        { PalCliEmit ("error", qwTotal, qwSize, uBlk, (int) ntohs ((unsigned short) tp->th_code),
                    "server error", "");  iRc = -1;  break; }

        if (uOp == TFTP_OACK && nCount == 0)
        {
            PalCliParseOack (pBuf, nRc, & nPkt, & qwSize);
            memset (ack, 0, sizeof ack);
            ack[1] = TFTP_ACK;
            sendto (s, ack, 4, 0, (struct sockaddr *) & srv, nAddrLen);
            nTimeOut = 0;
            continue;
        }

        if (uOp != TFTP_DATA)  continue;

        if (uBlk == (unsigned short)(nCount + 1))
        {
            int   nData = nRc - TFTP_DATA_HEADERSIZE;
            DWORD nWrt  = 0;

            if (! WriteFile (hFile, tp->th_data, nData, & nWrt, NULL) || (int) nWrt != nData)
            { PalCliEmit ("error", qwTotal, qwSize, uBlk, -1, "write failed", "");  iRc = -1;  break; }

            MD5Update (& ctx, (unsigned char *) tp->th_data, (unsigned) nData);
            qwTotal += nData;
            nCount ++;
            nTimeOut = 0;

            ack[0] = 0;  ack[1] = TFTP_ACK;
            ack[2] = (char)(nCount >> 8);  ack[3] = (char)(nCount & 0xFF);
            sendto (s, ack, 4, 0, (struct sockaddr *) & srv, nAddrLen);

            nReq = 4;
            memcpy (pBuf, ack, 4);                        /* 重传用：保存最后一个 ACK */

            if (time (NULL) != tLast)
            { tLast = time (NULL);  PalCliEmit ("progress", qwTotal, qwSize, nCount, 0, "", ""); }

            if (nData < nPkt)                             /* 末块 */
            {
                PalCliMd5Hex (& ctx, szMd5);
                PalCliEmit ("done", qwTotal, qwTotal, nCount, 0, "", szMd5);
                iRc = 0;
                break;
            }
        }
        else if (uBlk == (unsigned short) nCount)         /* 重复块：重发 ACK */
            sendto (s, pBuf, nReq, 0, (struct sockaddr *) & srv, nAddrLen);
    }

Exit:
    if (hFile != INVALID_HANDLE_VALUE)  CloseHandle (hFile);
    if (s != INVALID_SOCKET)            closesocket (s);
    if (pBuf != NULL)                   free (pBuf);
    return iRc;
}
```

PUT 状态机：

```c
static int PalCliPut (struct S_PalCli *p)
{
SOCKET s = INVALID_SOCKET;
struct sockaddr_storage srv, from;
struct pollfd pfd;
char *pBuf = NULL;
int   nPkt, nReq, nRc, nAddrLen = 0, nTimeOut = 0;
int   nCount = 0, nToSend = 0, iRc = -1, nRead = 0;
__int64 qwTotal = 0, qwSize = 0;
HANDLE hFile = INVALID_HANDLE_VALUE;
MD5_CTX ctx;
char  szMd5 [40] = "";
struct stat st;
time_t tLast = 0;

    if (stat (p->szLocal, & st) != 0)
    { PalCliEmit ("error", 0, 0, 0, -1, "local file missing", "");  return -1; }
    qwSize = (__int64) st.st_size;

    nPkt = (p->nBlkSize >= TFTP_MINSEGSIZE) ? p->nBlkSize : TFTP_SEGSIZE;

    if (! PalCliResolve (p->szHost, p->nPort, & srv, & nAddrLen))
    { PalCliEmit ("error", 0, 0, 0, -1, "host not resolved", "");  return -1; }

    s = socket (srv.ss_family, SOCK_DGRAM, 0);
    pBuf = (char *) malloc (MAXPKTSIZE);
    if (s == INVALID_SOCKET || pBuf == NULL)
    { PalCliEmit ("error", 0, 0, 0, -1, "socket/malloc failed", "");  iRc = -1;  goto Exit; }

    hFile = CreateFile (p->szLocal, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    { PalCliEmit ("error", 0, 0, qwSize, -1, "cannot open local file", "");  iRc = -1;  goto Exit; }

    MD5Init (& ctx);
    nReq = PalCliBuildRequest (p, pBuf, qwSize);
    sendto (s, pBuf, nReq, 0, (struct sockaddr *) & srv, nAddrLen);

    pfd.fd = s;
    pfd.events = POLLIN;

    for (;;)
    {
        struct tftphdr *tp;
        unsigned short uOp, uBlk;

        nRc = poll (& pfd, 1, (int) sSettings.Timeout * 1000);
        if (nRc <= 0)
        {
            if (++ nTimeOut > (int) sSettings.Retransmit)
            { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "timeout", "");  iRc = -1;  break; }
            sendto (s, pBuf, nToSend > 0 ? nToSend : nReq, 0,
                    (struct sockaddr *) & srv, nAddrLen);
            continue;
        }

        {
            socklen_t nFromLen = sizeof from;
            nRc = (int) recvfrom (s, pBuf, MAXPKTSIZE, 0,
                                  (struct sockaddr *) & from, & nFromLen);
        }
        if (nRc < 4)  continue;
        if (! PalCliSamePeer (& from, & srv))  continue;

        tp = (struct tftphdr *) pBuf;
        uOp = ntohs (tp->th_opcode);
        uBlk = ntohs (tp->th_block);

        if (uOp == TFTP_ERROR)
        { PalCliEmit ("error", qwTotal, qwSize, uBlk,
                    (int) ntohs ((unsigned short) tp->th_code), "server error", "");  iRc = -1;  break; }

        if (uOp == TFTP_OACK)
        {
            PalCliParseOack (pBuf, nRc, & nPkt, & qwSize);
            nRc = 0;                                      /* 走下面的发送首块流程 */
        }
        else if (uOp != TFTP_ACK)  continue;
        else if (uBlk == (unsigned short)(nCount - 1))  /* 重复 ACK */
        { sendto (s, pBuf, nToSend, 0, (struct sockaddr *) & srv, nAddrLen);  continue; }
        else if (uBlk != (unsigned short) nCount)  continue;

        nTimeOut = 0;
        nRead = 0;
        if (! ReadFile (hFile, pBuf + TFTP_DATA_HEADERSIZE, nPkt, (DWORD *) & nRead, NULL))
        { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "read failed", "");  iRc = -1;  break; }

        if (nRead == 0)  continue;                        /* 理论上不应到达 */

        nCount ++;
        ((struct tftphdr *) pBuf)->th_opcode = htons (TFTP_DATA);
        ((struct tftphdr *) pBuf)->th_block  = htons ((unsigned short) nCount);
        nToSend = TFTP_DATA_HEADERSIZE + nRead;

        MD5Update (& ctx, (unsigned char *) pBuf + TFTP_DATA_HEADERSIZE, (unsigned) nRead);
        qwTotal += nRead;

        sendto (s, pBuf, nToSend, 0, (struct sockaddr *) & srv, nAddrLen);

        if (time (NULL) != tLast)
        { tLast = time (NULL);  PalCliEmit ("progress", qwTotal, qwSize, nCount, 0, "", ""); }

        if (nRead < nPkt)                                 /* 末块已发，等最后一个 ACK */
        {
            nRc = poll (& pfd, 1, (int) sSettings.Timeout * 1000);
            if (nRc > 0)
            {
                socklen_t nFromLen = sizeof from;
                nRc = (int) recvfrom (s, pBuf, MAXPKTSIZE, 0,
                                      (struct sockaddr *) & from, & nFromLen);
                if (nRc >= 4 && ntohs (((struct tftphdr *) pBuf)->th_opcode) == TFTP_ACK
                    && ntohs (((struct tftphdr *) pBuf)->th_block) == (unsigned short) nCount)
                {
                    PalCliMd5Hex (& ctx, szMd5);
                    PalCliEmit ("done", qwTotal, qwSize, nCount, 0, "", szMd5);
                    iRc = 0;
                }
                else
                { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "last ack missing", "");  iRc = -1; }
            }
            else
            { PalCliEmit ("error", qwTotal, qwSize, nCount, -1, "timeout on last ack", "");  iRc = -1; }
            break;
        }
    }

Exit:
    if (hFile != INVALID_HANDLE_VALUE)  CloseHandle (hFile);
    if (s != INVALID_SOCKET)            closesocket (s);
    if (pBuf != NULL)                   free (pBuf);
    return iRc;
}
```

线程入口与导出：

```c
static void *PalClientThread (void *pArg)
{
struct S_PalCli *p = (struct S_PalCli *) pArg;

    PalCliEmit ("start", 0, 0, 0, 0, "", "");
    if (p->nOpcode == TFTP_RRQ)  PalCliGet (p);
    else                         PalCliPut (p);
    free (p);
    s_bClientBusy = FALSE;
    return NULL;
}

int PalClientStart (int nOpcode, const char *szHost, int nPort,
                    const char *szRemote, const char *szLocal, int nBlkSize)
{
struct S_PalCli *p;
pthread_t th;

    if (s_bClientBusy)
    { PalCliEmit ("error", 0, 0, 0, -1, "client busy", "");  return FALSE; }

    if (nBlkSize > 0 && (nBlkSize < TFTP_MINSEGSIZE || nBlkSize > TFTP_MAXSEGSIZE))
        nBlkSize = 0;                                     /* 0 = 默认 512 */

    p = (struct S_PalCli *) calloc (1, sizeof * p);
    if (p == NULL)  return FALSE;

    p->nOpcode = nOpcode;
    lstrcpyn (p->szHost,   (char *) szHost,   sizeof p->szHost);
    lstrcpyn (p->szRemote, (char *) szRemote, sizeof p->szRemote);
    lstrcpyn (p->szLocal,  (char *) szLocal,  sizeof p->szLocal);
    p->nPort    = nPort > 0 ? nPort : 69;
    p->nBlkSize = nBlkSize;

    s_bClientBusy = TRUE;
    if (pthread_create (& th, NULL, PalClientThread, p) != 0)
    { s_bClientBusy = FALSE;  free (p);  return FALSE; }
    pthread_detach (th);
    return TRUE;
}
```

> `TFTP_MAXSEGSIZE`/`TFTP_MINSEGSIZE` 取自 `_hdr_custom.h`，若名为 `TFTP_MAX_PACKET` 等，按既有宏替换；`TFTP_OPT_BLKSIZE`/`TFTP_OPT_TSIZE` 同理。

- [ ] **Step 7.2: 编译**

Expected: 无未定义符号（`MAXPKTSIZE`、`TFTP_*` 宏来自原头文件）。

- [ ] **Step 7.3: 提交**

```powershell
git add entry/src/main/cpp/tftpd/tftp_client.c
git commit -m "feat(client): poll-based TFTP client with RRQ/WRQ and option negotiation"
```

---

### Task 8: MD5 与目录列表（MD-01~MD-04 / UI-04）

**Files:**
- Create: `entry/src/main/cpp/tftpd/file_tool_pal.c`
- Modify: `entry/src/main/cpp/napi_init.cpp`
- Modify: `entry/src/main/cpp/types/libtftpd32/Index.d.ts`

- [ ] **Step 8.1: file_tool_pal.c 完整实现**

```c
//////////////////////////////////////////////////////////
// file_tool_pal.c —— MD5 计算与目录列表（GUI Directory 页签 / MD5 工具）
//////////////////////////////////////////////////////////
#include "headers.h"

BOOL PalComputeMd5 (const char *szPath, char *szOut, int nOut)
{
HANDLE h;
char   buf [8192];
DWORD  nRead = 0;
MD5_CTX ctx;
unsigned char id [16];
int Ark;

    if (nOut < 33)  return FALSE;

    h = CreateFile (szPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)  return FALSE;

    MD5Init (& ctx);
    while (ReadFile (h, buf, sizeof buf, & nRead, NULL) && nRead > 0)
        MD5Update (& ctx, (unsigned char *) buf, (unsigned) nRead);
    CloseHandle (h);

    MD5Final (id, & ctx);
    for (Ark = 0; Ark < 16; Ark++)
        snprintf (szOut + 2 * Ark, 3, "%02x", id[Ark]);
    szOut[32] = 0;
    return TRUE;
}

int PalListDirectory (const char *szPath, char *szJson, int nJson)
{
DIR *d;
struct dirent *e;
int n = 0, nFirst = 1;

    d = opendir (szPath);
    if (d == NULL)
    { snprintf (szJson, nJson, "[]");  return 2; }

    n = snprintf (szJson, nJson, "[");
    while ((e = readdir (d)) != NULL && n < nJson - 320)
    {
        char szFull [1024], szName [512];
        struct stat st;
        int  bDir = 0;
        long long qwSize = 0;

        if (strcmp (e->d_name, ".") == 0 || strcmp (e->d_name, "..") == 0)  continue;

        snprintf (szFull, sizeof szFull, "%s/%s", szPath, e->d_name);
        if (stat (szFull, & st) == 0)
        { bDir = S_ISDIR (st.st_mode) ? 1 : 0;  qwSize = (long long) st.st_size; }

        PalJsonEscape (szName, sizeof szName, e->d_name);
        n += snprintf (szJson + n, nJson - n,
                       "%s{\"name\":\"%s\",\"size\":%lld,\"dir\":%d}",
                       nFirst ? "" : ",", szName, qwSize, bDir);
        nFirst = 0;
    }
    closedir (d);
    n += snprintf (szJson + n, nJson - n, "]");
    return n;
}
```

- [ ] **Step 8.2: napi_init.cpp 追加导出**

```cpp
extern "C" {
int  PalServiceStartMask (unsigned uMask);
void PalServiceStopMask  (unsigned uMask);
int  PalClientStart (int opcode, const char *szHost, int nPort,
                     const char *szRemote, const char *szLocal, int nBlkSize);
BOOL PalComputeMd5 (const char *szPath, char *szOut, int nOut);
int  PalListDirectory (const char *szPath, char *szJson, int nJson);
BOOL Pal_GetSettingStr (const char *szKey, char *szOut, int nOut);
}

static napi_value StartServiceMask (napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    double dMask = 0;
    napi_value ret;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_double (env, argv[0], & dMask);
    napi_get_boolean (env, PalServiceStartMask ((unsigned) dMask) > 0, & ret);
    return ret;
}

static napi_value StopServiceMask (napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1];
    double dMask = 0;
    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_double (env, argv[0], & dMask);
    PalServiceStopMask ((unsigned) dMask);
    return nullptr;
}

static napi_value StartTftpClient (napi_env env, napi_callback_info info)
{
    size_t argc = 6;
    napi_value argv[6];
    char szHost[256] = "", szRemote[256] = "", szLocal[1024] = "";
    double dOpcode = 1, dPort = 69, dBlk = 0;
    size_t len;
    napi_value ret;

    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    if (argc < 5)  { napi_get_boolean (env, false, & ret);  return ret; }

    napi_get_value_double (env, argv[0], & dOpcode);
    napi_get_value_string_utf8 (env, argv[1], szHost, sizeof szHost, & len);
    napi_get_value_double (env, argv[2], & dPort);
    napi_get_value_string_utf8 (env, argv[3], szRemote, sizeof szRemote, & len);
    napi_get_value_string_utf8 (env, argv[4], szLocal, sizeof szLocal, & len);
    if (argc > 5)  napi_get_value_double (env, argv[5], & dBlk);

    napi_get_boolean (env, PalClientStart ((int) dOpcode, szHost, (int) dPort,
                                           szRemote, szLocal, (int) dBlk) != 0, & ret);
    return ret;
}

static napi_value ComputeMd5 (napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1], ret;
    char szPath[1024] = "", szOut[40] = "";
    size_t len;

    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], szPath, sizeof szPath, & len);

    if (PalComputeMd5 (szPath, szOut, sizeof szOut))
        napi_create_string_utf8 (env, szOut, NAPI_AUTO_LENGTH, & ret);
    else
        napi_get_undefined (env, & ret);
    return ret;
}

static napi_value ListDirectory (napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1], ret;
    char szPath[1024] = "";
    size_t len;
    char *pJson = (char *) malloc (32768);

    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], szPath, sizeof szPath, & len);
    if (pJson == nullptr)
    { napi_create_string_utf8 (env, "[]", 2, & ret);  return ret; }

    PalListDirectory (szPath, pJson, 32768);
    napi_create_string_utf8 (env, pJson, NAPI_AUTO_LENGTH, & ret);
    free (pJson);
    return ret;
}

static napi_value GetSettingStr (napi_env env, napi_callback_info info)
{
    size_t argc = 1;
    napi_value argv[1], ret;
    char szKey[128] = "", szVal[_MAX_PATH] = "";
    size_t len;

    napi_get_cb_info (env, info, & argc, argv, nullptr, nullptr);
    napi_get_value_string_utf8 (env, argv[0], szKey, sizeof szKey, & len);

    Pal_GetSettingStr (szKey, szVal, sizeof szVal);
    napi_create_string_utf8 (env, szVal, NAPI_AUTO_LENGTH, & ret);
    return ret;
}
```

在 `Init()` 的 `napi_property_descriptor desc[]` 中追加：

```cpp
    { "startServiceMask", nullptr, StartServiceMask, nullptr, nullptr, nullptr, napi_default, nullptr },
    { "stopServiceMask",  nullptr, StopServiceMask,  nullptr, nullptr, nullptr, napi_default, nullptr },
    { "startTftpClient",  nullptr, StartTftpClient,  nullptr, nullptr, nullptr, napi_default, nullptr },
    { "computeMd5",       nullptr, ComputeMd5,       nullptr, nullptr, nullptr, napi_default, nullptr },
    { "listDirectory",    nullptr, ListDirectory,    nullptr, nullptr, nullptr, napi_default, nullptr },
    { "getSettingStr",    nullptr, GetSettingStr,    nullptr, nullptr, nullptr, napi_default, nullptr },
```

`napi_init.cpp` 需 `#include <cstdlib>` 与 `#include "tftpd/_hdr_settings.h"`（或使用 `PATH_MAX` 常量替代 `_MAX_PATH`）。

- [ ] **Step 8.3: Index.d.ts 追加声明**

```ts
export const startServiceMask: (mask: number) => boolean;
export const stopServiceMask: (mask: number) => void;
export const startTftpClient: (opcode: number, host: string, port: number,
                               remote: string, local: string, blkSize: number) => boolean;
export const computeMd5: (path: string) => string | undefined;
export const listDirectory: (path: string) => string;
export const getSettingStr: (key: string) => string;
```

- [ ] **Step 8.4: 编译并提交**

```powershell
git add entry/src/main/cpp/tftpd/file_tool_pal.c entry/src/main/cpp/napi_init.cpp entry/src/main/cpp/types/libtftpd32/Index.d.ts
git commit -m "feat(napi): service masks, tftp client, md5 and directory exports"
```

---

### Task 9: ArkTS 服务层（事件与 API）

**Files:**
- Modify: `entry/src/main/ets/services/TftpdService.ets`

- [ ] **Step 9.1: 追加类型与 API**

在文件顶部类型区追加：

```ts
export interface SyslogMsg { from: string; txt: string; }
export interface DnsQuery  { name: string; ipv4: string; ipv6: string; }
export interface ClientState {
  state: string; bytes: number; size: number;
  blk: number; rc: number; msg: string; md5: string;
}
export interface DirEntry { name: string; size: number; dir: number; }
```

在类中追加静态状态与方法：

```ts
  static readonly syslogs: SyslogMsg[] = [];
  static readonly dnsQueries: DnsQuery[] = [];
  static client: ClientState = { state: 'idle', bytes: 0, size: 0, blk: 0, rc: 0, msg: '', md5: '' };

  static readonly SERVICE_TFTP: number = 0x0001;
  static readonly SERVICE_SYSLOG: number = 0x0008;
  static readonly SERVICE_SNTP: number = 0x0010;
  static readonly SERVICE_DNS: number = 0x0020;

  static startMask(mask: number): boolean { return libtftpd32.startServiceMask(mask); }
  static stopMask(mask: number): void { libtftpd32.stopServiceMask(mask); }

  static startClient(opcode: number, host: string, port: number,
                     remote: string, local: string, blkSize: number): boolean {
    return libtftpd32.startTftpClient(opcode, host, port, remote, local, blkSize);
  }

  static md5(path: string): string { return libtftpd32.computeMd5(path) ?? ''; }

  static listDir(path: string): DirEntry[] {
    try { return JSON.parse(libtftpd32.listDirectory(path)) as DirEntry[]; }
    catch (e) { return []; }
  }

  static getStr(key: string): string { return libtftpd32.getSettingStr(key); }
```

- [ ] **Step 9.2: 事件分派扩展**

在 `onEvent`（或等价 JSON 回调）中按 `type` 追加：

```ts
      } else if (type === 'syslog') {
        const m: SyslogMsg = JSON.parse(text) as SyslogMsg;
        TftpdService.syslogs.unshift(m);
        if (TftpdService.syslogs.length > 200) { TftpdService.syslogs.pop(); }
      } else if (type === 'dns') {
        const q: DnsQuery = JSON.parse(text) as DnsQuery;
        TftpdService.dnsQueries.unshift(q);
        if (TftpdService.dnsQueries.length > 100) { TftpdService.dnsQueries.pop(); }
      } else if (type === 'client') {
        TftpdService.client = JSON.parse(text) as ClientState;
      }
```

（若原实现使用 `if (ev.type === 'log') ...` 链，保持同链式结构。）

- [ ] **Step 9.3: 提交**

```powershell
git add entry/src/main/ets/services/TftpdService.ets
git commit -m "feat(ui): expose M2 services and event streams in TftpdService"
```

---

### Task 10: 8 页签界面（UI-04 / UI-05）

**Files:**
- Create: `entry/src/main/ets/pages/M2Pages.ets`
- Modify: `entry/src/main/ets/pages/Index.ets`

- [ ] **Step 10.1: M2Pages.ets 五个组件**

```ts
import { TftpdService, SyslogMsg, DnsQuery, ClientState, DirEntry } from '../services/TftpdService';

@Component
export struct SyslogPage {
  @State msgs: SyslogMsg[] = TftpdService.syslogs;

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('Syslog 服务器').fontSize(20).fontWeight(FontWeight.Bold)
        Blank()
        Text('端口 ' + TftpdService.getStr('SyslogPort')).fontSize(14).fontColor('#888')
      }.width('100%').padding(12)
      List({ space: 4 }) {
        ForEach(this.msgs, (m: SyslogMsg) => {
          ListItem() {
            Column() {
              Text(m.from).fontSize(12).fontColor('#0A59F7')
              Text(m.txt).fontSize(14)
            }.alignItems(HorizontalAlign.Start).width('100%')
          }
        }, (m: SyslogMsg, i: number) => i + m.from + m.txt)
      }.layoutWeight(1).width('100%').padding({ left: 12, right: 12 })
    }.width('100%').height('100%')
  }
}

@Component
export struct SntpPage {
  @State running: boolean = false;

  build() {
    Column({ space: 12 }) {
      Text('SNTP 服务器').fontSize(20).fontWeight(FontWeight.Bold)
      Text('端口 ' + TftpdService.getStr('SntpPort')).fontSize(14).fontColor('#888')
      Text('服务开关位于「设置」页签。客户端发送 NTP v3 请求（mode=3）即可获得应答。')
        .fontSize(14).fontColor('#666')
    }.width('100%').height('100%').padding(24).alignItems(HorizontalAlign.Start)
  }
}

@Component
export struct DnsPage {
  @State hosts: string = '';
  @State queries: DnsQuery[] = TftpdService.dnsQueries;
  @State status: string = '';

  aboutToAppear() {
    this.hosts = TftpdService.readText(TftpdService.baseDir + '/hosts.txt');
  }

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('DNS 服务器').fontSize(20).fontWeight(FontWeight.Bold)
        Blank()
        Text('端口 ' + TftpdService.getStr('DnsPort')).fontSize(14).fontColor('#888')
      }.width('100%')
      Text('hosts.txt（每行：主机名 IPv4 IPv6，缺省用 -）').fontSize(12).fontColor('#666')
      TextArea({ text: this.hosts })
        .width('100%').height(140)
        .onChange((v: string) => { this.hosts = v; })
      Row({ space: 12 }) {
        Button('保存 hosts.txt').onClick(() => {
          TftpdService.writeText(TftpdService.baseDir + '/hosts.txt', this.hosts);
          this.status = '已保存';
        })
        Text(this.status).fontSize(14).fontColor('#0A59F7')
      }
      Text('最近查询').fontSize(16).margin({ top: 12 })
      List({ space: 2 }) {
        ForEach(this.queries, (q: DnsQuery) => {
          ListItem() {
            Text(q.name + '  →  ' + (q.ipv4 !== '' ? q.ipv4 : q.ipv6))
              .fontSize(14).width('100%')
          }
        }, (q: DnsQuery, i: number) => i + q.name)
      }.layoutWeight(1).width('100%')
    }.width('100%').height('100%').padding(12)
  }
}

@Component
export struct ClientPage {
  @State host: string = '10.0.2.2';
  @State port: string = '69';
  @State remote: string = 'test_1k.txt';
  @State local: string = '';
  @State blkSize: string = '0';
  @State st: ClientState = TftpdService.client;
  @State busy: boolean = false;

  aboutToAppear() {
    this.local = TftpdService.baseDir + '/client_dl.bin';
  }

  build() {
    Column({ space: 10 }) {
      Text('TFTP 客户端').fontSize(20).fontWeight(FontWeight.Bold)
      Row({ space: 8 }) {
        Text('服务器').width(90)
        TextInput({ text: this.host }).layoutWeight(2).onChange((v: string) => { this.host = v; })
        Text('端口').width(48)
        TextInput({ text: this.port }).width(90).onChange((v: string) => { this.port = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('远端文件').width(90)
        TextInput({ text: this.remote }).layoutWeight(1).onChange((v: string) => { this.remote = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('本地文件').width(90)
        TextInput({ text: this.local }).layoutWeight(1).onChange((v: string) => { this.local = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('块大小').width(90)
        TextInput({ text: this.blkSize }).width(120).onChange((v: string) => { this.blkSize = v; })
        Text('0 = 默认 512').fontSize(12).fontColor('#888')
      }.width('100%')
      Row({ space: 16 }) {
        Button('下载 (GET)').onClick(() => {
          this.busy = TftpdService.startClient(1, this.host, Number(this.port),
            this.remote, this.local, Number(this.blkSize));
        }).enabled(!this.busy)
        Button('上传 (PUT)').onClick(() => {
          this.busy = TftpdService.startClient(2, this.host, Number(this.port),
            this.remote, this.local, Number(this.blkSize));
        }).enabled(!this.busy)
      }
      Text('状态：' + this.st.state + '  块 ' + this.st.blk + '  ' +
           this.st.bytes + '/' + this.st.size + '  错误码 ' + this.st.rc)
        .fontSize(14)
      Text('MD5：' + this.st.md5).fontSize(14).fontColor('#0A59F7')
      Text(this.st.msg).fontSize(14).fontColor('#D81E06')
    }.width('100%').height('100%').padding(16).alignItems(HorizontalAlign.Start)
  }
}

@Component
export struct DirectoryPage {
  @State files: DirEntry[] = [];
  @State md5Text: string = '';
  @State baseDir: string = TftpdService.baseDir;

  aboutToAppear() {
    this.refresh();
  }

  private refresh() {
    this.files = TftpdService.listDir(this.baseDir);
  }

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('目录：' + this.baseDir).fontSize(16).fontWeight(FontWeight.Bold).layoutWeight(1)
        Button('刷新').onClick(() => { this.refresh(); })
      }.width('100%')
      List({ space: 2 }) {
        ForEach(this.files, (f: DirEntry) => {
          ListItem() {
            Row() {
              Text((f.dir === 1 ? '[DIR] ' : '') + f.name).fontSize(14).layoutWeight(1)
              Text(f.size.toString()).fontSize(12).fontColor('#888')
              Button('MD5').height(28).margin({ left: 12 }).onClick(() => {
                this.md5Text = f.name + ': ' + TftpdService.md5(this.baseDir + '/' + f.name);
              })
            }.width('100%')
          }
        }, (f: DirEntry, i: number) => i + f.name + f.size.toString())
      }.layoutWeight(1).width('100%')
      Text(this.md5Text).fontSize(12).copyOption(CopyOptions.LocalDevice)
    }.width('100%').height('100%').padding(12)
  }
}
```

- [ ] **Step 10.2: TftpdService 补 baseDir 与文本读写**

```ts
  static baseDir: string = '';

  static readText(path: string): string {
    try {
      const f = fs.openSync(path, fs.OpenMode.READ_ONLY);
      const stat = fs.statSync(path);
      const buf = new ArrayBuffer(stat.size);
      fs.readSync(f.fd, buf);
      fs.closeSync(f);
      return buffer.from(buf).toString('utf-8');
    } catch (e) { return ''; }
  }

  static writeText(path: string, text: string): void {
    const f = fs.openSync(path, fs.OpenMode.READ_WRITE | fs.OpenMode.CREATE | fs.OpenMode.TRUNC);
    fs.writeSync(f.fd, text);
    fs.closeSync(f);
  }
```

`baseDir` 在 `init(context)` 中赋值为 `<filesDir>/tftpboot`（与 M1 的 `setConfigPath` 同批调用）；imports 增加 `import { fileIo as fs, buffer } from '@kit.CoreFileKit';`

- [ ] **Step 10.3: Index.ets 增加 5 个页签**

在 `import` 处追加：

```ts
import { SyslogPage, SntpPage, DnsPage, ClientPage, DirectoryPage } from './M2Pages';
```

在现有 `Tabs` 的 `TabContent` 末尾、`Tabs.barPosition(BarPosition.Start)` 对应的 `TabBar` 构建器中按顺序追加（保持原 3 页不变）：

```ts
        TabContent() { SyslogPage() }.tabBar(this.TabBarItem('Syslog', 3))
        TabContent() { SntpPage() }.tabBar(this.TabBarItem('SNTP', 4))
        TabContent() { DnsPage() }.tabBar(this.TabBarItem('DNS', 5))
        TabContent() { ClientPage() }.tabBar(this.TabBarItem('客户端', 6))
        TabContent() { DirectoryPage() }.tabBar(this.TabBarItem('目录', 7))
```

`TabBarItem` 若原实现为 `this.tabBuilder('设置', 0)` 形式，沿用原方法名并只改索引；索引从 0 起共 8 项。

- [ ] **Step 10.4: 设置页补 WinSize 与 Syslog 文件（UI-04 遗留）**

在设置页 TFTP 分组追加：

```ts
          Row({ space: 8 }) {
            Text('WinSize（RFC7440）').width(180)
            TextInput({ text: this.winSize })
              .width(120)
              .onChange((v: string) => { this.winSize = v; })
          }.width('100%')
          Row({ space: 8 }) {
            Text('Syslog 日志文件').width(180)
            TextInput({ text: this.syslogFile })
              .layoutWeight(1)
              .onChange((v: string) => { this.syslogFile = v; })
          }.width('100%')
```

`aboutToAppear` 中初始化：`this.winSize = TftpdService.getStr('WinSize');`（默认为 "1"），`this.syslogFile = TftpdService.getStr('SaveSyslogFile');`；保存时 `TftpdService.setInt('WinSize', Number(this.winSize));`、`TftpdService.setStr('SaveSyslogFile', this.syslogFile);`。

说明：`WinSize` 在原版 v4.74 未参与 RFC7440 协商（OP-06 已实测两端一致忽略），此处仅补齐设置项，行为不改。

- [ ] **Step 10.5: 构建验证**

```powershell
& "D:\Program Files\Huawei\DevEco Studio\tools\node\node.exe" "D:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.js" --mode module -p module=entry@default -p product=default assembleHap
```

Expected: `BUILD SUCCESSFUL`，`entry-default-unsigned.hap` 产出。

- [ ] **Step 10.6: 提交**

```powershell
git add entry/src/main/ets/pages/M2Pages.ets entry/src/main/ets/pages/Index.ets entry/src/main/ets/services/TftpdService.ets
git commit -m "feat(ui): eight-tab layout with syslog/sntp/dns/client/directory pages"
```

---

### Task 11: 测试工具与 M2 用例执行

**Files:**
- Create: `tools/m2tool.c`
- Modify: `docs/Tftpd32-功能测试方案.md`

- [ ] **Step 11.1: 编写 m2tool.c**

```c
// m2tool.c —— 设备端 M2 协议测试工具
// 用法：m2tool syslog <ip> <port> <count> <len>
//       m2tool dns    <ip> <port> <qname> <qtype>
//       m2tool ntp    <ip> <port>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

static int udp_sock (const char *ip, int port, struct sockaddr_in *to)
{
int s;
struct timeval tv = { 3, 0 };

    memset (to, 0, sizeof *to);
    to->sin_family = AF_INET;
    to->sin_port   = htons ((unsigned short) port);
    inet_pton (AF_INET, ip, & to->sin_addr);
    s = socket (AF_INET, SOCK_DGRAM, 0);
    setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, & tv, sizeof tv);
    return s;
}

static int do_syslog (const char *ip, int port, int count, int len)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), i;
char buf [2048];

    for (i = 1; i <= count; i++)
    {
        int n = snprintf (buf, sizeof buf, "<134>Mar  1 12:00:00 m2test: message #%d ", i);
        while (n < len && n < (int) sizeof buf - 1)  buf[n++] = 'x';
        buf[n++] = '\n';
        sendto (s, buf, n, 0, (struct sockaddr *) & to, sizeof to);
        usleep (50000);
    }
    printf ("sent %d syslog messages (%d bytes each)\n", count, len);
    close (s);
    return 0;
}

static int do_ntp (const char *ip, int port)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), n;
unsigned char pkt [48];
unsigned long sec;

    memset (pkt, 0, sizeof pkt);
    pkt[0] = 0x1B;                       /* LI=0 VN=3 mode=3(client) */
    pkt[40] = 0xAB;  pkt[41] = 0xCD;     /* 随意的时间戳，用于 loop 检测 */
    sendto (s, pkt, 48, 0, (struct sockaddr *) & to, sizeof to);
    n = (int) recv (s, pkt, sizeof pkt, 0);
    if (n < 48)  { printf ("timeout / short reply %d\n", n);  close (s);  return 1; }

    sec = ((unsigned long) pkt[40] << 24) | ((unsigned long) pkt[41] << 16)
        | ((unsigned long) pkt[42] << 8) | pkt[43];
    printf ("reply %d bytes mode=%d stratum=%d xmt=%lu (unix %lu)\n",
            n, pkt[0] & 7, pkt[1], sec, sec - 2208988800UL);
    close (s);
    return 0;
}

static int do_dns (const char *ip, int port, const char *name, int qtype)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), n, i, pos;
unsigned char q [512], r [512];
const char *p = name;
unsigned id = 0x1234;

    memset (q, 0, sizeof q);
    q[0] = 0x12;  q[1] = 0x34;  q[2] = 0x01;  q[3] = 0x00;
    q[5] = 1;                                    /* qdcount = 1 */
    pos = 12;
    while (*p)
    {
        const char *dot = strchr (p, '.');
        int l = dot ? (int)(dot - p) : (int) strlen (p);
        q[pos++] = (unsigned char) l;
        memcpy (q + pos, p, l);  pos += l;
        p = dot ? dot + 1 : p + l;
    }
    q[pos++] = 0;
    q[pos++] = (unsigned char)(qtype >> 8);  q[pos++] = (unsigned char)(qtype & 0xFF);
    q[pos++] = 0;  q[pos++] = 1;

    sendto (s, q, pos, 0, (struct sockaddr *) & to, sizeof to);
    n = (int) recv (s, r, sizeof r, 0);
    if (n < 12)  { printf ("timeout / short reply %d\n", n);  close (s);  return 1; }

    printf ("reply %d bytes id=%02x%02x rcode=%d an=%d\n",
            n, r[0], r[1], r[3] & 0x0F, (r[6] << 8) | r[7]);

    pos = 12;
    while (pos < n && r[pos] != 0)  pos += r[pos] + 1;
    pos += 5;                                     /* 0 + qtype + qclass */

    for (i = 0; i < ((r[6] << 8) | r[7]) && pos + 12 <= n; i++)
    {
        if ((r[pos] & 0xC0) == 0xC0)  pos += 2;
        else { while (pos < n && r[pos] != 0)  pos += r[pos] + 1;  pos++; }
        {
            int type = (r[pos] << 8) | r[pos + 1];
            int rdlen = (r[pos + 10] << 8) | r[pos + 11];
            char txt [64] = "";
            if (type == 1 && rdlen == 4)
                snprintf (txt, sizeof txt, "%d.%d.%d.%d",
                          r[pos + 12], r[pos + 13], r[pos + 14], r[pos + 15]);
            else if (type == 28 && rdlen == 16)
                inet_ntop (AF_INET6, r + pos + 12, txt, sizeof txt);
            printf ("  type=%d ttl=%d rdata=%s\n", type,
                    (r[pos + 4] << 24) | (r[pos + 5] << 16) | (r[pos + 6] << 8) | r[pos + 7], txt);
            pos += 12 + rdlen;
        }
    }
    close (s);
    return 0;
}

int main (int argc, char **argv)
{
    if (argc < 4)  { printf ("usage: m2tool syslog|dns|ntp ...\n");  return 2; }
    if (strcmp (argv[1], "syslog") == 0 && argc >= 6)
        return do_syslog (argv[2], atoi (argv[3]), atoi (argv[4]), atoi (argv[5]));
    if (strcmp (argv[1], "ntp") == 0)
        return do_ntp (argv[2], atoi (argv[3]));
    if (strcmp (argv[1], "dns") == 0 && argc >= 6)
        return do_dns (argv[2], atoi (argv[3]), argv[4], atoi (argv[5]));
    printf ("bad arguments\n");
    return 2;
}
```

- [ ] **Step 11.2: 交叉编译并推送**

```powershell
$ndk="D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\native"
& "$ndk\llvm\bin\clang.exe" --target=x86_64-linux-ohos --sysroot="$ndk\sysroot" -fuse-ld=lld -O2 -o tools\m2tool.exe tools\m2tool.c
$env:HDC_SERVER_PORT="18710"; $h="D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe"
& $h -t 127.0.0.1:5555 file send tools\m2tool.exe /data/local/tmp/m2tool
& $h -t 127.0.0.1:5555 shell "chmod +x /data/local/tmp/m2tool"
```

Expected: 推送成功，`/data/local/tmp/m2tool` 可执行。

- [ ] **Step 11.3: 安装并启动应用**

按 M1 的签名命令签名后：

```powershell
& $h -t 127.0.0.1:5555 install -r C:\Users\32572\AppData\Local\Temp\tftpd_sign\entry-signed.hap
& $h -t 127.0.0.1:5555 shell "aa start -a EntryAbility -b com.example.myapplication"
```

- [ ] **Step 11.4: 逐条执行 M2 用例**

| 编号 | 操作 | 预期 |
|---|---|---|
| SL-01 | 设置页开 Syslog，保存；`m2tool syslog 127.0.0.1 5140 1 30` | Syslog 页签实时出现 1 条，含 `m2test` 与来源 127.0.0.1 |
| SL-02 | 设置 `SaveSyslogFile=<filesDir>/syslog.txt`；发 3 条后切到目录页刷新 | `syslog.txt` 存在且 size 随消息增长 |
| SL-03 | 发送非法报文（`<5` 字节，如 `echo -n hi`） | 无崩溃、无新条目，进程仍应答后续合法消息 |
| ST-01 | `m2tool ntp 127.0.0.1 1230` | 返回 48 字节，`mode=4`，`unix` 与设备时间偏差 ≤ 2s |
| ST-02 | `m2tool ntp 127.0.0.1 1230` 重复 20 次 | 每次均应答，无 loop-detection 误判 |
| ST-03 | 发送 3 字节 UDP 后立即再发合法请求 | 3 字节被忽略，合法请求正常应答 |
| DN-01 | DNS 页填写 `myhost 192.168.1.100 -` 保存；`m2tool dns 127.0.0.1 5300 myhost 1` | `rcode=0 an=1`，A 记录 192.168.1.100 |
| DN-02 | `m2tool dns ... nosuchhost 1` | `rcode=4 an=0`（与原版一致：无应答记录） |
| DN-03 | hosts 加 `myhost6 - ::1`；`m2tool dns ... myhost6 28` | AAAA 应答 `::1` |
| DN-04 | 发送超长标签（>63）构造包 | 无应答、无崩溃 |
| TC-01 | 客户端页 GET `10.0.2.2:69 test_1k.txt` → 本地 `client_dl.bin` | 状态 done，MD5 与 PC `certutil -hashfile` 一致 |
| TC-02 | PUT 本地文件到 PC 原版（需原版根目录可写） | done，PC 端文件 SHA256 与本地一致 |
| TC-03 | GET `192.0.2.1:69 nofile` | 超时后状态 error，rc=-1，UI 不卡死 |
| TC-04 | 传输中断开服务开关再开启 | 客户端不受影响（独立线程），服务恢复监听 |
| MD-01 | 目录页对 `test_1k.txt` 点 MD5 | 显示 32 位十六进制，与 PC `certutil -hashfile test_1k.txt MD5` 一致 |
| MD-02 | 设置页开 MD5 后做一次 TFTP PUT（PC→设备） | 传输完成后日志出现 `MD5: ...`（M1 已具备），与 PC 值一致 |
| MD-03 | 目录页刷新 | 文件/子目录、大小显示正确，`[DIR]` 前缀正确 |
| UI-04 | 设置页存在 `WinSize` 与 `Syslog 日志文件` 输入项，保存后重启应用值保留 | 值持久化 |
| UI-05 | 8 页签均可点击且内容非空，宽屏/窄屏均不溢出 | 布局正常 |

- [ ] **Step 11.5: 记录结果**

把每条用例的实测结果写入 `docs/Tftpd32-功能测试方案.md` 的新小节 `#### 6.5.1 执行记录` ~ `#### 6.9.1 执行记录`（按测试方案既有编号：6.5 客户端、6.6 Syslog、6.7 SNTP、6.8 DNS、6.9 MD5/目录）。格式沿用 §6.1.1 的表格：编号 / 应用版结果 / 原版结果 / 结论。原版对拍在 PC 上以 `D:\trae\tfpd32\tftpd64.exe` 完成（Syslog 514、SNTP 123、DNS 53，需管理员权限）。

- [ ] **Step 11.6: 提交**

```powershell
git add tools/m2tool.c docs/Tftpd32-功能测试方案.md
git commit -m "test(m2): protocol test tool and M2 case results"
```

---

## 验收标准

1. 8 页签全部可用；设置项 `WinSize`/`SaveSyslogFile`/三个端口持久化。
2. Syslog 5140 收到消息 → UI 实时显示 + 可选落盘。
3. SNTP 1230 对 NTP v3 client 请求返回正确时间（偏差 ≤2s）。
4. DNS 5300 静态 hosts 应答 A/AAAA；未知域名 `rcode=4`；畸形包不崩溃。
5. TFTP 客户端 GET/PUT 与原版互通，MD5 一致；超时/错误路径 UI 有明确状态。
6. 目录列表与 MD5 工具与原版 Directory 页签行为一致。
7. 服务开关按位独立启停，停止后端口释放（重复启停 5 次无 `bind` 失败）。
8. `git status` 干净，测试记录已并入测试方案。

---

## Self-Review 结论

- **覆盖检查：** 测试方案 §6.5（客户端 TC-01~03）、§6.6（Syslog SL）、§6.7（SNTP ST）、§6.8（DNS DN）、§6.9（MD5/目录 MD）、UI-04（WinSize 设置项）、UI-05（8 页签）均有对应 Task。
- **占位检查：** 无 TBD/TODO；所有新增文件给出完整代码；两个"按现有格式"的插入点（`tKeys[]` 条目、`TabBarItem` 名称）附有明确约束与示例。
- **类型一致性：** `PalServiceStartMask/StopMask`、`PalClientStart`、`PalComputeMd5`、`PalListDirectory`、`Pal_GetSettingStr` 在 PAL 声明、实现、NAPI 导出、Index.d.ts、ArkTS 封装五层签名一致；`ClientState` 字段与 `PalCliEmit` JSON 键一一对应。
