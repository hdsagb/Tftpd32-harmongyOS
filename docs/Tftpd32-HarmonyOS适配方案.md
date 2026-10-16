# Tftpd32 鸿蒙（HarmonyOS PC & 手机）适配方案

> 目标：将 Tftpd32（原生 Windows 版）完整能力移植到 HarmonyOS PC 与手机，**尊重原生软件的特点——体积小、界面简洁、开箱即用**。
> 原生版本基准：Tftpd64 v4.74（官方仓库 https://github.com/PJO2/tftpd64 ，Tftpd32 为同一套代码的 32 位构建）。
> 源码已克隆至本工程 `reference/tftpd64/`，共约 **17,600 行 C 代码**（GUI 约 6,300 行，服务核心约 6,900 行，公共/库约 3,500 行）。
> 本工程当前为 DevEco Studio 5 新建工程，目标 HarmonyOS 6.1.1（API 24），runtimeOS = HarmonyOS。

---

## 1. 原生 Tftpd32 功能全景梳理

### 1.1 总体架构

Tftpd32 是一个**单进程多线程的 UDP 服务器套件**，一窗多页签，"小而全"：

```
┌────────────────────────── tftpd32.exe / tftpd64.exe ──────────────────────────┐
│  GUI 线程（Win32 对话框 + 标签页 + 托盘图标）                                  │
│      ↑↓ 控制台线程（线程间消息总线 to_console.c，GUI 版↔服务版走 TCP+密码）     │
│  ┌────────┬────────┬───────┬───────┬────────┬─────────┐                       │
│  │ DHCP   │  TFTP  │  DNS  │  SNTP │ Syslog │ (客户端) │  ← 每服务一个线程      │
│  └────────┴────────┴───────┴───────┴────────┴─────────┘                       │
│  辅助线程：Registry(设置异步落盘) / Scheduler(30s 轮询网卡、回收线程)            │
└───────────────────────────────────────────────────────────────────────────────┘
  另有服务版 tftpd32_svc.exe（注册为 Windows 服务，GUI 版仅作监视器）
```

线程配置表见 `src/_services/start_threads.c`（DHCP:67、TFTP:69、SNTP:123、DNS:53、Syslog:514，均 UDP；TFTP 支持 IPv4/IPv6 双栈）。

### 1.2 功能清单（按服务）

**TFTP 服务器**（`_services/tftpd_main.c`、`tftpd_thread.c`，核心约 3,700 行）
- RFC 1350 基本 RRQ/WRQ，RFC 2347/2348/2349 选项协商：`tsize`、`blksize`（8–16384 字节）、`timeout`；实验性 `udpport` 选项
- 自有"窗口发送"机制（设置项 WinSize，预先连发多块再等 ACK，见 tftpd_thread.c L579/L750），吞吐优化
- 安全级别 4 档（`enum e_SecurityLevels`）：None / Standard（限根目录树）/ High（写操作文件必须已存在）/ Read-only
- 根目录（Base Directory）绑定指定网卡 IP；虚拟根目录（VirtualRoot）；路径缩减（Reduce TFTP Path）
- 目录列表：`bDirText` 时把目录内容作为文本文件返回（目录浏览）
- 传输完成后计算 MD5（可选）
- 断点续传（Resume，写请求追加模式）、Unix 风格字符串兼容（bUnixStrings）
- PXE 兼容模式（禁 blksize 协商）、忽略最后一块 ACK（bIgnoreLastBlockAck）
- v4.74 支持 >4GB 大文件（64 位长度 `qwTotalBytes`）
- 数据通道动态端口范围（LowestUDPPort–HighestUDPPort）、最大并发传输数
- IPv4/IPv6 独立开关，双栈监听（IPV6_V6ONLY 关闭）；传输日志文件

**DHCP 服务器**（`_services/bootpd.c`、`bootpd_util.c`，约 3,700 行）
- 地址池 + 掩码/网关/DNS1/DNS2/WINS/域名/租约时长；BootFile（PXE 引导文件名）
- 自定义选项：Option 42（NTP）、Option 120（SIP）、用户自定义选项号/值
- 静态租约（MAC→IP 手工绑定）、租约持久化（注册表 Lease_* 键）
- 分配前 Ping 冲突检测（bPing）、双应答模式（bDoubleAnswer）、单播 BOOTP（bUnicastBOOTP）
- 绑定指定本机 IP（DHCP LocalIP）

