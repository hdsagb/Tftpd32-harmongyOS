# Tftpd32 功能梳理与测试方案
> 用途：验证鸿蒙适配版应用是否实现了原生 Tftpd32 的功能
> 基准：Tftpd64 v4.74 官方源码（`reference/tftpd64/`）+ 本机安装的原版程序（`D:\trae\tfpd32\tftpd64.exe`）
> 编写日期：2026-10-18

---

## 1. 目的

原生 Tftpd32 是一个约 17,600 行 C 代码的单进程多线程 UDP 服务套件。本方案先给出原生功能全景（§2），再据此建立**功能等价性测试用例集**（§6），用于逐项验证鸿蒙适配版是否达到"功能对齐原版"的目标。

测试总原则（对应适配方案 §3.1"兼容性强"原则）：

- **对拍测试**：同一输入分别发给原版（端口 69）与鸿蒙版（默认端口 6900），比对传输结果（内容哈希一致）、TFTP 错误码、日志行为是否一致。
- **报文级一致**：协议行为以 RFC 1350/2347/2348/2349/7440 + 原版实测响应为准。
- **配置键名一致**：配置键沿用原版注册表键名。

---

## 2. 原生 Tftpd32 功能全景（梳理结果）

### 2.1 服务矩阵

服务开关由配置项 `Services`（`sSettings.uServices` 位掩码）控制，见 `_common/custom.h:100`：

| 位 | 服务 | 默认端口 | 协议 | 实现文件 | 鸿蒙里程碑 |
|---|---|---|---|---|---|
| 0x0001 | TFTP 服务器 | 69/UDP | TFTP (RFC1350+选项) | tftpd_main.c / tftpd_thread.c | **M1（已实现）** |
| 0x0002 | TFTP 客户端 | — | TFTP | _gui/tftp_cli.c + tftpd_util.c | M2 |
| 0x0004 | DHCP 服务器 | 67/UDP | BOOTP/DHCP | bootpd.c / bootpd_util.c | M3 |
| 0x0008 | Syslog 服务器 | 514/UDP | Syslog | syslogd.c | M2 |
| 0x0010 | SNTP 服务器 | 123/UDP | NTP/SNTP | sntpd.c | M2 |
| 0x0020 | DNS 服务器 | 53/UDP | DNS | dns.c | M2 |
| 0x1000 | Telnet 控制台 | 可配端口 TCP | 文本命令 | to_console.c / console_actions.c | M3/M4 |

### 2.2 TFTP 服务器（功能核心）

**传输模式**：octet（二进制）、netascii（文本）。

**RFC 选项协商**（`Negociate` 开关，默认开）：
- blksize（RFC2348，块大小 8~65464）
- timeout（RFC2349，超时秒数）
- tsize（RFC2346，文件大小预告）
- windowsize（RFC7440，`WinSize` 配置，>1 时协商）
- port 选项（实验性，`Support for port Option`）

**安全级别**（`SecurityLevel`，enum e_SecurityLevels）：

| 值 | 名称 | 行为 |
|---|---|---|
| 0 | 无 | 不做路径校验，允许绝对路径读写 |
| 1 | 标准（默认） | 读写限制在根目录内（含子目录），拒绝 `..` 逃逸 |
| 2 | 高 | 仅允许根目录下的简单文件名（不允许子目录） |
| 3 | 只读 | 仅允许读，WRQ 一律拒绝（Access violation） |

**行为开关**（均对应注册表键，鸿蒙版同名配置键）：

| 键 | 功能 |
|---|---|
| BaseDirectory | 根目录 |
| TftpPort | 监听端口（原版默认 69） |
| Timeout / MaxRetransmit | 包超时秒数 / 最大重传次数（默认 3 / 6） |
| Negociate | 选项协商总开关 |
| WinSize | RFC7440 窗口大小 |
| ShowProgressBar | GUI 进度条 |
| DirText | RRQ 目标为目录时返回目录清单文本 |
| MD5 | 传输完成后计算并显示文件 MD5 |
| UnixStrings | 目录清单用 `\n`（Unix 风格） |
| PXECompatibility | PXE 客户端兼容模式 |
| VirtualRoot | 虚拟根目录（对客户端隐藏真实路径前缀） |
| Reduce TFTP Path | 剥离请求路径前导路径 |
| LowestUDPPort / HighestUDPPort | 数据传输临时端口范围 |
| Ignore ack for last TFTP packet | 忽略最后一块的 ACK（兼容某些 PXE） |
| Enable IPv4 / Enable IPv6 | 双栈开关 |
| Max Simultaneous Transfers | 最大并发传输数 |
| Keep transfer Gui | GUI 传输条目残留秒数 |
| TftpLogFile | TFTP 日志落盘 |

**错误码语义**（须与原版一致）：1=File not found，2=Access violation，3=Disk full，4=Illegal operation，5=Unknown transfer ID，6=File already exists，7=No such user。

**可靠性**：块计数器 16 位回绕（>32MB 文件）；超时重传；ACK 去重；最后一数据块（<blksize）识别。

### 2.3 DHCP 服务器（bootpd.c）

- 完整 DORA 流程（DISCOVER→OFFER→REQUEST→ACK）
- 地址池（IP_Pool 起始地址 + PoolSize）、掩码、网关、DNS/DNS2/WINS
- 启动文件（BootFile，配合 TFTP 做 PXE 网络引导，siaddr 自动填本机/TFTP 地址）
- Option42（NTP）、Option120（SIP）、DomainName、自定义选项（AddOptionNumber/Value ×2）
- 租约持久化（PersistantLeases，Lease_ 前缀键，含 MAC/IP/时间）
- UnicastBOOTP（单播应答）、DHCP Ping（分配前探测）、DHCP Double Answer（双重应答兼容）
- 租约展示与 GUI 续租/删除

### 2.4 DNS / SNTP / Syslog

- **DNS 服务器**（dns.c，53/UDP）：静态 A/AAAA 记录查询（GUI 中配置 主机名→IP 列表），支持 CNAME、拒绝超长标签（>63）、UDP 报文 ≤512B、格式校验。
- **SNTP 服务器**（sntpd.c，123/UDP）：应答 SNTP/NTP 请求，返回服务器时间戳。
- **Syslog 服务器**（syslogd.c，514/UDP）：接收并实时显示 syslog 消息；SaveSyslogFile 落盘；PipeSyslogMsg 管道模式。

### 2.5 客户端与工具

- **TFTP 客户端**（GUI 页签）：向任意 TFTP 服务器 get/put，可指定本地文件名、块大小、重试。
- **MD5 哈希工具**：对指定文件计算 MD5。
- **目录浏览**（GUI Directory 页签）：查看/管理根目录文件。
- **Telnet 控制台**：TCP 文本协议，口令默认 `tftpd32`（Console Password），支持状态查询等命令。

### 2.6 配置面

- 原版：注册表 `HKLM\SOFTWARE\TFTPD32`（DHCP 子键 `SOFTWARE\TFTPD32\DHCP`）。
- 鸿蒙版：等价 KEY=value 配置文件，**键名与原版注册表键完全相同**（见 §2.2 表与 settings.h `KEY_*` 宏），值语义一致。
- 服务器状态跨重启恢复（uRunningServices 除外）。

### 2.7 GUI 结构（原版）

页签：**Settings / TFTP Server / DHCP Server / DNS Server / Syslog Server / SNTP Server / TFTP Client / Directory**。
共性：全局服务开关、日志区（事件+传输）、最小化到托盘、开机自启（服务版）。

---

## 3. 测试策略

### 3.1 对拍基准环境

