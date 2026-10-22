# Tftpd32 for HarmonyOS：真机测试清单（E2）

> 汇总**只能在鸿蒙真机上完成**（或必须用真机才能得到有效结论）的测试项。
> 模拟器已完成的部分见 [Tftpd32-功能测试方案.md](Tftpd32-功能测试方案.md) §6 各执行记录与 §9.4。
> 基线：代码 `294b25a`、文档 `69d1ae3`（阶段 D 完成时）。

## 1. 为什么必须上真机

| 模拟器限制 | 影响的验证项 |
|---|---|
| 仅转发 TCP（`hdc fport`），UDP 无法与 PC 互通；`10.0.2.2` 不可达 | 所有 PC ↔ 设备的 UDP 对拍（TFTP/Syslog/SNTP/DNS/DHCP）、TFTP 客户端跨机、MD-02 |
| 无真实二层网络，DHCP 广播不跨机 | DHCP DORA 广播、多网卡选路、租约与真实客户端交互 |
| `hdc shell` 域禁止 bind 显式 UDP 端口（SELinux EACCES） | 组播客户端加入组、接收 DATA、组播 ACK 回程 |
| 长时任务 `TASK_KEEPING` 返回 9800005，只能降级为常驻通知 | 真实后台存活（NF-05）、通知授权与省电策略 |
| 模拟器网络栈与性能不代表真机 | 吞吐（NF-06）、24h 长稳（NF-04）、并发内存（NF-02） |
| 调试签名 + 模拟器安装 | 正式签名、真机安装/升级、应用市场审核材料 |

## 2. 前置准备

### 2.1 设备与连接

```powershell
$sdk='D:\Program Files\Huawei\DevEco Studio\sdk'
$hdc="$sdk\default\openharmony\toolchains\hdc.exe"

& $hdc list targets
& $hdc tconn <设备IP>:5555        # 无线调试；或 USB 直连
& $hdc shell ifconfig             # 记录 wlan0 的 IPv4（记为 <DEV_IP>）
```

- 手机与 PC 接入**同一网段**（同一路由器，或由第三台设备开热点；不要让 PC 自己开热点给被测手机，否则 PC 同时是网关，广播语义会变）。
- PC 防火墙放行入站 UDP：69（原版基准）、6900、5140、1230、5300、6700。
- 连通性：`ping <DEV_IP>` 与 `ping <PC_IP>`（设备侧可用 `hdc shell ping -c 3 <PC_IP>`）。

### 2.2 构建、签名、安装

```powershell
$env:DEVECO_SDK_HOME="$sdk"
$env:JAVA_HOME='D:\Program Files\Huawei\DevEco Studio\jbr'
$env:Path="$env:JAVA_HOME\bin;$env:Path"
& 'D:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat' assembleHap `
    --mode module -p product=default -p buildMode=release --no-daemon

& "$env:JAVA_HOME\bin\java.exe" -jar "$sdk\default\openharmony\toolchains\lib\hap-sign-tool.jar" sign-app `
  -keyAlias "OpenHarmony Application Release" -signAlg SHA256withECDSA -mode localSign `
  -appCertFile <appchain.pem> -profileFile <profile.p7b> `
  -inFile entry\build\default\outputs\default\entry-default-unsigned.hap `
  -keystoreFile "$sdk\default\openharmony\toolchains\lib\OpenHarmony.p12" `
  -outFile entry-signed.hap -keyPwd 123456 -keystorePwd 123456 -signCode 1

& $hdc install -r entry-signed.hap
& $hdc shell aa start -b com.example.myapplication -a EntryAbility
```

### 2.3 被测端配置（设置页）

| 项 | 建议值 | 说明 |
|---|---|---|
| 端口 | 6900 | 真机同样不能绑 69（特权端口） |
| 绑定网卡 LocalIP | 留空（全部接口）或选 wlan0 地址 | 多网卡用例时切换 |
| Syslog / SNTP / DNS / DHCP | 按用例开启 | 端口 5140 / 1230 / 5300 / 6700 |
| 组播地址 / 端口 | `239.255.0.1` / `6969` | 组播用例 |
| 安全级别 | 标准 | 路径用例另设 |
| 后台运行 | 允许通知、电池不限制 | 见 §7 |

### 2.4 PC 侧基准与工具