**DNS 服务器**（`_services/dns.c`，约 340 行）
- 响应 A / AAAA / CNAME 查询，**数据来源是 DHCP 租约表中的主机名**（与 DHCP 联动）

**SNTP 服务器**（`_services/sntpd.c`，约 190 行）：标准 SNTP/NTP 应答（端口 123）

**Syslog 服务器**（`_services/syslogd.c` + `async_log.c`）：UDP 514 接收、GUI 实时显示、落盘文件、Windows 命名管道转发（`\\.\pipe\Tftpd32Syslog`）

**TFTP 客户端**（`_gui/tftp_cli.c`，约 1,000 行）：GET/PUT、主机地址+端口、本地/远程路径、blksize 下拉选择、完整路径开关、进度显示

**辅助能力**
- 设置持久化：注册表 `HKLM\SOFTWARE\TFTPD32`（也可 ini 模式，`setinifilename.c`）
- 控制台/远程管理：TCP 控制端口 + 密码（默认 "tftpd32"），服务版与 GUI 版以此通信；命令行参数（`cmd_line.c`）
- 网卡轮询与热插拔检测（Scheduler 每 30s `PoolNetworkInterfaces`）、Windows 防火墙检测与提示（`firewall.c`）
- Windows 服务版安装/管理（`service stuff.c`）

### 1.3 界面特点（必须保留的神韵）

- **单窗口、标签页导航**：Settings / TFTP Server / DHCP Server / DNS Server / SNTP Server / Syslog Server / Log Viewer + About 对话框
- 全部为紧凑的原生控件：列表、下拉、复选、进度条（传输速率/进度 gauge），无任何皮肤与装饰
- 常驻托盘图标（隐藏到托盘）、当前传输实时列表
- 整个 GUI 约 6,300 行 C，无框架无依赖——"轻"是产品特性

### 1.4 Windows 专有依赖清单（移植工作量所在）

| 依赖 | 用途 | 涉及模块 |
|---|---|---|
| WinSock2 / IPHlpApi | UDP 双栈 socket、`GetAdaptersAddresses` 枚举网卡 | 全部服务 |
| Win32 GUI（对话框/ListView/CommCtrl/资源脚本） | 全部界面 | `_gui/`、`_libs/GUI/` |
| 注册表 API | 设置与 DHCP 租约持久化 | `_libs/registry/` |
| `_beginthread`/`CreateEvent`/`WaitForMultipleObjects` | 线程与事件同步 | `_services/`、`start_threads.c` |
| `CreateFile`/`SetFilePointer` | 日志文件 | syslogd 等 |
| ICMP API（IcmpSendEcho，`ping_api.c`） | DHCP 分配前冲突检测 | DHCP |
| Service API（SCM） | 服务版安装/启停 | `_main/`、`_services/sdk_Service.c` |
| 命名管道 | Syslog 转发 | syslogd.c |

其余部分（协议编解码、租约算法、TFTP 状态机、链表/工具库、MD5）均为**可移植纯 C**。

---

## 2. 鸿蒙平台能力评估（PC & 手机）

| 能力 | 鸿蒙现状 | 结论 |
|---|---|---|
| C/C++ 代码复用 | NDK 支持 C/C++ 源码经 CMake 编译为 .so，经 Node-API（NAPI）与 ArkTS 互调；musl libc 提供完整 POSIX（`sys/socket.h`、`pthread`、`getaddrinfo` 等） | ✅ 协议核心可**近乎原样编译** |
| UDP 双栈 socket | NDK 原生 socket 支持 AF_INET/AF_INET6/双栈；ArkTS Network Kit 也有 UDP socket | ✅ |
| 特权端口（<1024） | 官方文档明确：应用不允许使用 1024 以下知名端口 | ⚠️ 69/67/53/123/514 默认端口**不可绑定**，必须做端口映射 |
| 后台常驻 | 退后台若申请长时任务（continuous task）可免挂起；未申请长时任务的应用退后台会**主动断开 TCP/UDP 连接**（手机/平板）；PC/2in1 支持 TASK_KEEPING 类型 | ⚠️ 必须申请长时任务，PC 上最宽松 |
| 原始套接字/ICMP | 应用无 raw socket 权限（PC 上二进制程序可申请 NET_RAW 但需 sudo，非普通应用形态） | ⚠️ DHCP 的 ICMP Ping 冲突检测需降级 |
| 网卡枚举 | NDK `getifaddrs`（musl 提供）可用；ArkTS 侧亦有网络连接管理 API | ✅ |
| 文件访问 | 应用沙箱；可通过 DocumentViewPicker 由用户授权目录（可持久化授权） | ⚠️ 根目录需走用户选择，语义与原生一致 |
| UI | ArkUI（ArkTS）声明式，Tabs/List/Dialog/Progress 组件齐备，PC/手机自适应 | ✅ 可 1:1 复刻标签页布局 |
| Windows 服务 | 无对应概念 | 用长时任务 + 可选开机自启（受限）替代 |