| 角色 | 环境 | 说明 |
|---|---|---|
| 基准 | Windows PC + `D:\trae\tfpd32\tftpd64.exe`（v4.74 便携版） | 管理员运行，绑定 69；根目录建议设为 `D:\trae\tfpd32\root`；防火墙放行 UDP 69 |
| 被测 | DevEco 模拟器（x86_64）/ 鸿蒙真机 | 默认端口 6900，需 PC 与模拟器网络直连（TFTP 为 UDP，`hdc fport` 仅支持 TCP） |
| 工具 | curl.exe（Windows 10 1803+ 自带，支持 tftp://）、Windows 可选功能 TFTP 客户端（tftp.exe，用于 netascii）、python3（构造 DHCP/Syslog 报文）、nslookup/w32tm | — |

### 3.2 等价性判据

1. **传输结果**：上传/下载完成后，两端文件 SHA256 一致。
2. **错误行为**：同一非法请求，两端返回的 TFTP Error Code 一致（§2.2 错误码语义）。
3. **选项协商**：客户端请求的选项在应答中是否接受/拒绝，两端一致。
4. **配置**：同一配置键（同名）在两端产生相同行为。
5. **UI**：功能项一一对应（允许布局差异，见 §5）。

### 3.3 通用操作

```powershell
# 启动原版基准（管理员）
D:\trae\tfpd32\tftpd64.exe

# 取模拟器 IP（应用日志页也应显示服务已启动）
hdc shell ifconfig

# 连通性检查（不通则模拟器切桥接网络或回退真机）
ping <模拟器IP>
```

测试文件准备（对拍前生成一次，两端根目录各放一份）：

```powershell
New-Item -ItemType Directory -Force D:\trae\tfpd32\root | Out-Null
$bytes = New-Object byte[] (10MB); (New-Object Random 1234).NextBytes($bytes)
[IO.File]::WriteAllBytes('D:\trae\tfpd32\root\test_10m.bin', $bytes)
Set-Content D:\trae\tfpd32\root\test_1k.txt ("Tftpd32 test`r`n" * 100)
[IO.File]::WriteAllBytes('D:\trae\tfpd32\root\test_1b.bin', 0x41)
```

---

## 4. 测试环境矩阵

| 编号 | 环境 | 覆盖范围 |
|---|---|---|
| E1 | DevEco 模拟器 + PC 对拍 | M1/M2 全部功能用例（DHCP 广播除外） |
| E2 | 鸿蒙真机 + PC 对拍 | E1 全部 + DHCP（桥接/热点局域网）+ 后台存活 |

---

## 5. 预期平台偏差（以下差异**不判为失败**）

| 原生行为 | 鸿蒙版行为 | 原因 |
|---|---|---|
| 监听 69/67/53 等 <1024 端口 | 默认 6900，可配置（<1024 端口无法绑定） | 应用沙箱禁止特权端口 |
| 注册表存储 | 配置文件（键名一致） | 平台无注册表 |
| Win32 GUI 8 页签 | ArkUI 页签（M1 为 设置/TFTP/日志 三页签，逐步补齐） | 界面忠实复刻但分里程碑交付 |
| 开机自启 / Windows 服务 | 长时任务常驻（taskKeeping） | 平台机制不同 |
| DHCP Ping（ARP/ICMP 探测） | 模拟器内无 raw socket，探测降级 | 平台无 ICMP 权限 |

---

## 6. 功能测试用例

优先级：**P0**=核心必须通过；P1=重要；P2=一般。
执行方式：每条用例先在原版执行记录基准结果，再在鸿蒙版执行比对。

### 6.1 TFTP 服务器基础传输（M1，E1/E2）

| 编号 | 优先级 | 用例 | 命令/步骤 | 预期（与原版一致） |
|---|---|---|---|---|
| TF-01 | P0 | 小文件下载（octet 1 字节） | `curl -o o1.bin tftp://<IP>:<port>/test_1b.bin` | 下载成功，SHA256 一致 |
| TF-02 | P0 | 文本文件下载（1KB） | `curl -o o2.txt tftp://<IP>:<port>/test_1k.txt` | 成功，内容一致 |
| TF-03 | P0 | 大文件下载（10MB） | `curl -o o3.bin tftp://<IP>:<port>/test_10m.bin` | 成功，SHA256 一致 |
| TF-04 | P0 | 上传（PUT，1KB） | `curl -T test_1k.txt tftp://<IP>:<port>/up_1k.txt` | 成功，两端文件一致 |
| TF-05 | P0 | 上传（PUT，10MB） | `curl -T test_10m.bin tftp://<IP>:<port>/up_10m.bin` | 成功，SHA256 一致 |
| TF-06 | P0 | 下载不存在文件 | `curl -v tftp://<IP>:<port>/nofile.bin` | TFTP Error code 1 (File not found)；curl 报 `TFTP error` |
| TF-07 | P1 | netascii 模式下载 | `tftp -h` 装好客户端后：`tftp <IP> <port> get test_1k.txt out.txt`（不带 `-i` 即 netascii） | 成功，文本语义一致 |
| TF-08 | P1 | netascii 模式上传 | `tftp <IP> <port> put test_1k.txt up.txt` | 成功 |
| TF-09 | P1 | 重复下载同文件（会话复用/状态清理） | 连续执行 TF-03 五次 | 每次均成功，日志页生成 5 条传输记录 |
| TF-10 | P1 | 覆盖上传已有文件 | 对 TF-04 的目标再 PUT 一次 | 与原版一致（原版默认允许覆盖，再次成功） |
| TF-11 | P0 | 并发传输（Max Simultaneous Transfers） | 同时发起 5 个 TF-03 下载 | 全部成功；传输页 5 个进度条并行推进 |

#### 6.1.1 执行记录（2026-10-20，DevEco 模拟器 MateBook Pro ↔ 本机原版 v4.74）

测试客户端 `otftp2`（NDK clang 编译，octet / netascii 可选），两侧同一客户端、同一请求。

| 编号 | 应用版（6900） | 原版（6901） | 结论 |
|---|---|---|---|
| TF-07 | netascii GET：1902B / 4 块 | 1902B / 4 块 | 一致，SHA256 三方相同 |
| TF-08 | netascii PUT 后回读 1902B | 同 | 一致，SHA256 三方相同 |
| TF-09 | 连续 5 次 10MB：641 块 ×5 | — | 全部成功，5 份 SHA256 相同 |
| TF-10 | 覆盖 PUT：1902B → 1B | 同 | 一致（默认允许覆盖，回读哈希相同） |
| TF-11 | 5 路并发 10MB，连续 3 轮 | — | 全部成功，每轮 5 份哈希相同；每轮后追加小文件 GET 正常 |

本轮修复缺陷：`PalCreatePipeEvent` 创建的唤醒管道为阻塞模式，主循环用
`while (read(...) > 0);` 清空管道时最后一次 read 永久阻塞，导致 TFTP 监听线程挂死。
串行传输复用常驻线程不触发唤醒，故 TF-09 无异常；并发传输创建/销毁临时线程时必现
（TF-11 后服务完全无响应，UI 报"应用无响应"）。改为两端 `O_NONBLOCK` 后修复。

### 6.2 RFC 选项协商（M1）

| 编号 | 优先级 | 用例 | 命令/步骤 | 预期 |
|---|---|---|---|---|
| OP-01 | P0 | blksize 16384 协商 | `curl --tftp-blksize 16384 -o o.bin tftp://<IP>:<port>/test_10m.bin` | OACK 接受 blksize=16384，传输成功（抓包可见 OACK 或凭速度提升判断） |
| OP-02 | P1 | blksize 8 下限 | `curl --tftp-blksize 8 -o o.bin tftp://<IP>:<port>/test_1k.txt` | 接受，成功 |
| OP-03 | P1 | blksize 65464 上限 | `curl --tftp-blksize 65464 -o o.bin tftp://<IP>:<port>/test_10m.bin` | 与原版行为一致（接受或按策略拒绝） |
| OP-04 | P1 | tsize 预告 | `curl -v --tftp-blksize 1024 tftp://<IP>:<port>/test_10m.bin` | 响应中包含 tsize=10485760 |
| OP-05 | P1 | timeout 协商 | `curl --tftp-blksize 512 -m 8 ...` 并人为制造丢包 | 重传节奏符合 Timeout=3s/MaxRetransmit=6 |
| OP-06 | P2 | windowsize（WinSize>1） | 原版设 WinSize=4；客户端 RRQ 携带 `windowsize=4` | v4.74 源码无 RFC 7440 实现，预期两端均忽略该选项 |
| OP-07 | P1 | 关闭 Negociate 后的请求 | 应用关掉"选项协商"，再执行 OP-01 | 客户端回退 512 块传输，仍成功 |
| OP-08 | P2 | 未知选项请求 | python 构造 RRQ 携带非标准选项 | 与原版一致：OACK 只回显已知选项（或忽略） |