| 工具 | 用途 |
|---|---|
| `tftpd64.exe` v4.74（`D:\trae\tfpd32\tftpd64.exe`，管理员，绑 69） | 报文级对拍基准 |
| `curl.exe`（自带 tftp 支持）、`tftp.exe`（netascii） | 标准客户端 |
| `certutil -hashfile <f> SHA256` / `MD5` | 文件一致性 |
| `python3` + `scapy`（或 `m2tool` 的 PC 版） | DHCP/Syslog/DNS 报文构造 |
| `w32tm /stripchart /computer:<DEV_IP> /dataonly /samples:1` | SNTP 对拍 |
| `nslookup <name> <DEV_IP>` | DNS 对拍 |
| `otftp2`（NDK 编译，支持 `mcast=`、`abort=`、`delay=`） | 选项/组播/异常对拍 |
| `rtool <ip> <port> <hex> <timeout>` | 原始报文探针（设备内可用） |

测试文件（PC 与设备根目录各放一份）：

```powershell
New-Item -ItemType Directory -Force D:\trae\tftpd32\root | Out-Null
$bytes = New-Object byte[] (10MB); (New-Object Random 1234).NextBytes($bytes)
[IO.File]::WriteAllBytes('D:\trae\tftpd32\root\test_10m.bin', $bytes)
Set-Content D:\trae\tftpd32\root\test_1k.txt ("Tftpd32 test`r`n" * 100)
[IO.File]::WriteAllBytes('D:\trae\tftpd32\root\test_1b.bin', 0x41)
```

设备沙箱根目录：`/data/storage/el2/base/haps/entry/files/tftpboot`（可通过“目录”页查看）。

## 3. 用例总览

| 分组 | 用例 | 优先级 | 状态 |
|---|---|---|---|
| G1 真机通路 | R1 安装/启动/四端口监听；R2 局域网可达 | P0 | 未执行 |
| G2 报文级对拍 | TF-01/02/04、OP-01~08、RE-01/03/05、TC-01/02、SL-01、ST-01、DN-01~03、MD-02 | P0/P1 | 未执行（A3） |
| G3 TFTP 组播 | MC-01 OACK 协商、MC-02 组播接收、MC-03 组播 ACK、MC-04 双客户端 | P1 | 仅发送侧已验（模拟器），接收/ACK 未执行 |
| G4 DHCP 二层 | DH-01~05 广播版、DH-06 多网卡选路、DH-07 Ping 降级 | P0/P1/P2 | 回环单播已通过，广播版未执行 |
| G5 非功能 | NF-02 并发内存、NF-04 24h、NF-05 后台 8h、NF-06 吞吐 | P1/P2 | 均未执行 |
| G6 平台形态 | UI-06 手机长时任务/通知真机、UI-07 2in1 真机冒烟 | P1 | 模拟器已过，真机复测 |
| G7 交付 | SG-01 正式签名安装、SG-02 市场材料 | P0/P2 | 未执行 |

## 4. G1 真机通路

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| R1 | P0 | 安装后冷启动；查看“日志”页与 `hdc shell "cat /proc/net/udp"` | 日志仅一条 `TFTP 服务已启动（端口 6900）`；6900/5140/1230/5300/6700 按开关监听 |
| R2 | P0 | PC 侧 `ping <DEV_IP>`；`curl -v tftp://<DEV_IP>:6900/test_1b.bin -o out.bin` | ping 通；下载成功且 `certutil` 与源一致 |
| R3 | P1 | 关闭再开启服务开关 10 次 | 每次均可重新绑定，无“端口已被占用”残留 |
| R4 | P1 | 退到后台 1 分钟后 PC 发一次 TFTP GET | 前台/后台均可达（后台 8h 见 NF-05） |

## 5. G2 与原版 v4.74 报文级对拍（A3）