**平台策略**：HarmonyOS PC（2in1）是主力场景（网络工具类应用在 PC 上限制最少、可绑定网卡多、窗口自由缩放）；手机/平板为次场景（提供完整功能但遵守后台与端口管控）。

---

## 3. 适配总体策略

### 3.1 四条原则

1. **协议核心原样移植**：TFTP/DHCP/DNS/SNTP/Syslog 的 C 实现（`_services/`、`_common/`、`_libs/`）不做重写，仅做平台抽象层（PAL）适配——保证与原生 Tftpd32 **行为逐字节一致**，方便与原版对拍测试。
2. **兼容性强**：对所有外部世界保持最大兼容——(a) 与**原生 Tftpd32 及任意标准 TFTP 客户端**（Windows tftp、curl、busybox、PXE ROM）报文级互通，不改协议行为；(b) 设置**键名与语义沿用原版**（`BaseDirectory`、`WinSize`、`Lease_*` 等），老用户零学习成本；(c) 默认端口不可用时提供**可配置端口**，但绝不私自改动协议字段与错误码；(d) 配置文件格式可导入导出，便于从原版迁移；(e) PC 与手机行为一致，不因平台裁剪协议能力。
3. **界面忠实复刻**：ArkUI 复刻"单窗多页签 + 紧凑列表"的原版布局，不引入花哨设计；默认窗口小、启动即服务（TFTP 默认开启）。
4. **轻量交付**：不引入任何重型框架；native 层只产出一个小体积 .so（预计 <300KB），应用整体 HAP 控制在 MB 级，对齐原版"体积小"的特点。

### 3.2 目标架构

```
┌─────────────────────────── entry (HAP) ────────────────────────────┐
│  ArkUI（ArkTS）                                                    │
│   ├─ Index.ets：Tabs{ 设置 | TFTP | DHCP | DNS | SNTP | Syslog | 日志 }│
│   ├─ TftpClient.ets：TFTP 客户端页（复用 native 客户端逻辑）           │
│   ├─ Dialogs：关于/错误提示（对应 CMsgBox）                           │
│   └─ ServiceManager：长时任务申请、权限、网卡信息（ArkTS API）          │
│            ↑↓ NAPI（事件回调：日志、传输进度、服务状态）                │
│  ┌──────────────── native: libtftpd.so（C，CMake/NDK）─────────────┐ │
│   │  tftpd_main / tftpd_thread / bootpd / dns / sntpd / syslogd   │ │
│   │  + PAL（平台抽象层，本次新写，~800 行 C）：                     │ │
│   │    pal_thread/pal_event（pthread+cond） pal_net（BSD socket）  │ │
│   │    pal_cfg（Preferences 映射注册表语义） pal_fs pal_log pal_if  │ │
│   └────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────┘
```

**关键设计：PAL（Platform Abstraction Layer）**。原生代码对 Windows 的调用点集中且语义清晰（见表 1.4），用一组 `pal_*` 函数替换：