#### 6.2.1 执行记录（2026-10-20，DevEco 模拟器 MateBook Pro ↔ 本机原版 v4.74）

测试客户端为自建 `otftp2`（NDK clang 编译，octet，逐项可选），两侧同一客户端、同一请求。

| 编号 | 应用版（6900） | 原版（6901） | 结论 |
|---|---|---|---|
| OP-01 | `blksize=16384`，641 块，10MB 成功 | `blksize=16384`，641 块，成功 | 一致，SHA256 三方相同 |
| OP-02 | `blksize=8`，1902B / 238 块 | `blksize=8`，32B / 5 块 | 一致（截面值相同，文件不同） |
| OP-03 | 65464 → 回落 `blksize=16384` | 65464 → 回落 `blksize=16384` | 一致 |
| OP-04 | `tsize=1902` | `tsize=1902` | **修复后**一致（见下） |
| OP-05 | `timeout=5`；`timeout=0` 被忽略 | `timeout=5`；`timeout=0` 被忽略 | 一致 |
| OP-06 | 忽略 windowsize | 忽略 windowsize（WinSize=4 时同样忽略） | 一致；源码确认 v4.74 未实现 RFC 7440 |
| OP-07 | Negociate=0 时不回 OACK，回落 512 | 同 | 一致，哈希相同 |
| OP-08 | 仅未知选项→无 OACK；未知+blksize→只回 blksize | 同 | 一致 |

本轮修复缺陷：`%I64d` / `%u`（MSVC 专有格式符，musl 下 tsize 输出空串）改为 `%lld`，
涉及 tftpd_thread.c 三处（OACK 的 tsize、日志两处）。

#### 6.2.2 组播执行记录（2026-10-22，C1 RFC2090，Pura 90 Pro 模拟器）

配置：设置页 `MulticastAddress=239.255.0.1`、`MulticastPort=6969`（键名与原版一致），
保存后启动服务，用设备内 `m2tool mcast` 发送带 `multicast` 选项的 RRQ。

| 项 | 实测 | 结论 |
|---|---|---|
| OACK 协商 | 服务端回 `OACK: <multicast=239.255.0.1,6969,>`，日志 `multicast option accepted : 239.255.0.1,6969` | 通过；RFC2090 选项值格式为 `<地址>,<端口>` |
| 组播发送 | 日志 `multicast transfer to 239.255.0.1:6969`；独立 socket 在 app 域 `SO_REUSEADDR` 绑定 6969 并 `IP_ADD_MEMBERSHIP` 成功 | 通过 |
| 单播回归 | `m2tool get` 下载 4096 字节，MD5 `e9a56f976c51945a8cb5093215ab42ec` 与源文件一致 | 通过 |
| 组播接收/ACK | `hdc shell` 域绑定 6969 被 SELinux 拒绝（EACCES），客户端无法加入组收取 DATA；OACK 协商仍可完成 | **阻塞**：需真机或非 shell 域客户端；同网段双客户端对拍随 A2/A3 |

组播日志样例（应用日志页）：
`multicast option accepted : 239.255.0.1,6969` → `OACK: <multicast=239.255.0.1,6969,>` → `multicast transfer to 239.255.0.1:6969`。

### 6.3 安全级别与路径（M1）

| 编号 | 优先级 | 用例 | 命令/步骤 | 预期 |
|---|---|---|---|---|
| SC-01 | P0 | 标准级：目录内读 | 根目录内 `curl ... /test_1b.bin` | 成功 |
| SC-02 | P0 | 标准级：`..` 逃逸被拒 | `curl -v "tftp://<IP>:<port>/../../windows/win.ini"` | Error code 2 (Access violation) |
| SC-03 | P0 | 只读级：WRQ 被拒 | 安全级别切"只读"后 PUT | Error code 2 |
| SC-04 | P0 | 只读级：RRQ 放行 | 只读级下载 | 成功 |
| SC-05 | P1 | 高级：子目录路径被拒 | 级别"高"，访问 `/sub/file` | Error code 2 |
| SC-06 | P1 | 无级：绝对路径 | 级别"无"，请求绝对路径文件 | 与原版一致（放行） |
| SC-07 | P1 | 子目录文件读写（标准级） | 根下建 `sub/`，读写 `/sub/test_1k.txt` | 成功 |

### 6.4 TFTP 异常与可靠性（M1）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| RE-01 | P1 | 客户端超时不响应 | 下载中拔网线/暂停客户端 >Timeout×Retransmit | 服务端放弃会话，日志记录超时，服务不崩溃 |
| RE-02 | P1 | 非法 TID（乱发包到数据端口） | 向传输中端口发垃圾 UDP 包 | 忽略或 Error 5 (Unknown transfer ID)，会话不受影响 |
| RE-03 | P1 | 未知操作码 | python 发 opcode=9 报文到 6900 | 静默丢弃/与原版一致，服务存活 |
| RE-04 | P1 | 32MB+ 文件（块号回绕） | 上传 >32MB 文件 | 成功（16 位块号回绕处理正确） |
| RE-05 | P2 | 畸形报文（短包/超长包） | python 发 10 字节、>512+blksize 包 | 不崩溃，行为与原版一致 |
| RE-06 | P0 | 启停循环 | 连续 启动→停止→启动 10 次 | 每次端口均可重新绑定成功（无句柄泄漏） |
| RE-07 | P1 | 传输中停止服务 | 大文件下载进行中点"停止" | 传输中止，应用不崩溃，重启后可正常服务 |

#### 6.4.1 执行记录（2026-10-20，DevEco 模拟器 MateBook Pro ↔ 本机原版 v4.74）

测试客户端为自建 `otftp2`（带 `abort=` / `delay=` / `junk` 扩展）与原始报文工具 `rtool`，两侧同一客户端、同一请求。

| 编号 | 应用版（6900） | 原版（6901） | 结论 |
|---|---|---|---|
| RE-01 | 重传间隔 0 / 1002 / 3005 / 6008 / 9012 / 12016 ms，6 次后发 Error code=0 | 0 / 1013 / 3021 / 6035 / 9050 / 12057 ms，6 次后发 Error code=0 | 一致（Timeout=3s、MaxRetransmit=6），服务存活 |
| RE-02 | 非法 TID 垃圾包静默丢弃，传输继续，哈希一致 | 同（未回 Error 5） | 一致 |
| RE-03 | opcode=9 → Error 4 "Illegal TFTP operation" | 同 | 一致，服务存活 |
| RE-04 | 34MB / blksize 512 / 69633 块（跨 16 位回绕），PUT→GET 哈希 `d1afa4a0…` | PUT 后宿主机文件哈希相同 | 一致 |
| RE-05 | ≤516B 回 Error 4；≥520B 静默丢弃 | 同 | **修复后**一致（见下） |
| RE-06 | 5 轮 + 10 轮混合负载启停：线程数 32→37 波动无单调增长，VmRSS 203→207MB 后稳定 | — | 无句柄/线程泄漏迹象，每次端口均可重绑 |
| RE-07 | 停服瞬间 6900 端口消失；停服期间新 RRQ 超时无应答；**既有传输不中断继续**；重启后端口恢复，GET 1B 成功 | 原版 `bSoftReset` 语义相同（停监听、不打断 worker） | 一致，应用不崩溃 |