同一客户端、同一请求分别打应用（6900）与原版（69），逐项记录差异；差异必须归入
[测试方案 §5 平台偏差](Tftpd32-功能测试方案.md#5-预期平台偏差以下差异不判为失败) 或缺陷表。

| 编号 | 用例 | 步骤 | 预期 |
|---|---|---|---|
| TF-01 | 小文件下载 | `curl tftp://<IP>:6900/test_1k.txt` | 内容/SHA256 一致 |
| TF-02 | 小文件上传 | `curl -T test_1k.txt tftp://<IP>:6900/up.txt` | 落盘一致，日志方向 `>` |
| TF-04 | 10MB 下载 | `curl tftp://<IP>:6900/test_10m.bin` | SHA256 一致，进度正常 |
| OP-01~03 | blksize 协商 | `otftp2` 分别请求 `blksize=16384/8/65464` | 接受/回落行为与原版一致 |
| OP-04/07/08 | tsize、关闭协商、未知选项 | 按测试方案 §6.2 | 与原版一致 |
| OP-05/06 | timeout、windowsize | 请求 `timeout=5/0`、`windowsize=4` | 与原版一致（v4.74 忽略 windowsize） |
| RE-01/03/05 | 超时、未知操作码、畸形报文 | 拔网/暂停客户端 >Timeout×Retransmit；发 opcode=9；发短包/超长包 | 服务不崩溃，错误码与原版一致 |
| TC-01/02 | 应用客户端 ↔ 原版服务器 | 客户端页填 `<PC_IP>:69` GET/PUT | 成功，文件一致（模拟器仅回环，需真机跨机） |
| SL-01 | Syslog 跨机 | PC `python` 发 `<134>Mar 1 test: hello` 到 `<DEV_IP>:5140` | Syslog 页实时显示，来源为 PC IP |
| ST-01 | SNTP 跨机 | `w32tm /stripchart /computer:<DEV_IP> /dataonly /samples:1` | 收到应答，偏差为两机时钟差 |
| DN-01~03 | DNS 跨机 | `nslookup myhost <DEV_IP>`（端口 5300 需用 `nslookup -port=5300`） | A/AAAA/NXDOMAIN 与原版一致 |
| MD-02 | 传输自动 MD5 | 开 `MD5=1`，PC PUT 一个文件 | 日志/目录页 MD5 与 `certutil -hashfile ... MD5` 一致 |

> 端口注意：原版基准用 69；若 PC 上已有其它 TFTP 服务占用 69，可将原版改绑 6901 并在 `otftp2` 中同步。

## 6. G3 TFTP 组播（RFC2090）

配置：设置页 `MulticastAddress=239.255.0.1`、`MulticastPort=6969`（键名与原版一致）。

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| MC-01 | P1 | 单客户端 `otftp2 ... mcast=239.255.0.1:6969&blksize=512` 发 RRQ | 服务端回 `OACK` 含 `multicast=239.255.0.1,6969`；日志 `multicast option accepted` |
| MC-02 | P1 | 客户端加入组 `239.255.0.1:6969` 收 DATA | 收到完整文件（块号连续，MD5 与源一致） |
| MC-03 | P1 | 观察 ACK 回程 | ACK 以**单播**回到服务器（RFC2090），服务器继续推下一块；丢包时按 RFC2090 重传 |
| MC-04 | P2 | 两个客户端同时请求同一文件（先后加入同一组） | 两端均收完；后加入者从当前块继续（与原版一致） |
| MC-05 | P2 | 组播地址填非 224.0.0.0/4（如 10.0.0.1） | 拒绝/回落单播，不崩溃 |

设备内可先用 `m2tool mcast` 验证服务端发送；接收侧必须在真机 app 域或 PC 侧客户端完成
（模拟器 `hdc shell` 域 bind 6969 被 SELinux 拒绝）。

## 7. G4 DHCP 二层广播与多网卡

前置：被测手机设**静态 IP**；另一台 PC（或第二台设备）作为 DHCP 客户端接入同一二层网络；
该网络内**不能有其它 DHCP 服务器**（关闭路由器 DHCP 或使用隔离热点）。

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| DH-01 | P0 | PC `ipconfig /release` → `ipconfig /renew`（或 `scapy` 发广播 DISCOVER，`flags=0x8000`） | 收到 OFFER/ACK（广播回包），获得池内地址、掩码/网关/DNS 与配置一致 |
| DH-02 | P1 | 重启应用/设备后查看 DHCP 页与 `tftpd32.ini` | `Lease_*` 保留，租约表状态正确 |
| DH-03 | P1 | 配置 `BootFile=pxelinux.0` | ACK 携带 `file`；`siaddr` 行为与原版一致（普通 DISCOVER 为 0） |
| DH-04 | P2 | `PoolSize=1`，两台客户端请求 | 第一台成功，第二台无应答/等待，日志提示池耗尽；释放后可再分配 |
| DH-05 | P2 | 配置 option 42/120 | ACK 携带对应选项，长度正确 |
| DH-06 | P1 | 手机同时有 wlan0 与其它网卡（如有线/USB 网卡）时切换 `LocalIP` | DHCP 仅在选中网卡所在子网应答；未选中网卡不响应 |
| DH-07 | P1 | `DHCP Ping=1` | 记录一次降级告警（无 raw socket），随后正常分配；不应崩溃或卡住 |
| DH-08 | P2 | 真实客户端 RELEASE / DECLINE | 租约状态“空闲”/条目销毁，`Lease_NumLeases` 递减，与模拟器回环结果一致 |

> 已知平台差异：鸿蒙无 raw socket，DHCP 的 ICMP Ping / ARP 冲突检测无法实现，`DHCP Ping=1`
> 仅记录告警后直接分配——列入平台偏差，不判失败。

## 8. G5 非功能

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| NF-02 | P1 | 同时发起 10 个 TFTP 传输，`hdc shell "cat /proc/$(pidof com.example.myapplication)/status"` 观察 VmRSS | <100MB，无持续增长 |
| NF-04 | P1 | 连续挂机 24h，每小时探测一次 TFTP/Syslog，记录 VmRSS | 无崩溃、无单调内存增长、端口始终可达 |
| NF-05 | P1 | 开启服务后退到后台 8h，PC 每 30min 发 TFTP GET + Syslog | 全程有应答；系统未杀进程；日志无 socket 断开 |
| NF-06 | P2 | 10MB GET/PUT 各 5 次，记录 MB/s；同机原版 v4.74 同操作对比 | 不劣于原版同机结果的 5 倍（趋势指标） |
| NF-07 | P2 | 弱网/切网（Wi-Fi 断开再连） | 服务不崩溃；恢复后端口重新可达（或日志明确提示需重启服务） |

后台存活观察点：
- `hdc shell "ps -ef | grep com.example.myapplication"` 进程仍在；
- 通知栏常驻通知存在（真机应走 `TASK_KEEPING`，而非模拟器的降级通知）；
- `hdc shell "cat /proc/net/udp"` 中 6900/5140/1230/5300/6700 仍在监听。

## 9. G6 平台形态（真机复测）

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| UI-06 | P1 | 手机首次启动 → 通知授权弹窗 → 允许 → 开启服务 → 退后台 | 常驻通知/长时任务生效；拒绝授权时有中文引导；停服后通知消失 |
| UI-07 | P1 | 2in1 真机：窗口模式切换（全屏/分屏/自由窗口）、拖拽改变尺寸、Tab 遍历 + Enter 激活 | 布局不截断；页签可横向滚动；键盘操作完整 |
| UI-08 | P2 | 深色模式 / 不同 DPI / 横竖屏旋转 | 文字与控件可读，无重叠（允许与原版布局差异） |

## 10. G7 交付

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| SG-01 | P0 | 使用正式发布证书/profile 签名，真机安装并覆盖升级 | 安装成功、升级后配置与租约保留 |
| SG-02 | P2 | 准备市场材料：应用简介、截图、权限用途说明（INTERNET / GET_NETWORK_INFO / KEEP_BACKGROUND_RUNNING）、GPLv2 与源码地址 | 材料齐全，隐私与权限说明与实际行为一致 |

## 11. 建议执行顺序

1. 准备环境与真机连接（§2）→ G1（R1~R4）。
2. G2 报文级对拍：先 TF，再 OP，再 RE/TC，最后 SL/ST/DN/MD（每项记录 SHA256 或报文十六进制）。
3. G3 组播（需同网段双客户端）。
4. G4 DHCP（需隔离二层网络，建议单独时段，避免影响办公网络）。
5. G5 长稳/后台/吞吐（可并行挂机，NF-05 建议过夜）。
6. G6/G7 冒烟与交付（市场材料可提前准备，签名在最后统一做）。

## 12. 记录模板

| 编号 | 环境（机型/系统版本/PC） | 步骤摘要 | 实测结果 | 与原版差异 | 结论（通过/缺陷/平台偏差） | 证据（SHA256/报文/截图路径） |
|---|---|---|---|---|---|---|
|  |  |  |  |  |  |  |

缺陷统一登记到 [测试方案 §9](Tftpd32-功能测试方案.md#9-缺陷记录模板)，平台差异归入
[§5 预期平台偏差](Tftpd32-功能测试方案.md#5-预期平台偏差以下差异不判为失败)。

## 13. 已知阻塞与放行条件

- 无可用真机时，G2~G5 全部阻塞；G1 的安装流水线已在模拟器验证，可复用命令。
- 发布门禁：**G1 + G2 的 P0 用例 100% 通过**，G4 DH-01 通过，G5 NF-05 通过；
  G3 若因系统组播权限受限失败，须记录为平台偏差并在应用说明中注明。
- 组播接收、DHCP 广播、后台 8h 三项是当前版本与“可交付”之间的最后差距。