| 原生调用 | PAL 替换（鸿蒙） |
|---|---|
| `socket/sendto/recvfrom/select/getaddrinfo` | 同名 POSIX 函数（musl 一致，多数零修改） |
| `_beginthread`/`CreateEvent`/`WaitForMultipleObjects` | `pthread_create` + `pthread_cond_t` 事件对象；等待组用 epoll/poll 或条件变量数组 |
| `GetAdaptersAddresses`（ip_util.c） | `getifaddrs()` 枚举 IP/MAC；网卡变化由 ArkTS 侧监听后经 NAPI 通知 |
| 注册表 `registry.c` | 同名 API 语义封装到 Preferences/JSON 文件（键名不变，如 `BaseDirectory`、`WinSize`、`Lease_xxx`） |
| `CreateFile/SetFilePointer` 日志 | POSIX `open/lseek/write`（沙箱路径） |
| `IcmpSendEcho`（ping_api.c） | 降级为 UDP 探测（向目标发送 UDP 报文等待 ARP 失败不可行时直接跳过检测，见 §5 风险） |
| Service API（服务版） | 长时任务（PC 用 TASK_KEEPING；手机用对应类型）+ NAPI 通知 ArkTS 维持常驻 |
| 命名管道（Syslog pipe） | 本地 Unix socket 或暂不支持（GUI 文件/界面路径已覆盖核心场景） |
| Win32 GUI 全部 | 删除，由 ArkUI 承接；C 侧仅保留消息结构体经 NAPI 上报 |

`_gui/` 目录不参与 native 编译，仅作为 UI 逻辑参考（设置项、页签语义逐一映射）。

### 3.3 NAPI 接口设计（保持"服务与界面解耦"，与原版控制台线程同构）

ArkTS ↔ native 之间仅三类消息，对应原版 `to_console.c` 的消息总线：

- **下行（ArkTS → C）**：`startService(mask)` / `stopService(mask)` / `updateSettings(json)` / `tftpClient(req)` / `getLeases()` / `getTransferList()`
- **上行（C → ArkTS，事件回调）**：`onLog(level, msg)`（对应 LogToMonitor/SVC_ERROR）、`onTransferProgress(struct)`（对应 GUI gauge 刷新）、`onServiceChanged(mask, status)`（对应 C_CHG_SERVICE）
- **设置**：C 侧结构与 ArkTS 侧用同一份键名 JSON，`sSettings` 序列化/反序列化保持字段一致

---

## 4. 界面复刻方案（尊重"简洁"）

原版一窗多页签 → ArkUI `Tabs`，不做导航栏、不做侧滑抽屉：

| 原版页签 | ArkUI 页面 | 控件映射 |
|---|---|---|
| Settings | 设置 Tab（分组复选/下拉/输入框，与原版字段一一对应） | CheckBox/TextInput/Select/TextArea |
| TFTP Server | 当前传输 List（文件名、对端、进度条、速率）+ 根目录选择 | List + Progress（对应 gui_gauges.c） |
| DHCP Server | 租约表 List + 分配统计 | List（对应 gui_bootpd.c） |
| DNS/SNTP/Syslog | 各自记录 List | List（对应 gui_dns/sntpd/syslogd.c） |
| Log Viewer | 全局日志 List（等宽字体） | List + Scroll（对应 gui_log.c） |
| TFTP 客户端（原版独立对话框） | 客户端 Tab：主机/端口/本地/远程/GET/PUT | TextInput/Button（对应 tftp_cli.c） |
| About 对话框 | AlertDialog（含 GPLv2 声明） | AlertDialog |

交互细节同样保留：服务开关即改即生效（对应原版 Services 复选 + 线程热重启 `Tftpd32UpdateServices`）；托盘图标在手机上以常驻通知代替（长时任务通知），PC 上最小化到任务栏/托盘。

---

## 5. 关键风险与对策（务必先读）

| # | 风险 | 影响 | 对策 |
|---|---|---|---|
| R1 | **应用禁止绑定 1024 以下端口** | TFTP(69)/DHCP(67)/DNS(53)/SNTP(123)/Syslog(514) 默认端口不可用 | 所有服务端口可配置；默认映射为非特权端口（TFTP 6900、DHCP 6700、DNS 5300、SNTP 1230、Syslog 5140），设置页显著提示。协议兼容性不受影响（客户端可指定端口）；标准端口场景（PXE 等）在应用沙箱内无法实现，文档明示 |
| R2 | **退后台 socket 被断开**（手机/平板） | 服务停摆 | 服务开启期间自动申请长时任务；PC/2in1 使用 TASK_KEEPING；对用户明示省电策略提示 |
| R3 | 无 raw socket/ICMP | DHCP 分配前 Ping 冲突检测不可用 | 降级实现：默认关闭冲突检测并提示；保留双应答/静态租约/租约持久化等纯 UDP 能力 |
| R4 | 沙箱文件系统 | TFTP 根目录无法任意访问 | 根目录经 DocumentViewPicker 用户选择并持久化授权；默认使用应用沙箱 `files/tftpboot`（对齐原版"开箱即用"） |
| R5 | `WaitForMultipleObjects`/事件语义差异 | 移植引入并发 bug | PAL 单点实现 + 压力测试（原版 `#ifdef _DEBUG` 有同步压力测试代码可参考）；TFTP 传输状态机保持逐行对照 |
| R6 | 网卡热插拔/多网卡选择 | 绑定指定 IP 失效 | `getifaddrs` + ArkTS 网络监听双通道；Scheduler 轮询逻辑保留 |
| R7 | Syslog 管道转发无对应物 | 小功能缺失 | 改为可选"转发到本地 UDP"或裁剪；文件落盘与界面显示不受影响 |
| R8 | 许可合规 | 法律风险 | 仓库声明 **GPLv2**，衍生作品必须以 GPLv2 开源并在"关于"页保留原作者 Philippe Jounin 版权与声明 |