本轮修复缺陷：Linux `recvfrom` 对超长报文静默截断并返回截断长度，而 WinSock 返回
EMSGSIZE（Rc<0）直接丢弃，导致 RE-05 长包行为不一致（应用回 Error 4、原版静默丢弃）。
[tftpd_main.c](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/tftpd_main.c#L326-L334)
改为 `MSG_TRUNC` 取真实长度，超过 `sizeof cnx_frame` 时按原版语义丢弃。

**回归（2026-10-21，构建 `2949414`，配置 6900/3/6）**：RE-01~RE-07 全部复跑通过，与原版基线无回归。

| 编号 | 回归结果 |
|---|---|
| RE-01 | 重传 0 / 1007 / 3016 / 6027 / 9035 / 12042 ms，随后 Error code=0（基线 0/1002/3005/6008/9012/12016，偏差 ≤30 ms） |
| RE-02 | 垃圾包静默丢弃（`JUNK_REPLY: none`），10MB 传输完成且 md5 与源一致 |
| RE-03 | opcode=9 → `00050004` Error 4 "Illegal TFTP operation" |
| RE-04 | 34MB PUT（块号回绕到 4097）→ GET 69633 块，md5 `044c6245…` 一致 |
| RE-05 | 516B 请求获 Error 1 应答；520B 静默丢弃（分辨率与原版一致） |
| RE-06 | 连续 10 轮 PUT+GET：线程 32→33，VmRSS 194.8→196.7MB，无单调泄漏迹象 |
| RE-07 | 传输中停服：监听端口立即消失、在途传输仍完整完成（md5 一致）、停服期间新请求超时无应答、重启后 20481 块 GET 成功 |

已知差异（M2 已消除）：应用 UI 当时尚无 WinSize 配置项；M2 已补设置项与持久化，见 §6.13.2。

### 6.5 TFTP 客户端（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| TC-01 | P1 | 从原版服务器 get | 客户端页签填 PC IP:69 下载 | 成功，文件出现在应用可读目录 |
| TC-02 | P1 | 向原版服务器 put | 上传 | 成功 |
| TC-03 | P2 | 目标不可达 | 填不存在 IP | 按 Timeout/Retransmit 重试后报错，不崩溃 |
| TC-04 | P2 | 传输中切换服务开关 | 大文件传输中关服务再开 | 在途传输不受影响，重启后端口恢复监听 |

#### 6.5.1 执行记录（2026-10-21，DevEco 模拟器，客户端页 ↔ 应用自带服务器 6900）

模拟器仅做 TCP 端口转发、UDP 不通（`10.0.2.2` 不可达），故 TC-01/TC-02 的“与原版 PC 互通”退化为设备内环回：应用客户端 ↔ 应用服务器，报文格式与块协商逻辑不变；与原版互通的完整对拍留到 M3 真机。

| 编号 | 操作 | 结果 | 结论 |
|---|---|---|---|
| TC-01 | 客户端页 GET `127.0.0.1:6900 hosts.txt` → 本地 `client_dl.bin` | `状态：done 块 1 37/37 错误码 0`，`client_dl.bin` 37 字节，MD5 与 PC `certutil` 一致 | 通过 |
| TC-02 | PUT 本地文件为远端 `up_hosts.txt` | `up_hosts.txt` 37 字节，MD5 与源文件一致，往返无差异 | 通过 |
| TC-03 | 端口改为 6999（无监听）后 GET | `状态：error 块 0 0/0 错误码 -1`，消息 `timeout`，界面不卡死 | 通过 |
| TC-04 | 34MB PUT（blksize 512，4097 块）进行中，点主服务开关关闭 → 探测无应答 → 再开启 → 探测有应答 | 停服前 `reply=28`（ERR 包）；停服中 `reply=none` 且客户端仍持有 OACK 继续传；重启后 `reply=28`；传输完成 `put ok: 35651584 bytes, 4097 blocks`，落盘文件 MD5 `044c6245…` 与源一致 | 通过 |

### 6.6 Syslog 服务器（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| SL-01 | P1 | 接收标准消息 | `python -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.sendto(b'<134>Mar 1 test: hello',('<IP>',514))"` | 日志区实时显示该条 |
| SL-02 | P2 | 落盘（SaveSyslogFile） | 配置日志文件后发多条 | 文件追加记录 |
| SL-03 | P2 | 超长/畸形消息 | 发 >SYSLOG_MAXMSG 字节 | 截断处理，不崩溃 |

#### 6.6.1 执行记录（2026-10-21，设备内环回，`m2tool syslog`）

| 编号 | 操作 | 结果 | 结论 |
|---|---|---|---|
| SL-01 | 设置页开启 Syslog（5140），`m2tool syslog 127.0.0.1 5140 1 30` | Syslog 页签实时逐条出现 `<134>Mar  1 12:00:00 m2test: message #N`，来源列 `127.0.0.1` | 通过 |
| SL-02 | `SaveSyslogFile=<filesDir>/tftpboot/device_syslog.txt`，发 3 条后查看文件 | 文件 237 字节，逐行 `Wed Oct 21 17:43:16 2026;127.0.0.1; <134>…`，与界面一致 | 通过（**修复缺陷**：键未登记，见 BUG-011） |
| SL-03 | 发 2 字节畸形报文 | 无新条目、无崩溃，`m2tool bad` 探测 `reply=none` 后继续发合法消息仍正常显示 | 通过 |

### 6.7 SNTP 服务器（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| ST-01 | P1 | NTP 查询应答 | `w32tm /stripchart /computer:<IP> /dataonly /samples:1` | 收到应答，偏差为两机时钟差（秒级合理） |
| ST-02 | P2 | 畸形 NTP 包 | 发 3 字节 UDP 包 | 丢弃，不崩溃 |

#### 6.7.1 执行记录（2026-10-21，设备内环回，`m2tool ntp`）

| 编号 | 操作 | 结果 | 结论 |
|---|---|---|---|
| ST-01 | `m2tool ntp 127.0.0.1 1230` | 应答 48 字节，`mode=4 stratum=10`，`unix` 与设备时间偏差 ≤2s | 通过 |
| ST-02 | 连续 20 次查询 | 20/20 均应答，无 loop-detection 误判、无丢包 | 通过 |
| ST-03 | 先发 20 字节畸形包，再发合法请求 | 畸形包无应答（`reply=none`），随后合法请求正常返回 48 字节，服务存活 | 通过 |

### 6.8 DNS 服务器（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| DN-01 | P1 | A 记录命中 | GUI 配置 `myhost→192.168.1.100`，`nslookup myhost <IP>` | 返回 192.168.1.100 |
| DN-02 | P2 | 未配置域名 | `nslookup nosuch <IP>` | NXDOMAIN/空应答，与原版一致 |
| DN-03 | P2 | AAAA 查询 | 配置 IPv6 地址后查询 | 返回 AAAA 记录 |
| DN-04 | P2 | 超长标签 | 构造 64 字符标签查询 | 静默丢弃，不崩溃 |

#### 6.8.1 执行记录（2026-10-21，设备内环回，DNS 页 hosts.txt + `m2tool dns`）

hosts.txt 内容：`myhost 192.168.1.100 -` / `myhost6 - ::1`。

| 编号 | 操作 | 结果 | 结论 |
|---|---|---|---|
| DN-01 | `m2tool dns 127.0.0.1 5300 myhost 1` | `reply 40 bytes rcode=0 an=1 type=1 ttl=300 rdata=192.168.1.100`（RDATA 恰 4 字节，符合 RFC1035） | 通过（**修复缺陷**：LP64 下 RDATA 长度错误，见 BUG-006） |
| DN-02 | `m2tool dns 127.0.0.1 5300 nosuch 1` | `rcode=4 an=0`，与原版“无记录即 NXDOMAIN”一致 | 通过 |
| DN-03 | `m2tool dns 127.0.0.1 5300 myhost6 28` | `reply 53 bytes rcode=0 an=1 type=28 ttl=300 rdata=::1` | 通过（**修复缺陷**：原版 AAAA 类型码误写 2，见 BUG-008） |
| DN-04 | 发送 64 字符标签查询（`timeout 5` 包裹） | 无应答（`rc=124`），与原版静默丢弃一致；随后 DN-01 仍正常，进程未崩溃 | 通过 |

补充：对畸形/不支持的查询（qtype 非 A/AAAA/CNAME）不再回发上一请求的应答，`anslen` 每轮复位（见 BUG-009）。

### 6.9 MD5 工具（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| MD-01 | P2 | 手动计算文件 MD5 | 对 test_1k.txt 计算 | 与 `certutil -hashfile test_1k.txt MD5` 一致 |
| MD-02 | P2 | 传输后自动 MD5（bMD5） | 开启 MD5 选项后传输 | 日志/界面显示的哈希与 certutil 一致 |
| MD-03 | P2 | 目录列表 | 目录页刷新 | 文件/子目录、大小显示正确 |

#### 6.9.1 执行记录（2026-10-21，目录页，MD5 按钮）

| 编号 | 操作 | 结果 | 结论 |
|---|---|---|---|
| MD-01 | 目录页对 `hosts.txt`（37 字节）点 MD5 | `hosts.txt: ecb492e43bb51bd9c2b6f0360bd83a60`，与 PC `certutil -hashfile hosts.txt MD5` 完全一致 | 通过（**修复缺陷**：LP64 下 UINT4 为 8 字节，见 BUG-007） |
| MD-02 | 设置页开 `MD5=1` 后由 PC 向设备 PUT | 未执行 | 受 E1 模拟器 UDP 不通限制，随 M3 真机 TC 对拍执行 |
| MD-03 | 目录页刷新 | 依次显示 `up_hosts.txt`、`client_dl.bin`、`hosts.txt`、`client_form.txt` 及大小，每行带 MD5 按钮 | 通过 |

### 6.10 DHCP 服务器（M3，E2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| DH-01 | P1 | DORA 完整流程 | 客户端机器接入同一二层网络，`ipconfig /release` → `ipconfig /renew`（或 scapy 发 DISCOVER） | 获得 Pool 内地址、掩码/网关/DNS 与配置一致 |
| DH-02 | P2 | 租约持久化 | 重启应用后查看租约 | 租约保留（Lease_ 键） |
| DH-03 | P2 | BootFile 下发 | 配置 BootFile | ACK 携带 file 字段与 siaddr |
| DH-04 | P2 | 地址池耗尽 | PoolSize=1，两个客户端请求 | 第一个成功，第二个被拒/等待，行为同原版 |
| DH-05 | P2 | 自定义选项 | AddOptionNumber=42 | ACK 携带 option 42 |

#### 6.10.1 执行记录（2026-10-21，DevEco 模拟器 Pura 90 Pro，回环单播）

环境：应用沙箱 `tftpd32.ini` 写入 `IP_Pool=192.168.1.100`、`PoolSize=10`、`Mask=255.255.255.0`、`Gateway=192.168.1.1`、`DNS=8.8.8.8`、`DNS2=1.1.1.1`、`BootFile=pxelinux.0`、`Option42=192.168.1.123`、`Option120=192.168.1.124`、`DhcpPort=6700`、`PersistantLeases=1`；服务经设置页开关启动（`Services` 含 `0x0004`，`/proc/net/udp` 可见 `1A2C` 绑定）。客户端为设备内 `m2tool dhcp`（`flags=0` → 服务器按单播应答，绕开模拟器不转发二层广播的限制）。

| 编号 | 步骤 | 结果 | 结论 |
|---|---|---|---|
| DH-01 | `m2tool dhcp discover 127.0.0.1 6700 02:11:22:33:44:66` → `dhcp request ... 192.168.1.103` | OFFER 320B：`op=2`、`yiaddr=192.168.1.103`，选项 53=2、1=255.255.255.0、3=192.168.1.1、6=8.8.8.8/1.1.1.1、51=172800s、54=10.0.2.15、58/59 续租与重绑定时间；REQUEST 回 ACK（53=5），租约表状态转“已确认”。10 个 MAC 依次获得 .100~.109，无重复、无越池 | 通过 |
| DH-02 | 全部 ACK 后 `aa force-stop` → 重启应用 | `Lease_NumLeases=10` 与 `Lease_<n>_MAC/_IP/_InitialOfferTime/_LeaseStartTime` 全在；DHCP 页租约表（10）恢复，8 条“已确认” + 2 条“已提议”（offer 未 ACK 的租约保留为已提议） | 通过 |
| DH-03 | 配置 `BootFile=pxelinux.0` | OFFER/ACK 的 `file=pxelinux.0`；`siaddr=0.0.0.0`。与原版一致：原版 `bootpd.c` 仅在请求 `siaddr==INADDR_NONE` 时才填最近服务器地址，普通 DISCOVER（`siaddr=0`）同样返回 0；服务器地址通过 option 54/66 下发（66=10.0.2.15） | 通过（与原版行为一致） |
| DH-04 | `PoolSize=10`，10 个 MAC 占满后再发 2 个 DISCOVER | 第 11/12 个客户端 3s 无应答（`timeout / no reply`），应用日志 `no more address or address previously allocated by another server`；随后 RELEASE 释放 .100、DECLINE 销毁 .102，释放出的地址立刻被新 DISCOVER 重新分配（yiaddr 分别 .102/.100） | 通过 |
| DH-05 | 配置 `Option42=192.168.1.123`、`Option120=192.168.1.124` | OFFER/ACK 携带 option 42（len 4，192.168.1.123）与 option 120（len 5，子选项 0 + 4 字节地址）；未配置的 WINS 不下发 option 44（len 0） | 通过 |
| 附加 | `dhcp release` / `dhcp decline` | RELEASE：租约保留但清空分配/续租时间，状态转“空闲”；DECLINE：校验 option 50 与租约地址一致后销毁条目，`Lease_NumLeases` 由 10 降为 9 | 通过（与原版 `ZeroAllocTime`/`DHCPDestroyItem` 逻辑一致） |

说明与遗留：
- 回环自测覆盖报文语义（选项编码、地址分配/回收、持久化），**未覆盖**二层广播/多网卡选路，后者需真机 + 同网段 PC（A3 / 本用例真机部分）。
- 测试工具缺陷（非应用缺陷）：`m2tool dhcp decline` 最初把请求地址写进了 BOOTP `yiaddr` 字段，而 `ProcessDHCPMessage` 见到 `yiaddr!=0` 会直接丢弃报文（原版逻辑），导致 DECLINE 看似无效；改为只带 option 50 后正常。已在 `tools/m2tool.c` 注释标注。
- 租约数减少后 `tftpd32.ini` 会残留旧的 `Lease_9_*` 键（写键不删键），加载以 `Lease_NumLeases` 为准，不影响行为，与原版私有配置文件写法的表现一致。


### 6.11 Telnet 控制台（M3/M4）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| CN-01 | P2 | 口令登录 | `nc <IP> <consolePort>` 输入 `tftpd32` | 进入命令界面 |
| CN-02 | P2 | 状态命令 | 执行 STATUS 类命令 | 输出与服务状态一致 |

**实现决策（2026-10-22，C5）：不做（架构差异）。** 核对原版源码 `reference/tftpd64/src/_services/to_console.c`：
原版所谓的“控制台”是 GUI 与服务进程之间的 IPC 通道（监听 `TFTPD32_TCP_PORT`=13576，用 `Console Password`
做 `TcpExchangeChallenge` 挑战应答，报文类型为 `C_START` / `C_TFTP_WRQ_SETTINGS` / `C_DHCP_RRQ_SETTINGS` 等），
并非可交互的 Telnet 命令服务器。鸿蒙版为单进程（ArkUI + NAPI 直接回调），GUI 与服务共享同一内存，
不存在需要跨进程同步的状态，也没有 STATUS 类命令接口；若按名字新建 Telnet 服务将是原版没有的新功能，
违背“界面/行为等价”目标。因此 CN-01/CN-02 关闭为 N/A，`Console Password` 键保持未接线（与 B6 Ping 冲突检测同类）。

### 6.12 配置兼容性（M1 起）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| CF-01 | P0 | 键名一致 | 取原版配置基线（注册表 `HKLM\SOFTWARE\TFTPD32`，便携模式则为安装目录 `tftpd32.ini`）；对比应用配置文件 | §2.2 表中每个键在应用配置中同名存在 |
| CF-02 | P0 | 值语义一致 | 两端设 Timeout=5、Retransmit=9、Port 各自合法值 | 行为一致（重传节奏） |
| CF-03 | P1 | 配置持久化 | 应用改端口→保存→杀进程→重启 | 配置恢复，服务按新端口启动 |
| CF-04 | P1 | 非法输入防御 | 端口填 99999/0、超时填 0 | 与原版一致（拒绝/回落默认），不崩溃 |

#### 6.12.1 执行记录（2026-10-21，DevEco 模拟器 ↔ 本机原版 v4.74 便携版）

本机原版为便携模式，未写注册表（`HKLM\SOFTWARE\TFTPD32` 不存在），实际配置基线为其目录下的 `tftpd32.ini`（键名与注册表值名一致），故 CF-01 以该文件比对。

| 编号 | 步骤 | 结果 | 结论 |
|---|---|---|---|
| CF-01 | 把原版 `tftpd32.ini` 原样（含 `[TFTPD32]` 段头、`Donot verify firewall` / `LastWindowPos` 等仅 Windows 键）写入应用沙箱配置路径，重启应用 | 端口 6920、超时 4、重传 8 全部生效；`Enable IPv6=0` 正常；未知键静默忽略，不报错 | 通过。键名逐项同名（见下“键覆盖”），配置可直接迁移 |
| CF-02 | 两端 Timeout=5 / MaxRetransmit=9，收 2 块后停止 ACK | 应用 0 / 2005 / 5013 / 10018 / 15027 / 20032 / 25050 ms；原版 1 / 2019 / 5034 / 10043 / 15050 / 20051 / 25063 ms | 通过，节奏一致 |
| CF-03 | UI 改端口 6910、超时 5、重传 9 →「保存配置」→ `aa force-stop` → 重启 | 界面回填 6910/5/9，服务按 6910 监听 | 通过（**修复缺陷**：原实现启动不加载配置、保存按钮不写原生设置） |
| CF-04 | 端口 99999、超时 0、重传 0 → 保存 → 启动服务 | 超时/重传回落默认 3/6；端口 99999 绑定失败，服务保持“已停止”，进程存活无崩溃 | 通过（回落默认，与原版 `Port==0` 回落默认端口语义一致） |

键覆盖对照（原文 `tftpd32.ini` → 应用配置文件）：`BaseDirectory`、`SecurityLevel`、`Timeout`、`MaxRetransmit`、`TftpPort`、`WinSize`、`Negociate`、`PXECompatibility`、`DirText`、`ShowProgressBar`、`MD5`、`UnixStrings`、`Beep`、`VirtualRoot`、`LocalIP`、`Services`、`TftpLogFile`、`LowestUDPPort`、`HighestUDPPort`、`Max Simultaneous Transfers`、`Reduce TFTP Path`、`Ignore ack for last TFTP packet`、`Keep transfer Gui`、`Enable IPv6`、`Support for port Option` 均同名读写。本轮补入 `Enable IPv6`、`Support for port Option` 两个键。

仍未接线的键（归属后续里程碑）：`PipeSyslogMsg`（M2 Syslog 事件转发，已由 `console_pal.c` 事件通道承担）、`MulticastPort` / `MulticastAddress`（M2 TFTP 组播）、`PersistantLeases` / `DHCP Ping` / `DHCP LocalIP`（M3 DHCP）；`Hide` / `UseEventLog` / `Console Password` / `LastWindowPos` / `Donot verify firewall` 为 Windows GUI/平台相关项，不适用。M2 已补 `SaveSyslogFile` 落盘（见 §6.6.1），`WinSize` 已加 UI 设置项（见 §6.13.2）。

保存时应用只写自己的键集合，原版独有键会在重写时被丢弃（值本身已迁移，不影响行为）。

### 6.13 UI 等价性（随里程碑）

| 编号 | 优先级 | 用例 | 预期 |
|---|---|---|---|
| UI-01 | P0 | 服务开关 | 切换开关→服务启停，状态文字同步 |
| UI-02 | P0 | 传输进度 | 传输中进度条、字节数、重传计数实时刷新 |
| UI-03 | P1 | 日志实时性 | 每次传输/错误在日志页实时出现 |
| UI-04 | P1 | 设置项映射 | 原 Settings 页的每个设置项在应用中存在对应控件（分里程碑补齐） |
| UI-05 | P2 | 页签完整性 | 最终覆盖原版 8 页签对应功能 |

#### 6.13.1 执行记录（2026-10-21，构建 `2949414` + 本轮 UI 修复构建）

| 编号 | 结果 | 实测 |
|---|---|---|
| UI-01 | 通过 | 点开关 → `netstat` 出现 6900 监听、状态文字"运行中"；再点 → 端口消失、"已停止" |
| UI-02 | 通过（修复 4 处） | 传输中行显示 `#467 <v_up.txt` + `127.0.0.1:41758 12:19:34` + `1536/1902 字节 ret=0 80%`，进度条随字节增长；结束后变 `#467 .v_up.txt` + `1902/1902 字节 100%`，行按 `Keep transfer Gui`（默认 10s，与原版 settings.c 一致）保留，超时后自动清除 |
| UI-03 | 通过 | 切到日志页签，PUT 34MB（69633 块/9s）、GET 4 块、GET 不存在文件（Error 1）三类事件逐条实时追加并自动滚底；日志格式与原版一致（`sent N blks, M bytes in S s. R blk resent`） |
| UI-04 | 部分通过 | 已映射：端口、根目录、安全级别、超时、重传次数、选项协商（键名同原版）；缺 WinSize（M2） |
| UI-05 | 结构等价 | 原版 7 个页签（Tftp Server / Tftp Client / DHCP / Syslog / SNTP / DNS / Log viewer）中，Tftp Server→"TFTP"、Log viewer→"日志"、Settings 对话框→"设置"；其余 5 个服务页签随对应服务在 M2/M3 补齐 |

UI-02 修复明细（均在 §9 缺陷模板登记为 BUG-002~BUG-005）：

| 缺陷 | 现象 | 根因 | 修复 |
|---|---|---|---|
| BUG-002 | 传输行文件名消失 | stat 帧整表覆盖且不含文件名 | stat 刷新按 `id` 保留 `trf` 帧送来的文件名 |
| BUG-003 | 时长恒为 0s | ForEach key 未含 `dur`，值变不触发重渲染 | key 加入 `file/peer/bytes/ret/done/rc` |
| BUG-004 | `ret` 恒 0 | stat 帧误用 `ret_code`（运行中恒 0） | 改用 `dwTotalTimeOut`（原版 FD_TIMEOUT 列） |
| BUG-005 | 结束后行立即消失、无 100%/STPD/ERR | 缺少 `C_TFTP_TRF_END` 转发 | `console_pal.c` 新增 `end` 事件，前端保留行并按 `Keep transfer Gui` 秒数清除 |

#### 6.13.2 执行记录（2026-10-21，M2 补齐 WinSize 与 8 页签）

| 编号 | 结果 | 实测 |
|---|---|---|
| UI-04 | 通过（修复 2 处缺陷） | 设置页新增 `WinSize（RFC7440）` 与 `Syslog 日志文件` 两个输入项；`WinSize=2`、`SaveSyslogFile=…/device_syslog.txt` 保存后 `aa force-stop` → 重启，界面回填 `2` 与完整路径，配置文件同名键存在（BUG-010 WinSize 读取、BUG-011 键未登记） |
| UI-05 | 通过 | 8 个页签均可点击且非空：设置 / TFTP / 日志 / Syslog / SNTP / DNS / 客户端 / 目录；3200×2080 与窄屏下 `barMode(Scrollable)` 不溢出 |

原 UI-04 的“缺 WinSize”差异已消除；WinSize 仅做配置持久化与设置项映射，块窗口协商本身原版 v4.74 未实现（见 §5 平台偏差），故不涉及协议行为变化。

#### 6.13.3 执行记录（2026-10-22，阶段 C：多网卡 / 2in1 / 常驻通知）

| 项 | 环境 | 实测 | 结论 |
|---|---|---|---|
| C2 多网卡绑定 | Pura 90 Pro | 「绑定网卡（LocalIP）」下拉列出「全部接口」与 `10.0.2.15`；选择具体地址后 ini 写入 `LocalIP=10.0.2.15`，服务在 6900 启动，从该地址 GET 4096 字节 MD5 `e9a56f97…` 与源一致；切回「全部接口」写空值 | 通过 |
| C3 自由窗口 | MateBook Pro（2in1，3120×2080） | 应用以自由窗口启动，标题栏含最大化/最小化/关闭；`Meta+←` 分屏后窗口占半屏（≈515vp），设置页各行完整、无截断，内容区 `Scroll` 可达保存按钮；9 个页签在半宽下仍全部可见 | 通过 |
| C3 键盘操作 | MateBook Pro | `Tab`（keyCode 2049）在页签间移动焦点（焦点框依次右移），`Enter`（2054）激活当前焦点页签（设置→日志）；文本框可键入 | 通过 |
| C3 窄屏页签 | Pura 90 Pro | 9 个页签 `BarMode.Scrollable` 横向滚动，不挤压换行 | 通过（M2 已验，本轮回归） |
| C4 长时任务 | Pura 90 Pro | `TASK_KEEPING` 返回 9800005（模拟器不支持）；降级路径：服务启动成功后发布普通常驻通知 | 平台降级，行为可接受 |
| C4 常驻通知 | Pura 90 Pro | 首次发布返回 1600004（应用通知未启用）；接入 `requestEnableNotification` 弹出授权，允许后重发成功，通知栏显示「Tftpd32 / 服务运行中（端口 6900）」；停止服务后通知消失 | 通过 |
| C3 窗口标题 | MateBook Pro | 修复前标题栏与桌面图标显示 `label`（`EntryAbility_label` 为工程默认值）；改为 `Tftpd32`（`app_name`/`module_desc` 同步）后标题栏立即显示正确；桌面已存在的快捷方式标签由启动器缓存，卸载重装后显示 `Tftpd32` | 通过（BUG-014） |

设置页同时新增「后台运行/省电策略」说明：服务运行期间申请长时任务，若系统提示耗电，引导用户在“设置 > 电池 > 应用耗电管理”允许后台活动。

### 6.14 非功能（E1/E2）

| 编号 | 优先级 | 用例 | 预期 |
|---|---|---|---|
| NF-01 | P1 | 启动时间 | 冷启动→服务可接受请求 ≤2s |
| NF-02 | P1 | 内存占用 | 挂 10 个并发传输时内存 <100MB |
| NF-03 | P1 | 体积 | 安装包与 .so 保持轻量（当前 HAP≈2.8MB / libtftpd32.so≈180KB，与原版 exe 同数量级） |
| NF-04 | P1 | 长稳 | 持续挂机 24h（模拟器）无内存持续增长、无崩溃 |
| NF-05 | P1 | 后台存活 | （真机 M4）退后台 8h 后服务仍可达 |
| NF-06 | P2 | 吞吐 | 模拟器内 10MB 下载耗时不劣于原版在本机的 5 倍 |

#### 6.14.1 LP64 审计执行记录（2026-10-21）

审计范围：`entry/src/main/cpp` 全部产品代码（PAL、tftpd 服务、客户端、工具层），模式包括
`sizeof(long)`、`%ld/%lu/%lx`、`%I64d`、`long` 用作定宽、`unsigned long` 参与协议/时间运算、`LARGE_INTEGER` 布局、`time_t`/`size_t` 格式化。

| 项 | 位置 | 问题 | 处理 | 结论 |
|---|---|---|---|---|
| A6-01 | [pal_compat.h](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/pal_compat.h#L43-L44) | `typedef long LONG`：Win32 为 32 位，LP64 下 8 字节，使 `LARGE_INTEGER` 变成 16 字节、`HighPart` 覆盖 QuadPart 高半部 | 改为 `typedef int LONG` | 已修（编译通过，读写回归正常） |
| A6-02 | [pal_compat.h](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/pal_compat.h#L50) | 自定义 `typedef unsigned long u_long` 与 musl `sys/types.h` 冲突（重定义错误） | 删除自定义 typedef，改用系统定义（项目未使用） | 已修 |
| A6-03 | [dns.c](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/dns.c#L94) / [dns_hosts.c](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/dns_hosts.c#L9) | `DNS_ANSWER_EXP.ipv4` 与 `PalDnsHostsLookup` 用 `long` 承载 4 字节网络序地址，依赖小端低 4 字节，可移植性差 | 改为 `unsigned int`，删除 dns_hosts 中的 `long tmp` 位移拷贝 | 已修（DN-01/03 复测一致） |
| A6-04 | [sntpd.c](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/sntpd.c#L83-L105) | NTP 时间戳常量与 `time(NULL)` 用 `unsigned long`，LP64 下 8 字节，依赖 `htonl` 隐式截断 | 显式 `(u_int32_t)` | 已修（xmt 与 unix 时间差仍为 2208988800） |
| A6-05 | [tftpd_thread.c](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/tftpd_thread.c#L762-L768) | 依赖 `LARGE_INTEGER.LowPart/HighPart`，A6-01 修复前 `HighPart=0` 会连带清零 QuadPart 的 32..63 位 | 随 A6-01 修正布局；代码本身只使用低 32 位，与原版注释一致 | 无行为变化 |
| A6-06 | [global.h](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/libs/md5/global.h#L20-L24) | `UINT4` 原为 `unsigned long`，破坏 MD5 模 2^32 运算（M2 的 BUG-007） | 改为 `uint32_t` | 已修并在本轮复测 |
| A6-07 | `bootpd_util.h:111` | `unsigned long AddrInc` 在 LP64 下为 8 字节（当前未参与编译） | 留待 M3 DHCP 移植时改为 `unsigned int` | 待办（B1） |
| A6-08 | 全局 | 未发现 `%ld/%lu`、`%I64d`、`sizeof(long)` 的活跃使用；64 位量已统一 `%lld` + `long long` | — | 通过 |

复测（模拟器 x86_64，同为 LP64）：
`DN-01 reply 40 bytes type=1 rdata=192.168.1.100`、`DN-03 reply 53 bytes type=28 rdata=::1`、
`ST reply 48 bytes xmt=4001567403 (unix 1792578603)`、TFTP GET/PUT 37 字节往返 MD5 `ecb492e4…` 一致、
目录页 MD5 `hosts.txt: ecb492e43bb51bd9c2b6f0360bd83a60` 与 PC 一致。

#### 6.14.2 真机基线回归（阶段 A）执行状态

| 项 | 状态 | 说明 |
|---|---|---|
| A1 构建→签名→安装 | 已完成 | `hvigorw assembleHap` → `hap-sign-tool sign-app`（OpenHarmony.p12 + debug-profile）→ `hdc install -r`；模拟器验证通过 |
| A2 同网段组网 | 未执行 | 当前 `hdc list targets` 仅 `127.0.0.1:5555`（模拟器）与 COM 串口，无可用真机 |
| A3 与原版 v4.74 报文对拍（TC-01/02、SL、ST、DN、MD-02） | 阻塞 | 需真机 + 同网段 PC；模拟器仅转发 TCP，UDP 不可达 |
| A4 后台 8h 存活（NF-05） | 阻塞 | 需真机 |
| A5 吞吐（NF-06） | 阻塞 | 需真机；模拟器仅可作为趋势参考 |

真机到位后的执行顺序：`hdc tconn <IP>:5555`（或 USB 调试）→ 安装签名 HAP → 启动并开启四服务 →
PC 侧 `tftpd32.exe` 与 `m2tool`/`cf` 工具对拍 → 退后台 8h 探测 → 10MB GET/PUT ×5 记录吞吐。

---

## 7. 通过标准

- **P0 用例 100% 通过**（含全部对拍一致）。
- **P1 用例通过率 ≥95%**，未通过项须有缺陷记录且不属于协议正确性问题。
- P2 用例结果记录归档，不阻塞版本发布。
- 任何"崩溃/无响应/端口无法重绑"类问题，无论用例级别一律 P0 缺陷。

## 8. 里程碑映射

| 里程碑 | 用例集 | 环境 |
|---|---|---|
| M1（当前） | TF/OP/SC/RE/CF/UI-01~03/NF-01~03 | E1 模拟器 |
| M2 | TC/SL/ST/DN/MD + M1 回归 | E1 |
| M3 | DH/CN + M1~M2 回归 | E2 真机 |
| M4 | NF-05 后台存活 + 全量回归 | E2 真机 |

## 9. 缺陷记录模板

| 编号 | 用例 | 现象 | 原版行为 | 鸿蒙版行为 | 复现步骤 | 严重级 | 状态 |
|---|---|---|---|---|---|---|---|
| BUG-001 | TF-06 | ... | Error code 1 | 无响应 | ... | P0 | Open |

### 9.1 已修复（UI 等价性轮次）

| 编号 | 用例 | 现象 | 原版行为 | 鸿蒙版行为（修复前） | 复现步骤 | 严重级 | 状态 |
|---|---|---|---|---|---|---|---|
| BUG-002 | UI-02 | 传输行丢失文件名 | 文件名列常驻 | stat 帧整表刷新后文件名变空 | 发起传输 → 等一个刷新周期 → 切 TFTP 页签 | P1 | Fixed |
| BUG-003 | UI-02 | 时长恒 0s | 时长逐秒递增 | 状态已变但行不重渲染 | 传输中连续 dumpLayout，`dur` 不变 | P1 | Fixed |
| BUG-004 | UI-02 | `ret` 恒 0 | FD_TIMEOUT 列显示重传次数 | 误取 `ret_code`（运行中恒 0） | 制造重传后查看行文本 | P1 | Fixed |
| BUG-005 | UI-02 | 结束后行立即消失、无 100%/STPD/ERR | 保留 nGuiRemanence 秒并显示终态 | 未转发 `C_TFTP_TRF_END`，且 `nGuiRemanence` 默认写成 0 | 完成/中断一次传输后观察列表 | P1 | Fixed |

补充：本轮将 `nGuiRemanence` 默认值由 0 校正为原版 settings.c 的 10（"Keep transfer Gui" 键随配置读写，迁移原版 ini 后行为一致）。

### 9.2 已修复（M2 服务与工具轮次）

| 编号 | 用例 | 现象 | 原版行为 | 鸿蒙版行为（修复前） | 根因 | 修复 | 严重级 | 状态 |
|---|---|---|---|---|---|---|---|---|
| BUG-006 | DN-01 | A 记录 RDATA 长度为 0 | 4 字节地址 | DNS 应答无有效 A 地址 | 原版用 `sizeof(long)` 作 RDATA 长度并写入 8 字节 `long`，LP64 下 RDLENGTH=8，违反 RFC1035 | [dns.c](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/cpp/tftpd/dns.c) 固定 `htons(4)` 并只拷贝 4 字节 | P0 | Fixed |
| BUG-007 | MD-01 | MD5 摘要与 PC 不一致（`574f9779…` vs `ecb492e4…`） | 标准 MD5 | 同一文件摘要不同 | `libs/md5/global.h` 以 `unsigned long` 定义 `UINT4`，LP64 下为 8 字节，破坏模 2^32 运算 | 改为 `uint32_t` | P1 | Fixed |
| BUG-008 | DN-03 | AAAA 查询无应答 | 返回 AAAA 记录 | `myhost6 28` 被过滤 | 原版 `DNS_AAAA` 误定义为 2（标准/`nslookup` 用 28） | 定义改为 28，并兼容接受 2 | P2 | Fixed |
| BUG-009 | DN-04 | 畸形/不支持查询会收到上一次的应答 | 静默丢弃 | 回发过期报文 | `anslen` 为循环外变量，未生成应答时保留旧值 | 每轮 `anslen=0`，且 `anslen>0` 才 sendto | P1 | Fixed |
| BUG-010 | UI-04 | WinSize 保存 2，重启后界面显示 1 | 回填 2 | 回填默认 1 | 整型键用 `Pal_GetSettingStr` 读取，类型不匹配返回空 | [Index.ets](file:///d:/Users/3257219077/DevEcoStudioProjects/Tftpd32-harmongyOS/entry/src/main/ets/pages/Index.ets) 改用 `getInt` | P1 | Fixed |
| BUG-011 | SL-02 / UI-04 | `SaveSyslogFile` 保存后配置文件中无该键 | 键持久化 | 重启后丢失 | `settings_pal.c` 的 `tKeys[]` 未登记 `KEY_SYSLOGFILE` | 补登记（键名 `SaveSyslogFile`） | P2 | Fixed |
| BUG-012 | CF-01 | 迁移原版 ini 后根目录拼出 `ENOENT` | 正常解析 | 路径尾部带换行 | 读 ini 时值尾部 CR/LF 未裁剪 | 解析后裁掉 CR/LF/空白 | P1 | Fixed |
| BUG-013 | UI-05 / DN-01 | DNS 页保存 hosts.txt 崩溃（13900002） | 正常保存 | 应用退出 | `TftpdService.writeText` 未捕获文件异常、目录不存在 | 加 try/catch、自动建目录、记录 `lastError` | P1 | Fixed |

M2 同时确认的平台限制（非缺陷）：E1 模拟器只转发 TCP，TFTP/Syslog/SNTP/DNS 的 UDP 对拍只能在设备内环回（TC-01/02、SL、ST、DN 均按此方式执行），与原版 PC 的报文级对拍随 M3 真机执行；`MD-02`（传输自动 MD5）同此。

### 9.3 已修复（阶段 C 轮次）

| 编号 | 用例 | 现象 | 原版行为 | 鸿蒙版行为（修复前） | 根因 | 修复 | 严重级 | 状态 |
|---|---|---|---|---|---|---|---|---|
| BUG-014 | UI-01 / UI-05 | 2in1 窗口标题、桌面图标名显示 `label` | 标题为 `Tftpd32 v4.74` | 显示资源占位符 `label` | `AppScope/string.json` 只有 `app_name`，`entry` 的 `EntryAbility_label` 为工程模板值 `label`，且 `app_name` 为模板名 | `AppScope` 改为 `Tftpd32`；`EntryAbility_label`、`module_desc` 同步为 `Tftpd32` / `Tftpd32 for HarmonyOS` | P1 | Fixed |

阶段 C 的平台限制（非缺陷）：模拟器 `TASK_KEEPING` 返回 9800005，长时任务降级为普通常驻通知；`hdc shell` 域禁止 bind 显式 UDP 端口，组播接收侧与 DHCP 二层广播对拍仍需真机。
