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

已知差异（计入 M2）：应用 UI 尚无 WinSize 配置项，配置文件 `WinSize` 键可读但界面不可改。

### 6.5 TFTP 客户端（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| TC-01 | P1 | 从原版服务器 get | 客户端页签填 PC IP:69 下载 | 成功，文件出现在应用可读目录 |
| TC-02 | P1 | 向原版服务器 put | 上传 | 成功 |
| TC-03 | P2 | 目标不可达 | 填不存在 IP | 按 Timeout/Retransmit 重试后报错，不崩溃 |

### 6.6 Syslog 服务器（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| SL-01 | P1 | 接收标准消息 | `python -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.sendto(b'<134>Mar 1 test: hello',('<IP>',514))"` | 日志区实时显示该条 |
| SL-02 | P2 | 落盘（SaveSyslogFile） | 配置日志文件后发多条 | 文件追加记录 |
| SL-03 | P2 | 超长/畸形消息 | 发 >SYSLOG_MAXMSG 字节 | 截断处理，不崩溃 |

### 6.7 SNTP 服务器（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| ST-01 | P1 | NTP 查询应答 | `w32tm /stripchart /computer:<IP> /dataonly /samples:1` | 收到应答，偏差为两机时钟差（秒级合理） |
| ST-02 | P2 | 畸形 NTP 包 | 发 3 字节 UDP 包 | 丢弃，不崩溃 |

### 6.8 DNS 服务器（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| DN-01 | P1 | A 记录命中 | GUI 配置 `myhost→192.168.1.100`，`nslookup myhost <IP>` | 返回 192.168.1.100 |
| DN-02 | P2 | 未配置域名 | `nslookup nosuch <IP>` | NXDOMAIN/空应答，与原版一致 |
| DN-03 | P2 | AAAA 查询 | 配置 IPv6 地址后查询 | 返回 AAAA 记录 |

### 6.9 MD5 工具（M2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| MD-01 | P2 | 手动计算文件 MD5 | 对 test_1k.txt 计算 | 与 `certutil -hashfile test_1k.txt MD5` 一致 |
| MD-02 | P2 | 传输后自动 MD5（bMD5） | 开启 MD5 选项后传输 | 日志/界面显示的哈希与 certutil 一致 |

### 6.10 DHCP 服务器（M3，E2）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| DH-01 | P1 | DORA 完整流程 | 客户端机器接入同一二层网络，`ipconfig /release` → `ipconfig /renew`（或 scapy 发 DISCOVER） | 获得 Pool 内地址、掩码/网关/DNS 与配置一致 |
| DH-02 | P2 | 租约持久化 | 重启应用后查看租约 | 租约保留（Lease_ 键） |
| DH-03 | P2 | BootFile 下发 | 配置 BootFile | ACK 携带 file 字段与 siaddr |
| DH-04 | P2 | 地址池耗尽 | PoolSize=1，两个客户端请求 | 第一个成功，第二个被拒/等待，行为同原版 |
| DH-05 | P2 | 自定义选项 | AddOptionNumber=42 | ACK 携带 option 42 |

### 6.11 Telnet 控制台（M3/M4）

| 编号 | 优先级 | 用例 | 步骤 | 预期 |
|---|---|---|---|---|
| CN-01 | P2 | 口令登录 | `nc <IP> <consolePort>` 输入 `tftpd32` | 进入命令界面 |
| CN-02 | P2 | 状态命令 | 执行 STATUS 类命令 | 输出与服务状态一致 |

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

仍未接线的键（归属后续里程碑）：`SaveSyslogFile` / `PipeSyslogMsg`（M2 Syslog）、`MulticastPort` / `MulticastAddress`（M2 TFTP 组播）、`PersistantLeases` / `DHCP Ping` / `DHCP LocalIP`（M3 DHCP）；`Hide` / `UseEventLog` / `Console Password` / `LastWindowPos` / `Donot verify firewall` 为 Windows GUI/平台相关项，不适用。

保存时应用只写自己的键集合，原版独有键会在重写时被丢弃（值本身已迁移，不影响行为）。

### 6.13 UI 等价性（随里程碑）

| 编号 | 优先级 | 用例 | 预期 |
|---|---|---|---|
| UI-01 | P0 | 服务开关 | 切换开关→服务启停，状态文字同步 |
| UI-02 | P0 | 传输进度 | 传输中进度条、字节数、重传计数实时刷新 |
| UI-03 | P1 | 日志实时性 | 每次传输/错误在日志页实时出现 |
| UI-04 | P1 | 设置项映射 | 原 Settings 页的每个设置项在应用中存在对应控件（分里程碑补齐） |
| UI-05 | P2 | 页签完整性 | 最终覆盖原版 8 页签对应功能 |

### 6.14 非功能（E1/E2）

| 编号 | 优先级 | 用例 | 预期 |
|---|---|---|---|
| NF-01 | P1 | 启动时间 | 冷启动→服务可接受请求 ≤2s |
| NF-02 | P1 | 内存占用 | 挂 10 个并发传输时内存 <100MB |
| NF-03 | P1 | 体积 | 安装包与 .so 保持轻量（当前 HAP≈2.8MB / libtftpd32.so≈180KB，与原版 exe 同数量级） |
| NF-04 | P1 | 长稳 | 持续挂机 24h（模拟器）无内存持续增长、无崩溃 |
| NF-05 | P1 | 后台存活 | （真机 M4）退后台 8h 后服务仍可达 |
| NF-06 | P2 | 吞吐 | 模拟器内 10MB 下载耗时不劣于原版在本机的 5 倍 |

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