---

## 6. 分期实施计划

### 里程碑 1 —— TFTP 服务器先行（核心价值）
1. 建 `entry/src/main/cpp/`，接入 CMake；拷贝 `_services`、`_common`、`_libs`（registry/linked_list/md5/scandir/lasterr/log）源码
2. 实现 PAL：pal_thread/pal_event/pal_cfg/pal_fs/pal_net/pal_log（以最小集通过编译）
3. 移植 `tftpd_main.c`/`tftpd_thread.c`：RRQ/WRQ、OACK（tsize/blksize/timeout）、安全级别、>4GB、传输列表上报
4. ArkUI：设置页（端口/根目录/安全/blksize 等核心项）+ TFTP 页 + 日志页；长时任务接入
5. **验收**：与原生 Tftpd32/Windows tftp 客户端/curl `--tftp-blksize` 对拍，双向传输、大文件、异常重传一致

### 里程碑 2 —— 全服务补齐
6. Syslog、SNTP、DNS（含 DHCP 租约表联动接口先留空）、TFTP 客户端
7. MD5、目录列表、断点续传、窗口发送（WinSize）、PXE 兼容等次级开关全量对齐
8. 设置页全量字段（对照 settings.h `S_Tftpd32Settings` 逐项核对）
9. **验收**：settings.h 全字段可用；Syslog/SNTP 协议测试工具对拍

### 里程碑 3 —— DHCP 与平台适配
10. bootpd 移植：地址池/静态租约/租约持久化/自定义选项（Ping 检测降级见 R3）
11. DNS 与 DHCP 租约联动；IPv6 全链路（双栈监听、AAAA）
12. PC 形态优化：自由窗口、键盘操作、多网卡绑定 UI；手机形态：常驻通知
13. **验收**：租约生命周期对拍；PC/手机真机长时间后台运行不挂起

### 里程碑 4 —— 打磨与交付
14. 体积优化（.so strip、release 构建）、启动即服务（恢复原版 out-of-the-box 行为）
15. 错误提示中文化引导（端口占用、权限、防火墙对应提示）
16. 应用市场材料、GPLv2 源码公开仓库
17. **验收**：功能清单（§1.2）逐项打勾；HAP 体积报告；真机（PC+手机）冒烟

---

## 7. 测试与验收基线

- **协议一致性**：所有报文级测试用原生 Tftpd32 作为参照系（同请求同响应）
- **客户端矩阵**：Windows `tftp`、curl TFTP、busybox tftp、PXE ROM（仅自定义端口场景）
- **并发/压力**：≥50 并发传输、blksize 8/512/16384 边界、1MB–6GB 文件、超时重传
- **平台项**：后台长时任务存活 8h、网卡切换重绑、权限拒绝降级路径、PC 自由窗口多尺寸 UI
- **回归**：每次合入跑 NDK 单元测试（租约算法、OACK 解析等纯 C 部分可写 gtest）

---

## 8. 结论

该适配**可行且工作量可控**：原生 17.6k 行代码中约 11k 行（协议核心+工具库）可经 PAL 直接编译复用，行为与原版逐字节一致；真正的新增工作是 PAL（约 800–1,000 行 C）、NAPI 桥（约 300 行）与 ArkUI 界面（约 2,000–3,000 行 ArkTS）。硬性平台约束只有两条需要明示用户：**非特权端口**（R1）与**后台常驻策略**（R2）；DHCP 的 Ping 冲突检测是唯一功能降级点。最终产物在体积与界面上完全对齐原版"小、快、简"的气质。
