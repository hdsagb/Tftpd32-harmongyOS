# Tftpd32 for HarmonyOS：真机开发与测试方案（E2）

> 本文档只收录**必须在鸿蒙真机上完成**的事项：只能在真机上收尾的开发项（§1）与只能在真机上得出
> 有效结论的测试用例（§3~§10）。不需要真机即可完成的内容不写入本文档。
> 使用方式：按 §11 的顺序执行，结果填入 §12 的记录模板，放行条件见 §13。

## 1. 真机开发项

以下 9 项的共同点：代码路径已存在，但**是否成立只能由真机决定**；验证失败的修复也在这一阶段完成。

| 编号 | 开发项 | 现状与代码位置 | 真机完成判据 | 失败处理方向 |
|---|---|---|---|---|
| D1 | 组播接收与 ACK 回程（RFC2090） | OACK 回 `multicast=addr,port` 与组播发送已实现（`tftpd_main.c` 组播分支） | MC-01~MC-05 全部通过 | 若加入组被拒：确认 `INTERNET` 权限与组播地址范围；若 ACK 未回服务器：检查组播 socket 的接收端口与 `TID` 校验 |
| D2 | DHCP 二层广播与真实客户端 | 监听 socket 已设 `SO_BROADCAST`，回包目标为 `INADDR_BROADCAST`（`bootpd.c`） | DH-01~DH-09 通过 | 广播被系统过滤时改为按 `giaddr`/`yiaddr` 单播定向；记录为平台偏差需给出证据 |
| D3 | 后台长时任务与省电策略 | `startBackgroundRunning(TASK_KEEPING)` + 失败降级常驻通知（`Index.ets`） | UI-06、NF-05 通过；真机不出现 9800005 | 若被拒：补 `ohos.permission.KEEP_BACKGROUND_RUNNING` 说明文案、引导用户在电池设置放行；必要时缩短通知刷新间隔 |
| D4 | 休眠/唤醒与网络切换后的端口恢复 | 目前无网络状态监听，套接字在启动时一次性创建 | NF-07 已执行（2026-11-11）：断网 ~1min49s 重连后进程存活、6900 监听在位、GET 自动恢复（无需重启），见 §12；NF-08 通过 | 若唤醒或切网后端口丢失：引入网络状态订阅，在 `netAvailable` 后自动重启服务并写日志 |
| D5 | IPv6 对外形态与 `LocalIP` 绑定 | 监听地址族由 `Enable IPv4`/`Enable IPv6` 决定，`TftpBindLocalInterface` 按 `LocalIP` 绑定 | R7~R9 通过 | 双栈下 IPv4 对端被显示为 v4-mapped 时补归一化；绑定失败需在日志给出可读原因 |
| D6 | 2in1 窗口形态与键盘操作 | 全量支持全屏/分屏/自由窗口，Tab 遍历与 Enter 激活已实现 | UI-07：布局不截断、页签可滚动、键盘操作完整（完成判据，真机未测——见 §3 G6） | 按真机窗口尺寸调整断点与页签滚动策略 |
| D7 | 权限拒绝后的降级 | 网卡枚举与通知均有失败分支 | UI-09 已执行（2026-11-11）：通知撤销腿通过（服务仍运行、恢复无需重装，中文提示不触发系长时任务成功路径不可达）；GET_NETWORK_INFO 撤销腿不适用（system_grant 用户不可撤销），网卡枚举失败降级分支保持代码覆盖（见 §12） | 网卡枚举失败时下拉固定为“全部接口”并解释原因；权限引导文案补齐 |
| D8 | 正式签名与覆盖升级 | 正式发布证书（tftpd32-HS.cer/.p12）与内部测试 profile（AGC「指定设备发布」T32-testRelease.p7b）已齐备，签名配置已切换（build-profile.json5） | SG-01 已执行（2026-11-11）：internaltesting profile 签名 v1（1000000）首装 → 留存升级前基准（ini 保存 + TFTP 基准文件 MD5 + 双 MAC 租约）→ bump 1000001 重签 install -r 覆盖升级成功（appIdentifier/firstInstallTime 不变）→ 双服务自动恢复、GET MD5 一致、同 MAC 重分得同 IP（ini 与 Lease_* 保留），见 §12 | 申请发布证书与 profile；确认升级后 `tftpd32.ini` 与租约表保留 |
| D9 | 市场材料截图 | 文字材料已就绪，截图必须真机拍摄 | SG-02 通过（§10 的 7 张清单） | 按清单补拍，标注页签名 |

触发式开发项（只有真机测试暴露问题时才产生，修复同样在本阶段完成）：

- NF-04/NF-05 出现崩溃、内存单调增长或端口丢失 → 定位并修复（含 D4 的恢复机制）。
- NF-06 吞吐明显劣于原版基线 → 调优默认 `blksize`、减少拷贝与日志写入。
- R5/R6 的提示文案与真机实际错误码不一致 → 按真机返回码修正中文提示。
- UI-07/UI-08 在真机尺寸下截断或错位 → 调整断点与页签滚动策略。
- **2026-11-10 新增（UI-07 附带发现）**：滚轮切页签行为不一致——内容短页（无 Scroll 包裹，如 SNTP/DNS/DHCP）滚轮冒泡至 Tabs 快捷切页、长内容页（设置页，有 Scroll）仅滚动内容；可切页时快速滚轮易误跳非预期页签。统一各页滚轮消费逻辑（全部可切或全部不可切）并回归。
- **2026-11-10 新增（UI-08 附带发现）**：系统"字体大小"调整不改变应用字体（"显示大小"正常跟随）——排查 fontSize 单位与系统字体缩放联动（fp 跟随/vp 固定），必要时显式适配；低优先级，与滚轮统一逻辑一并回归。
- **2026-11-11 新增（SG-01 期间提出）**：配置自动保存——设置页修改配置后无需手动点「保存配置」即自动持久化到 tftpd32.ini（含服务开关、端口、安全级别、根目录、DHCP 等全部登记键），降低配置丢失风险；实现时定义保存时机（如失焦/切换页签/退后台）并避免高频写盘。

## 2. 前置准备

### 2.1 设备与网络

```powershell
$sdk='D:\Program Files\Huawei\DevEco Studio\sdk'
$hdc="$sdk\default\openharmony\toolchains\hdc.exe"

& $hdc list targets
& $hdc tconn <设备IP>:5555        # 无线调试；或 USB 直连
& $hdc shell ifconfig             # 记录 wlan0 的 IPv4（<DEV_IP>）与 global IPv6（<DEV_IP6>）
```

- 手机与 PC 接入**同一网段**（同一路由器，或由第三台设备开热点）。不要让 PC 自己开热点给被测手机，否则 PC 同时是网关，DHCP 与广播语义会变。
- PC 防火墙放行入站 UDP：69（原版基准）、6900、5140、1230、5300、6700。
- 连通性：`ping <DEV_IP>` 与 `hdc shell ping -c 3 <PC_IP>` 均通。

### 2.2 构建、签名、安装

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -BuildMode release -Launch
# 仅构建与签名、不安装：追加 -NoInstall；签名材料变更导致覆盖安装失败：先加 -Uninstall
```

手动流程（与脚本等价，便于排查）：

```powershell
$deveco='D:\Program Files\Huawei\DevEco Studio'
$sdk="$deveco\sdk"
$env:DEVECO_SDK_HOME=$sdk
$env:Path="$deveco\tools\node;$deveco\jbr\bin;$env:Path"

& "$deveco\tools\hvigor\bin\hvigorw.bat" --mode module -p product=default `
    -p buildMode=release assembleHap --no-daemon

& "$deveco\jbr\bin\java.exe" -jar "$sdk\default\openharmony\toolchains\lib\hap-sign-tool.jar" sign-app `
  -mode localSign -keyAlias tftpd32-key -keyPwd 123456 -signAlg SHA256withECDSA -signCode 1 -compatibleVersion 24 `
  -appCertFile signature\app-chain.cer -profileFile signature\profile.p7b -keystoreFile signature\tftpd32.p12 -keystorePwd 123456 `
  -inFile entry\build\default\outputs\default\entry-default-unsigned.hap -outFile entry-signed.hap

& $hdc install -r entry-signed.hap
& $hdc shell aa start -b com.example.myapplication -a EntryAbility
```

### 2.3 被测端配置（设置页）

| 项 | 建议值 | 说明 |
|---|---|---|
| 端口 | 6900 | 真机同样不能绑 69（特权端口） |
| 绑定网卡 LocalIP | 留空（全部接口）或选 wlan0 地址 | 多网卡用例时切换 |
| 启用 IPv4 | 默认开启 | 地址族用例见 R8/R9 |
| Syslog / SNTP / DNS / DHCP | 按用例开启 | 端口 5140 / 1230 / 5300 / 6700 |
| 组播地址 / 端口 | `239.255.0.1` / `6969` | 组播用例 |
| 安全级别 | 标准 | 路径用例另设 |
| 后台运行 | 允许通知、电池不限制 | 见 D3 |

### 2.4 PC 侧基准与工具

| 工具 | 用途 |
|---|---|
| `tftpd64.exe` v4.74（管理员，绑 69） | 报文级对拍基准 |
| `curl.exe`（自带 tftp 支持）、`tftp.exe`（netascii） | 标准客户端 |
| `certutil -hashfile <f> SHA256` / `MD5` | 文件一致性 |
| `python3` + `scapy`（或 `m2tool` 的 PC 版） | DHCP/Syslog/DNS 报文构造 |
| `w32tm /stripchart /computer:<DEV_IP> /dataonly /samples:1` | SNTP 对拍 |
| `nslookup -port=5300 <name> <DEV_IP>` | DNS 对拍 |
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

| 组 | 用例 | 优先级 | 关联开发项 | 状态 |
|---|---|---|---|---|
| G1 通路 | R1 安装/启动/端口监听；R2 局域网可达；R3 启停；R4 前后台；R5 特权端口；R6 端口占用；R7 网卡绑定；R8 双栈；R9 纯 IPv6；R10 配置迁移 | P0/P1/P2 | D4/D5 | 部分执行：R1~R5、R7、R9、R10 已有记录（R5/R9/R10 缺陷后 2026-11-02 回归闭环）；R6 已于 2026-11-11 真机执行通过（第三方应用占用 6900→中文提示+TFTP 不监听，释放后重启可绑定，见 §12）；R8 IPv4 腿通过、IPv6 腿待环境（详见 §12） |
| G2 报文对拍 | TF-01~06、OP-01~08、RE-01/03/05、TC-01/02、SL-01、ST-01、DN-01~03、MD-02 | P0/P1 | D5 | 部分执行：TF-01/02、TF-04（10MB GET）、OP/RE/TC/SL/ST/DN、MD-02 已通过；TF-03/05/06 于 2026-11-03 回归闭环（详见 §12 与未测项目报告） |
| G3 组播 | MC-01 OACK 协商、MC-02 组播接收、MC-03 组播 ACK、MC-04 双客户端、MC-05 非法地址 | P1/P2 | D1 | 已执行：MC-01/03/05 通过；MC-02/04 缺陷（D7/D8）修复后 2026-11-02 回归闭环（详见 §12 与未测项目报告） |
| G4 DHCP | DH-01~05 广播版、DH-06 多网卡、DH-07 Ping 降级、DH-08 释放、DH-09 option 82 | P0/P1/P2 | D2 | 已执行：DH-01~09 均有记录（DH-01 受限广播属 Wi-Fi 链路层平台行为，2026-11-02 复核通过；详见 §12） |
| G5 非功能 | NF-01 冷启动、NF-02 并发内存、NF-03 体积、NF-04 24h、NF-05 后台 8h 含锁屏、NF-06 吞吐、NF-07 切网、NF-08 休眠唤醒 | P1/P2 | D3/D4 | 已全部执行：NF-01/02/03/04/05/06/08 通过（NF-04 见复测 B-03；NF-02 native heap 采样无泄漏、NF-03 体积独立结论、NF-06 判据修订为绝对值，均 2026-11-03）；NF-07 已于 2026-11-11 真机人工执行通过（断网 ~1min49s 进程存活、监听在位、恢复后 GET 自动可达，见 §12） |
| G6 平台形态 | UI-06 长时任务/通知、UI-07 2in1、UI-08 深色/DPI/旋转、UI-09 权限拒绝 | P1/P2 | D3/D6/D7 | 已全部执行：UI-06（2026-11-03）、UI-07（2026-11-10）、UI-08（2026-11-10，字体不跟随记偏差、旋转设备不支持不适用）、UI-09（2026-11-11，通知撤销腿通过、GET_NETWORK_INFO 撤销腿不适用系 system_grant 平台限制）均通过，详见 §12 |
| G7 交付 | SG-01 正式签名与升级、SG-02 市场材料 | P0/P2 | D8/D9 | 已全部执行通过：SG-02（2026-11-10，7 张截图齐、材料一致性核对通过，见 §12）；SG-01（2026-11-11，内部测试 profile 签名首装 → 覆盖升级 1000001，配置/文件/租约保留，见 §12） |

> 状态截至 2026-11-11。§12 为逐轮原始记录（保留执行当时的结论）；缺陷后续回归闭环与复核记录见《Tftpd32-未测项目报告》。

## 4. G1 真机通路

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| R1 | P0 | 安装后冷启动；查看“日志”页与 `hdc shell "cat /proc/net/udp"` | 日志仅一条 `TFTP 服务已启动（端口 6900）`；6900/5140/1230/5300/6700 按开关监听 |
| R2 | P0 | PC 侧 `ping <DEV_IP>`；`curl -v tftp://<DEV_IP>:6900/test_1b.bin -o out.bin` | ping 通；下载成功且 `certutil` 与源一致 |
| R3 | P1 | 关闭再开启服务开关 10 次 | 每次均可重新绑定，无“端口已被占用”残留 |
| R4 | P1 | 退到后台 1 分钟后 PC 发一次 TFTP GET | 前台/后台均可达（后台 8h 见 NF-05） |
| R5 | P2 | 端口改 69 保存并启动服务 | 中文提示“无权限绑定该端口（1024 以下为系统特权端口）”，服务保持“已停止”，进程存活 |
| R6 | P2 | 先用其它应用/`m2tool` 占用 6900，再启动服务 | 提示端口已被占用（中文），服务保持停止；释放后重启可绑定 |
| R7 | P1 | `LocalIP` 选 wlan0 地址，PC 从同网段 GET；再用另一子网（USB 网卡/蜂窝）地址访问 | 选中网卡可达；未选中接口不响应 |
| R8 | P1 | 默认双栈（`Enable IPv4=1`、`Enable IPv6=1`）：PC 分别用 `curl tftp://<DEV_IP>:6900/test_1b.bin` 与 `curl tftp://[<DEV_IP6>]:6900/test_1b.bin` | 两种地址族均成功；日志中的 IPv4 对端显示为普通 IPv4，不出现 `::ffff:` 形式 |
| R9 | P1 | 关“启用 IPv4”、保留 `Enable IPv6=1`，重启服务后重复 R8 | IPv6 成功；IPv4 请求无应答（`Who is [::]:6900`，端口仅存在于 `/proc/net/udp6`）；再打开 IPv4 并重启可恢复 |
| R10 | P2 | 把 PC 上原版 `tftpd32.ini` 推入沙箱，`aa force-stop` 后冷启动；再在设置页点“保存配置” | 端口/超时/重传/根目录按原版 ini 生效且界面回填正确；保存后登记键按当前值写回，`LastWindowPos`/`HttpBaseDirectory`/`Donot verify firewall` 等未登记键原样保留 |

```powershell
# R10 的准备命令
& $hdc file send D:\trae\tftpd32\tftpd32.ini `
    /data/app/el2/100/base/com.example.myapplication/haps/entry/files/tftpd32.ini
& $hdc shell aa force-stop com.example.myapplication
& $hdc shell aa start -a EntryAbility -b com.example.myapplication
& $hdc shell cat /data/app/el2/100/base/com.example.myapplication/haps/entry/files/tftpd32.ini
```

> R8/R9 若 PC 无 IPv6 地址，结论无效；须给 PC 配置 IPv6 或换支持 IPv6 的终端。

## 5. G2 与原版 v4.74 报文级对拍

同一客户端、同一请求分别打应用（6900）与原版（69），逐项记录差异；差异归入 §12 的平台偏差表或缺陷表。

| 编号 | 用例 | 步骤 | 预期 |
|---|---|---|---|
| TF-01 | 小文件下载 | `curl tftp://<IP>:6900/test_1k.txt` | 内容/SHA256 一致 |
| TF-02 | 小文件上传 | `curl -T test_1k.txt tftp://<IP>:6900/up.txt` | 落盘一致，日志方向 `>` |
| TF-04 | 10MB 下载 | `curl tftp://<IP>:6900/test_10m.bin` | SHA256 一致，进度正常 |
| OP-01~03 | blksize 协商 | `otftp2` 分别请求 `blksize=16384/8/65464` | 接受/回落行为与原版一致 |
| OP-04/07/08 | tsize、关闭协商、未知选项 | 请求 `tsize=0`、`Negociate=0` 后再请求选项、带未知选项 | 与原版一致 |
| OP-05/06 | timeout、windowsize | 请求 `timeout=5/0`、`windowsize=4` | 与原版一致（v4.74 忽略 windowsize） |
| RE-01/03/05 | 超时、未知操作码、畸形报文 | 拔网/暂停客户端 >Timeout×Retransmit；发 opcode=9；发短包/超长包 | 服务不崩溃，错误码与原版一致 |
| TC-01/02 | 应用客户端 ↔ 原版服务器 | 客户端页填 `<PC_IP>:69` GET/PUT | 成功，文件一致 |
| SL-01 | Syslog 跨机 | PC 发 `<134>Mar 1 test: hello` 到 `<DEV_IP>:5140` | Syslog 页实时显示，来源为 PC 的 IP |
| ST-01 | SNTP 跨机 | `w32tm /stripchart /computer:<DEV_IP> /dataonly /samples:1` | 收到应答，偏差为两机时钟差 |
| DN-01~03 | DNS 跨机 | `nslookup -port=5300 myhost <DEV_IP>` | A/AAAA/NXDOMAIN 与原版一致 |
| MD-02 | 传输自动 MD5 | 开 `MD5=1`，PC PUT 一个文件 | 日志/目录页 MD5 与 `certutil -hashfile ... MD5` 一致 |

> 原版基准用 69；若 PC 上已有其它 TFTP 服务占用 69，可将原版改绑 6901 并在 `otftp2` 中同步。
>
> 编号说明（消除两义，编号权威=功能方案 §6.1）：本节 TF-04（10MB 下载）对应功能方案 **TF-03**；功能方案的 TF-04（1KB PUT）由本节 TF-02 与 TC-01/02 覆盖。本节 TF-01/TF-02 与功能方案 TF-01/TF-02 语义不同（功能 TF-01=1B 下载、TF-02=1KB 文本下载）。§12 历史记录保留原编号。

## 6. G3 TFTP 组播（RFC2090）

配置：`MulticastAddress=239.255.0.1`、`MulticastPort=6969`（键名与原版一致）。

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| MC-01 | P1 | 单客户端 `otftp2 ... mcast=239.255.0.1:6969&blksize=512` 发 RRQ | 服务端回 `OACK` 含 `multicast=239.255.0.1,6969`；日志 `multicast option accepted` |
| MC-02 | P1 | 客户端加入组 `239.255.0.1:6969` 收 DATA | 收到完整文件（块号连续，MD5 与源一致） |
| MC-03 | P1 | 观察 ACK 回程 | ACK 以**单播**回到服务器，服务器继续推下一块；丢包按 RFC2090 重传 |
| MC-04 | P2 | 两个客户端同时请求同一文件（先后加入同一组） | 两端均收完；后加入者从当前块继续（与原版一致） |
| MC-05 | P2 | 组播地址填非 224.0.0.0/4（如 10.0.0.1） | 拒绝/回落单播，不崩溃 |

## 7. G4 DHCP 二层广播与多网卡

前置：被测手机设**静态 IP**；另一台 PC（或第二台设备）作为 DHCP 客户端接入同一二层网络；
该网络内**不能有其它 DHCP 服务器**（关闭路由器 DHCP 或使用隔离热点）。

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| DH-01 | P0 | PC `ipconfig /release` → `ipconfig /renew`（或 `scapy` 发广播 DISCOVER，`flags=0x8000`）；再用一台 Android/手机接入同一网络自动获取 IP | 两台客户端均收到 OFFER/ACK（广播回包），获得池内地址、掩码/网关/DNS 与配置一致 |
| DH-02 | P1 | 重启应用/设备后查看 DHCP 页与 `tftpd32.ini` | `Lease_*` 保留，租约表状态正确 |
| DH-03 | P1 | 配置 `BootFile=pxelinux.0` | ACK 携带 `file`；`siaddr` 行为与原版一致（普通 DISCOVER 为 0） |
| DH-04 | P2 | `PoolSize=1`，两台客户端请求 | 第一台成功，第二台无应答/等待，日志提示池耗尽；释放后可再分配 |
| DH-05 | P2 | 配置 option 42/120 | ACK 携带对应选项，长度正确 |
| DH-06 | P1 | 手机同时有 wlan0 与其它网卡（有线/USB）时切换 `LocalIP` | DHCP 仅在选中网卡所在子网应答；未选中网卡不响应 |
| DH-07 | P1 | `DHCP Ping=1` | 记录一次降级告警（无 raw socket），随后正常分配；不崩溃不卡住 |
| DH-08 | P2 | 真实客户端 RELEASE / DECLINE | 租约状态“空闲”/条目销毁，`Lease_NumLeases` 递减 |
| DH-09 | P2 | DHCP relay/option 82：在交换机或软件 relay 上开启 option 82 后申请地址 | ACK 回显 option 82（cirID/remID 日志同上游 `Adding option 82`），租约正常分配 |

> 编号说明（消除两义，编号权威=功能方案 §6.10）：本节 DH-06（多网卡 `LocalIP` 选路）与功能方案 DH-06（自定义选项对齐，已在模拟器 §6.10.1 执行通过）编号两义；本节 DH-06 相当于功能 DH-06 遗留的真机“多网卡选路”子项。§12 历史记录保留原编号。

## 8. G5 非功能

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| NF-01 | P1 | 冷启动计时：`aa start` 到 PC 首次 GET 成功 | ≤2s（3 次中位数） |
| NF-02 | P1 | 同时发起 10 个 TFTP 传输，`hdc shell "cat /proc/$(pidof com.example.myapplication)/status"` 观察 VmRSS | <100MB，无持续增长（判据修订：ArkTS 常驻致绝对阈值不适用，按复测 B-03 口径以 native heap Pss 无单调增长判定） |
| NF-04 | P1 | 连续挂机 24h，每小时探测一次 TFTP/Syslog，记录 VmRSS | 无崩溃、无单调内存增长、端口始终可达 |
| NF-05 | P1 | 开启服务后退到后台 8h（其中 ≥1h **锁屏灭屏**），PC 每 30min 发 TFTP GET + Syslog | 全程有应答；系统未杀进程；锁屏期间同样可达 |
| NF-06 | P2 | 10MB GET/PUT 各 5 次记录 MB/s；同机原版 v4.74 同操作对比 | 不劣于原版同机结果的 5 倍（判据修订：设备无法运行 Windows exe，改为「传输完整+哈希一致+绝对耗时记录」，2026-11-03 关闭） |
| NF-07 | P2 | Wi-Fi 断开再连（或切到热点） | 服务不崩溃；恢复后端口重新可达（或日志明确提示需重启服务） |
| NF-08 | P1 | 休眠/唤醒（`hdc shell power-shell suspend` 或电源键休眠 10min）后 PC 探测 5 个端口 | 唤醒后 5 个端口仍在监听；若长时任务被回收，日志给出明确提示且可一键重启 |

后台存活观察点：

- `hdc shell "ps -ef | grep com.example.myapplication"` 进程仍在；
- 通知栏常驻通知存在（真机应走 `TASK_KEEPING`）；
- `hdc shell "cat /proc/net/udp"` 中 6900/5140/1230/5300/6700 仍在监听。

## 9. G6 平台形态

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| UI-06 | P1 | 首次启动 → 通知授权弹窗 → 允许 → 开启服务 → 退后台 | 成功申请 `TASK_KEEPING`（不出现 9800005）；常驻通知按系统策略显示；拒绝授权有中文引导；停服后通知消失 |
| UI-07 | P1 | 2in1：全屏/分屏/自由窗口切换、拖拽改尺寸、Tab 遍历 + Enter 激活 | 布局不截断；页签可横向滚动；键盘操作完整 |
| UI-08 | P2 | 深色模式 / 不同 DPI / 横竖屏旋转 | 文字与控件可读，无重叠（允许与原版布局差异） |
| UI-09 | P2 | “设置 > 应用 > 权限”撤销 `GET_NETWORK_INFO` 后进入设置页；再撤销通知权限 | 绑定网卡降级为“全部接口”，其余功能可用；撤销通知后启动服务给出中文提示，服务仍可运行 |

## 10. G7 交付

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| SG-01 | P0 | 使用正式发布证书/profile 签名，真机安装并覆盖升级 | 安装成功、升级后配置与租约保留 |
| SG-02 | P2 | 备齐市场材料：简介、截图、权限用途说明（INTERNET / GET_NETWORK_INFO / KEEP_BACKGROUND_RUNNING）、GPLv2 与源码地址 | 材料齐全，权限说明与实际行为一致 |

SG-02 截图清单（竖屏 1080×2340 左右，每张标注页签名）：

1. 设置页（服务“运行中”，可见端口与“启用 IPv4”）；
2. TFTP 页传输中（进度条 + 重传计数）；
3. 日志页（含 `sent N blks` 与 `Error 1` 两条对比）；
4. DHCP 页（地址池配置 + 租约表至少 2 条）；
5. 客户端页（一次 GET 的 done 状态）；
6. 目录页（文件列表 + MD5 按钮）；
7. 2in1 自由窗口形态 1 张（无 2in1 设备可省略，并在材料中说明仅手机形态）。

## 11. 建议执行顺序

1. §2 准备 → G1（R1~R10）。R5/R6 必须在正式签名前做（便于失败时快速重装）。
2. G2 对拍：TF → OP → RE/TC → SL/ST/DN/MD，每项记录 SHA256 或报文十六进制。
3. G3 组播（同网段双客户端）。
4. G4 DHCP（隔离二层网络，单独时段；DH-09 可用软件 relay）。
5. G5 非功能（可并行挂机，NF-05 过夜）。
6. G6 形态 → G7 交付（SG-01 最后做，SG-02 截图随 G6/G7 一并补拍）。

## 12. 记录模板与平台偏差

| 编号 | 环境（机型/系统版本/PC） | 步骤摘要 | 实测结果 | 与原版差异 | 结论（通过/缺陷/平台偏差） | 证据（SHA256/报文/截图路径） |
|---|---|---|---|---|---|---|
| R1 | MateBook Pro(HAD-W32) / HarmonyOS 6.1.0.117 SP68C00E100R13P3 / API 23；PC Win10 10.78.218.198 ↔ 设备 10.78.218.8:34101 | 冷启动后看日志页 + `netstat -anu` | 日志仅一条 `TFTP 服务已启动（端口 6900）`；`udp 0 0 0.0.0.0:6900`；DHCP/Syslog/SNTP/DNS 按开关监听 | 无 | 通过 | netstat 输出；dumpLayout 日志页文本 |
| R2 | 同上 | `ping 10.78.218.8`；`curl tftp://10.78.218.8:6900/test_1b.bin` | ping 通；GET 1 字节成功 | 无 | 通过 | SHA256 `559AEAD08264D5795D3909718CDD05ABD49572E84FE55590EEF31A88A08FDFFD` |
| R3 | 同上 | 设置页服务开关关/开 ×10 | 10 轮全部重新绑定成功，无"端口已被占用"残留 | 无 | 通过 | 每轮 `started=True` + netstat 6900 |
| R4 | 同上 | 退后台 65s 后 PC GET | 后台仍可达，端口未释放 | 无 | 通过 | GET SHA256 同 R2 |
| R5 | 同上 | 端口改 69 保存并启动服务 | 日志 `ERROR: 绑定端口失败：错误 13（Permission denied）`；服务保持"已停止"，进程存活 | 未走 `PalSvcErrText` 的中文专用文案（"无权限绑定该端口（1024 以下为系统特权端口）"） | 缺陷 | 日志页文本 |
| R6 | 同上 | 构造外部进程占用 6900 后启动服务 | 无法构造：SELinux 禁外部 ELF；系统端口在其它 netns；`SO_REUSEADDR` 使双绑成功 | — | 阻塞（仅源码级确认） | `service_main.c` bind 路径 |
| R6（2026-11-11 复测闭环） | MateBook Pro(HAD-W32) / HarmonyOS 6.1.0.117；PC Win10 192.168.10.104 ↔ 设备 192.168.10.128；占用工具=应用市场「网络调试助手PC版」（设备本机，UDP Server 绑 192.168.10.128:6900） | 第三方应用占用 6900 → 冷启动应用（服务开关持久化为开，自启动即在占用下绑定）→ 观察日志页 → 工具断开释放 → 服务开关关/开重启 → PC GET | 占用时设备侧仅 `udp 192.168.10.128:6900`（工具）、`/proc/net/udp6` 无 1AF4；日志中文提示「TFTP 服务无法绑定端口：tftp / 该端口已被其它应用占用，请更换端口后重试」与「TFTP 服务未在端口 6900 上建立监听（该端口已被占用，errno 98）」；`Port 6900 may be reused` 系应用已设 SO_REUSEADDR 仍 EADDRINUSE（占用方未设），排除自占用假象；主开关保持「运行中」（其余四服务正常启动），TFTP 实际未监听。释放后 `udp6 [::]:6900` 重建（pid 不变）；PC GET test_1b.bin DONE，MD5=2408AD11F9EB830DA749E2A36A29EFF7 与基准一致 | 无（errno 98=EADDRINUSE 映射中文文案正确） | 通过（三判据全成立；2026-11-02 阻塞原因"SELinux 禁外部 ELF"经第三方占用应用绕开） | `docs\test-evidence\R6\r6_01_service_autostart_under_occupation.jpeg`、`r6_02_bind_fail_log_chinese_prompt.jpeg`、`r6_03_service_rebound_after_release.jpeg`（含 GET 时间线） |
| R7 | 同上 | `LocalIP` 选 10.78.218.8 后 PC GET | `udp 0 0 10.78.218.8:6900`，GET 成功 | 无 | 通过 | netstat + GET SHA256 同 R2 |
| R8 | 同上（PC 本地连接网卡无 IPv6 地址） | 默认双栈，分别用 IPv4/IPv6 地址 GET | `netstat` 仅有 `udp 0 0 0.0.0.0:6900`，**无 udp6**；设备 wlan0 无任何 inet6 地址、PC 无 IPv6 出口 → IPv6 腿无法实测 | `AF_UNSPEC` 下直接取 `res[0]`（=IPv4），双栈时未同时监听 IPv6 | 缺陷 + 环境受限（按 §4 L147 结论无效） | netstat；`ip addr` / `Get-NetIPAddress -AddressFamily IPv6` |
| R9 | 同上 | 关"启用 IPv4"→重启服务→重复 R8 | 端口仅存在于 udp6（`udp6 0 0 [::]:6900`）符合预期；但 `curl tftp://10.78.218.8:6900/...` **仍成功**（1B，SHA256 一致） | 纯 IPv6 时未设 `IPV6_V6ONLY=1`，socket 以 v4-mapped 继续接受 IPv4 | 缺陷（不符预期） | netstat + GET SHA256；`tftpd_main.c` L128 |
| R10 | 同上 | 推入原版 `tftpd32.ini`（TftpPort=6912/Timeout=5/MaxRetransmit=8/BaseDirectory=…/files + 3 个未登记键）→ `aa force-stop` 冷启动 → 点"保存配置" | 端口 6912 真实生效（`udp 0 0 0.0.0.0:6912`，GET SHA256 一致）；超时 5、重传 8 回填；保存后登记键按当前值写回，`LastWindowPos=120,90,1024,768`/`HttpBaseDirectory=C:\www\http`/`Donot verify firewall=1` **原样保留** | 根目录被 `Index.ets` 硬编码为 `<filesDir>/tftpboot`，未按 ini 的 `BaseDirectory` 回填 | 部分通过（根目录一项缺陷） | ini 前后全文；netstat 6912；日志"配置已保存" |
| TF-01 | 同上 | `curl tftp://10.78.218.8:6900/test_1k.txt` | 1405B，143ms，SHA256 `617388A2EB73603A2E2663E4167BC040B0B94FA76C67EE235F3DEE386B54FF3B` 与源一致 | 无 | 通过 | SHA256；耗时 |
| TF-02 | 同上 | `curl -T test_1k.txt tftp://10.78.218.8:6900/up.txt` | 落盘 1405B，SHA256 与源一致；日志方向为上传 | 无 | 通过 | SHA256 |
| TF-04（≙ 功能方案 TF-03） | 同上 | 10MB `test_10m.bin` GET | blksize=512：577.15s（≈18KB/s）；blksize=16384：41.92s（≈250KB/s）；SHA256 与源一致 | 无（速度受 Wi-Fi 与逐块 ACK 限制，见 NF-06） | 通过 | SHA256；耗时 |
| OP-01/02/03 | 同上 | RRQ 带 `blksize=16384 / 8 / 65464` | OACK 分别回 `16384`、`8`、`16384`（65464 钳制到上限 16384） | 与原版一致 | 通过 | OACK 十六进制（`0006626c6b73697a6500313633383400` 等） |
| OP-04 | 同上 | RRQ 带 `tsize=0` | OACK `tsize=1405` | 一致 | 通过 | OACK `00067473697a65003134303500` |
| OP-05 | 同上 | RRQ 带 `timeout=5` | OACK `timeout=5` | 一致 | 通过 | OACK `000674696d656f7574003500` |
| OP-06 | 同上 | RRQ 带 `windowsize=4` | 不回 OACK，直接 DATA#1 len=512（选项被忽略） | 与原版 v4.74 一致 | 通过 | 首包 `00030001…` |
| OP-07 | 同上 | `Negociate=0` 后 RRQ 带 `blksize=16384` | 不回 OACK，直接 DATA#1 len=512 | 一致 | 通过 | 首包 `00030001…` |
| OP-08 | 同上 | RRQ 带未知选项 `foo=bar` | 忽略未知选项，直接 DATA#1 len=512 | 一致 | 通过 | 首包 `00030001…` |
| RE-01 | 同上 | 客户端只发 RRQ 后停止应答 | 服务端按 Timeout 重传 8 次后静默（与 `MaxRetransmit=8` 一致），进程存活 | 无 | 通过 | 抓包重传计数；日志 |
| RE-03 | 同上 | 发 opcode=9 报文 | 回 `ERROR code=4 (Illegal operation)` | 一致 | 通过 | 报文十六进制 |
| RE-05a | 同上 | 发 1~3 字节短包 | 回 `ERROR code=4` | 一致 | 通过 | 报文 |
| RE-05c | 同上 | RRQ 文件名 600B（超长） | 无应答、不崩溃 | 与原版一致（静默丢弃） | 通过（按原版行为判定） | 无回包 |
| RE-05d/e | 同上 | 向 6900 直发 DATA / ACK | 回 `ERROR code=4`（未知 TID 语义按原版处理） | 一致 | 通过 | 报文 |
| TC-01/02 | 同上 | 应用"客户端"页对 PC 端 TFTP 服务 GET/PUT | 均成功，MD5 `5ec7149304561c08cc505c5903e34bca` 一致 | 无 | 通过 | 客户端页状态；MD5 |
| SL-01 | 同上 | PC 发 `<134>Mar 1 test: hello` → `10.78.218.8:5140` | Syslog 页实时显示，来源 IP 为 PC | 无 | 通过 | 页面文本 |
| ST-01 | 同上 | `w32tm /stripchart /computer:10.78.218.8 /dataonly /samples:1`（端口 1230 由应用监听） | 收到应答，stratum=10，时钟差 0.3s | 无 | 通过 | 抓包/命令输出 |
| DN-01~03 | 同上 | `nslookup -port=5300` 查 A 记录 / 不存在域名 | A 记录正确；不存在域名 rcode=4（与原版一致） | 无 | 通过 | nslookup 输出 |
| MD-02 | 同上 | `MD5=1` 后 PC PUT 文件 | 生成 `.md5` 文件，内容与 `certutil -hashfile … MD5` 一致 | 无 | 通过 | 两侧 MD5 文本 |
| MC-01 | 同上 + 组播配置 `MulticastAddress=239.255.0.1`/`MulticastPort=6969` | RRQ 带 `multicast=239.255.0.1:6969&blksize=512` | OACK `{'multicast': '239.255.0.1,6969', 'blksize': '512'}`；日志 `multicast option accepted : 239.255.0.1,6969` | 无 | 通过 | OACK 文本；日志 |
| MC-02 | 同上 | 客户端加入组收 DATA，小文件 `test_1k.txt` | 3 块全部收到，1405B，0.15s，SHA256 `617388A2…B54FF3B` 与源一致 | 大文件（>10 块）会因缺陷 D7 中止 | 部分通过（小文件通过，大文件缺陷） | SHA256；逐块时间戳 |
| MC-03 | 同上 | 观察 DATA 源与 ACK 回程 | DATA 源 `10.78.218.8:6969`（组播 socket）；ACK 单播回 OACK 源端口（如 55202） | 无 | 通过 | 抓包源/目的地址 |
| MC-04 | 同上 | 两个客户端同时请求 `test_10m.bin`，同组先后加入 | **失败**：每个客户端各只收到 block 1，服务端 `MAX RETRIES while waiting for Ack block 2` 后向组播组发 `ERROR code=0`，两端同时中断 | 与 RFC2090/原版不一致 | 缺陷（P1，D7+D8 共同导致） | 服务端日志；客户端逐块时间戳 |
| MC-05 | 同上 | 组播地址改 `10.0.0.1`（非 224/4）后 RRQ 带 `multicast` | OACK 仅含 `blksize=512`（不含 multicast）；组播口无 DATA；改走单播 GET 成功（SHA256 一致）；服务未崩溃 | 拒绝方式为"忽略该选项并回落单播"，与原版一致 | 通过 | OACK 文本；单播 GET SHA256 |
| DH-01 | 同上 | PC 发 DISCOVER/REQUEST（`flags=0x8000`，MAC 02:11:22:33:44:88）到 `10.78.218.8:6700` | OFFER/ACK 均收到，`yiaddr=10.78.218.200`；opt1=255.255.255.0、opt3=10.78.218.1、opt6=8.8.8.8、opt51=300、opt54=10.78.218.8、opt58=120、opt59=240、opt66=`31302e37382e3231382e38`（"10.78.218.8"） | 广播到 `255.255.255.255:6700` 在本机环境收不到 OFFER（Windows 防火墙/无线驱动不透传 255.255.255.255），改用单播仍可验证服务端回包（服务端按请求源地址判定 `bUniCast` 后单播回源端口） | 平台偏差（见下表） | `dhcp_probe.py` 报文输出 |
| DH-02 | 同上 | MAC `…:44:66` 取址后不释放 → `aa force-stop` + `aa start` | 重启后 DHCP 页租约表仍 2 条（`.200` 空闲、`.201` 已确认 10-28 02:24）；ini 中 `Lease_NumLeases`/`Lease_n_MAC/IP/LeaseStartTime` 完整保留 | 无 | 通过 | 租约表 dump；ini `Lease_*` 全文 |
| DH-03 | 同上 + `BootFile=pxelinux.0` | DISCOVER/REQUEST | OFFER/ACK 的 `file="pxelinux.0"`，`siaddr=0.0.0.0`（未配置 66 时为 0） | 与原版一致 | 通过 | 报文 `file`/`siaddr` 字段 |
| DH-04 | 同上 + `PoolSize=1` | MAC `…:44:88` 与 `…:44:99` 先后请求 | 第一个 MAC 获 `.200`（OFFER+ACK）；第二个 MAC 6s 内无 OFFER；日志 `[1] no more address or address previously allocated by another server` | 无 | 通过 | 探针输出；日志 |
| DH-05 | 同上 + `Option42=10.78.218.1`、`Option120=10.78.218.1` | DISCOVER/REQUEST | opt42=`0a4eda01`（长度 4）；opt120=`010a4eda01`（长度 5，RFC3361 子选项 1） | 无 | 通过 | 报文选项十六进制 |
| DH-06 | 同上 | `DHCP LocalIP=10.78.218.8` 时请求；改为 `10.0.0.1` 后重复 | 正例正常 OFFER/ACK；负例 6s 无应答，日志 `[1] Message received on an unbound interface (IP 10.78.218.8)` | 无 | 通过 | 探针输出；日志 |
| DH-07 | 同上 + `DHCP Ping=1` | DISCOVER | 首次 DISCOVER 记录一次 `WARNING: DHCP Ping/ARP 冲突检测在鸿蒙上不可用（无 raw socket），已降级为直接分配`，随后 OFFER/ACK 正常，不崩溃不卡住 | 平台偏差（无 raw socket，已登记） | 通过 | 日志页文本 |
| DH-08 | 同上 | 真实 RELEASE；再 DECLINE | RELEASE：条目保留、状态变"空闲"、时间归零，`Lease_NumLeases` 不变（原版设计 `do not destroy the item but mark it free`）；DECLINE：条目销毁，`Lease_NumLeases` 1→0 | 与原版一致（RELEASE 不删条目，仅 DECLINE 销毁） | 通过 | ini `Lease_NumLeases`；日志 `item released` / `item destroyed` |
| DH-09 | 同上 | 构造 `giaddr=10.78.218.1` + option82（子选项1 cirID=`eth0`、子选项2 remID=`relay01`）的 DISCOVER/REQUEST | OFFER/ACK 均回显 opt82=`010465746830020772656c61793031`；日志两次 `Adding option 82, length: 15 ; CirID: <eth0>; RemID: <relay01>`；租约正常分配 | 回包按请求源地址单播（原版亦按源端口回），giaddr 不用于定向转发；ACK 中 giaddr 字段为 0 | 通过（附平台说明） | 报文 opt82；日志 |
| NF-01 | 同上 | `aa force-stop` 后 `aa start` 计时至 PC 首次 GET 成功，3 次 | 0.54s / 1.13s / 0.59s，中位数 **0.59s** | 无 | 通过（≤2s） | 计时输出；GET SHA256 同 R2 |
| NF-02 | 同上 | 同时发起 10 个 TFTP GET，采 `VmRSS` | 10 路全部成功；VmRSS 基线 ~160MB → 并发中 162292kB → 结束后 162352kB，无持续增长。**2026-11-03 复核（pid 30127，native heap Pss 七点采样）**：T0 冷启动 25102 → 10MB GET×3 后 24946/25079/25067 → 10 路并发 10MB 中 25548 → 完成后 25291 → 空闲 30s 25411KB，波动 ±1.2% 无增长趋势；10 路 10MB 全部收完（100MB）、MD5 全一致、0 块重传 | ArkTS 运行时常驻已 ~157MB，绝对值超过“<100MB”阈值 | 已通过（2026-11-03，native heap Pss 口径，无泄漏） | `/proc/<pid>/status` 三次采样；10 路 GET 结果；hidumper 七点采样 |
| NF-04 | 同上 | 连续挂机 24h，每小时探测 TFTP/Syslog 并记 VmRSS | 已由复测 B-03 执行通过：25 轮全应答、无崩溃、端口不丢；VmRSS 首末 +53.1MB 经追踪为 jemalloc 脏页滞留 + VmRSS 采样噪声（native heap 静置回落，200 次 GET 净增 1.1MB），非泄漏；判据修订为 native heap Pss | — | 通过（复测 §1.2 / B-03） | 复测方案 B-03 记录 |
| NF-05 | 同上（本次 10.78.218.8:33111） | 开启服务后退到后台 8h（前 60min 用 `power-shell suspend` 熄屏锁屏，第 3 轮后 `wakeup`）；Windows 计划任务 `NF05_Probe` 每 30min 探测一次：`pidof` + `VmRSS` + `netstat -anu`（6900/5140）+ PC 侧 `curl tftp://…/test_1k.txt` 校验 SHA256 + Syslog UDP 探测 | **通过**：17 轮（round 0~16）全部成功，跨度 480min（00:24:33→08:25:04，8h00m31s），轮间隔实测均为 30min，无漏轮；GET 17/17 成功且 SHA256 均为 `617388A2…B54FF3B`；pid 全程 32168，无重启/崩溃；锁屏时长 60m33s（00:24:33~01:25:06），锁屏期轮 0~2 均可达；VmRSS 轮 0 99576kB（前台资源未回收）→ 轮 1 61696kB → 稳定 52~57MB（最小 52148，末轮 52992），无单调增长；Syslog 页可见 `10.78.218.198 <14>NF05 probe round=1…16`，锁屏期同样到达；测试结束（08:25）计划任务自动删除，09:36 复查进程/端口/GET 仍正常 | 无（`power-shell suspend` 本机仅熄屏，见平台偏差"休眠语义"） | 通过 | 系统日志 `has continuous task:1`、`ERR_HAS_CONTINUOUS_TASK`；17 轮探测日志；Syslog 页 dumpLayout 文本；结束后复查 netstat/GET |
| NF-06 | 同上 | 10MB `test_10m.bin` GET/PUT 各 5 次（blksize=16384） | GET：209.58 / 102.11 / 179.90 / 42.91 / 60.90 s，中位数 **102.11s（≈0.098MB/s）**，最快 0.233、最慢 0.048MB/s；PUT：19.77 / 19.31 / 18.83 / 19.16 / 18.60 s，中位数 **19.16s（≈0.52MB/s）**；10 次 SHA256/MD5 全部一致（GET SHA256 `21CDF320…ADC0B1`；PUT 设备侧 md5 `b743a96f573cbafd8571518e38f580db`）。**2026-11-03 判据修订关闭**：Wi-Fi 波动 42.91~209.58s 相差近 5 倍，任何基线对比无统计意义，判据改为绝对值+正确性（见未测报告 B-09） | 同机无原版 v4.74 可执行文件，5 倍趋势对比无法进行 | 通过（2026-11-03 判据修订，NF-06 关闭） | 逐次耗时；两侧 SHA256/MD5 |
| NF-07 | 同上 | Wi-Fi 断开重连 / 切热点后探测端口 | **无法执行**：shell 用户无 NET_ADMIN（`ifconfig wlan0 down` → `ioctl: Permission denied`）；且 hdc 无线调试本身走 wlan0，断开 Wi-Fi 会同时切断唯一控制通道且无法无人值守恢复 | — | 阻塞（环境受限） | `ifconfig` 报错输出 |
| NF-08 | 同上 + `Services=63`（5 服务全开） | `power-shell suspend` → 120s 内不发 hdc 流量，每 20s ping + TCP 34101 探测 → `power-shell wakeup` → 复查端口与功能 | suspend 后 t=24s ping 不通（熄屏瞬间），t=45~125s ping/TCP34101 全部可达；设备时间仅推进 132s；唤醒后 5 端口（6900/5140/1230/5300/6700）全部仍在监听，pid 不变（20140）；复测 TFTP GET 1405B SHA256 `617388A2…B54FF3B` 一致、Syslog 收到、DHCP OFFER+ACK（yiaddr .201）、DNS 探针返回 A 记录 `0a4edac6` | `power-shell suspend` 在本机仅熄屏，系统与网络未深睡，无法模拟真正休眠回收 | 通过（附平台偏差） | ping/TCP 时序；`netstat -anu`；复测报文与 SHA256 |
| UI-06 | 同上（本次 192.168.10.128:38691；PC 192.168.10.104） | force-stop 冷启动观察授权弹窗 → 设置页确认（端口 6900/安全级别=标准/四子服务开）→ 日志页验证 → hilog 过滤 9800005/continuous → 通知中心截图 → 最小化退后台 ≥1min 后 PC GET → 停服复查通知与监听 | 授权态=已允许（冷启动无弹窗）；日志 `TFTP 服务已启动（端口 6900）`；hilog 双快照（冷启动期+运行期）grep 9800005/1600004 零匹配；`SaveNtf __0_1096_…_bgmode_20020154_…`（系统长时任务 bgmode 常驻通知被 SystemUI 接收）；通知中心首条 Tftpd32 🔒通知；退后台 ~4min 后 GET test_1b.bin 成功 MD5 `2408AD11F9EB830DA749E2A36A29EFF7` 一致（pid 存活、6900 监听在位）；停服后通知消失且 `/proc/net/udp6` 1AF4 计数归 0 | 判据③（拒绝授权中文引导）并入 UI-09，本次授权态为"已允许"未复现拒绝路径；curl exit=28 系 PC 侧 Windows curl 对 1 字节尾块的怪癖（文件完整、get_client MD5 一致），非应用缺陷 | 通过（2026-11-03） | ui06_s1_cold / ui06_s2_settings / ui06_s2_log / ui06_s4_notif3 / ui06_s6_a / ui06_s6_b.jpeg；ui06_s1_hilog / ui06_s3_hilog.txt；get_client 输出 MD5 |
| UI-07 | 2in1 真机（HAD-W32，HarmonyOS 6.1.0.117） | 全屏/分屏/自由窗口三形态切换、拖拽改尺寸、Tab/Shift+Tab 遍历、Enter 激活 | 全屏与分屏布局完整不截断（设置/客户端/DHCP 页证据，页签栏 9 项完整）；自由窗口页签横向滚动正常、中等尺寸可读性中上、缩至最小不可读直至仅剩标题栏（系统允许的极限窗口）；Tab/Shift+Tab 焦点框全程可见并贯穿页签栏（"设置""客户端""DHCP"页签焦点框留证）；焦点落"服务开关"按 Enter 服务启停生效；焦点落页签自动切至对应页 | ①自由窗口最小尺寸不可读：系统允许极小窗口所致，记观察项；②滚轮切页签不一致（缺陷待统一）：内容短页（无 Scroll 包裹）滚轮冒泡至 Tabs 快捷切页、设置页（有 Scroll）仅滚动内容，快速滚轮易误切——并入 §1 触发式开发项统一逻辑 | 通过（2026-11-10；布局不截断/页签可横向滚动/键盘操作完整三判据均满足） | docs\test-evidence\UI-07\（13 张：20261103×9 全屏 Tab 逐页签遍历含焦点框、20261110×4 分屏 DHCP 完整/全屏页签焦点框/自由窗口最小尺寸×2） |
| UI-08 | 2in1 真机（HAD-W32） | 深色模式 / 显示大小 / 字体大小 / 旋转 逐项切换 | 深色模式：全屏日志页与设置页完整深色适配，文字与控件可读、无重叠；显示大小调整：应用元素同步放大、布局无重叠；字体大小调整：应用字号不变（偏差）；旋转：设备为 2in1 笔记本形态不支持旋转，用例不适用 | 字体大小不跟随系统缩放（"显示大小"正常跟随）——记偏差，待排查 fontSize 缩放联动，并入 §1 触发式开发项；旋转不适用系设备形态限制（非应用缺陷） | 通过（2026-11-10；"可读、无重叠"判据在适用条件内满足，2 项适用通过 + 1 项偏差 + 1 项设备限制不适用） | docs\test-evidence\UI-08\（5 张：深色模式设置操作、深色日志页、深色设置页、显示大小调整后 DHCP 页、字体大小调整后设置页） |
| SG-02 | 2in1 真机（HAD-W32）+ PC 192.168.10.104（tftpd64 v4.74 @6901） | 按 §10 清单真机拍摄 7 张：①设置页（服务运行中/端口 6900/启用 IPv4）②TFTP 页 10MB GET 传输中（60% 进度、ret=0）③日志页（`sent 20481 blks` + `sent 3 blks` + `Error sg02_err1.bin` 同屏）④DHCP 页（地址池 10.78.218.200×5 + 租约表 5 条，含探针双 MAC 02:11:22:33:44:88→.200、02:11:22:33:44:99→.201）⑤客户端页 GET `192.168.10.104:6901 test_1k.txt` → done（1902B/块 4/错误码 0）⑥目录页（files 根列表 + MD5 按钮）⑦自由窗口形态复用 UI-07 截图 | 材料一致性核对通过：README 权限三条与实测一致（INTERNET=监听收发、GET_NETWORK_INFO=网卡枚举绑定、KEEP_BACKGROUND_RUNNING=长时任务，UI-06 已证），GPLv2 与源码地址无误；10MB 下载件与设备 test_10m.bin MD5 一致（DD908EF6F9257C74D9E4BE806050F124）传输无损 | 无（材料存 `docs\test-evidence\SG-02\` 英文路径，替代指南原定 `docs\Tftpd32-市场材料\`） | 通过（2026-11-10） | docs\test-evidence\SG-02\（SG02_01~07 共 7 张） |
| UI-09 | 2in1 真机（HAD-W32）+ PC（本次 10.188.157.x 网段，设备 10.188.157.8:38785） | GET_NETWORK_INFO 腿：`bm dump` 权限授权模式核查 + 系统设置权限页核查 + 绑定网卡下拉基线留证；通知腿：通知允许基线 → 撤销 → 启动服务 → PC GET `test_1k.txt` → 恢复权限复验 | 网络腿不适用：三条权限均 system_grant（bm dump reason 空、`reqPermissionStates=[0,0,0]`，module.json5 未声明 reason/usedScene），系统设置权限页无网络权限项可撤销；通知腿通过：撤销后服务正常启动运行、GET 1024B MD5 `73F9EBD42260DB128B26155773C4710F` 与设备基准一致，系统连续任务通知绕过应用通知开关仍显示（后台保活无损），中文提示不触发（长时任务成功、降级发布路径不可达——Index.ets 仅 `!ctRunning` 才 `publishRunningNotice`）；恢复权限后一切正常、无需重装 | 撤销 GET_NETWORK_INFO 降级路径在鸿蒙 system_grant 授权模式下不可触发，D7 该分支保持代码覆盖；UI-06 判据③按实际行为记录：提示分支存在但本机不可达 | 通过（2026-11-11；通知腿通过、网络腿不适用） | docs\test-evidence\UI-09\（6 张：绑定网卡基线、权限页无网络权限、通知允许基线、通知已撤销、撤销后服务运行、恢复后正常） |
| NF-07 | 2in1 真机（HAD-W32）+ PC 10.188.157.198（设备 10.188.157.8:38785，hdc 走 wlan0） | PC 起 10s 间隔连续 GET 探测（get_client.py，test_1b.bin）作断网期唯一取证 → 人工设置断开 Wi-Fi ~1min49s → 重连 → hdc 重连核查进程/端口/hilog | 断网前 74 次探测全 DONE；断网窗口 00:59:25~01:01:14 连续 9 次 TimeoutError（预期失联）；重连后 **01:01:45 首次 DONE**、MD5 `2408AD11F9EB830DA749E2A36A29EFF7` 与基准一致——**服务自动恢复、无需重启**（监听 socket 未随断网丢失）；进程 pid 20020154 全程不变（未崩溃未重启）、6900（udp6）监听在位、wlan0 IP 未变；hilog 窗口快照本项目进程无 died/crash/fault/kill 记录（30 条命中系其它系统进程被 memmgr 回收、142 条 "fault" 命中系 `Default` 子串误匹配） | 无（hdc 无线通道随断网失联属预期，断网期间仅 PC 探测取证；切热点路径无第二热点环境未测） | 通过（2026-11-11；不崩溃 + 恢复后端口重新可达两判据均满足） | docs\test-evidence\NF-07\（nf07_hilog_before.txt / nf07_hilog_after.txt / nf07_probe_log.txt） |
| SG-01 | 2in1 真机（HAD-W32，HarmonyOS 6.1.0.117）+ PC 192.168.10.104（设备 192.168.10.128，hdc 无线 42217）；签名材料=AGC 发布证书 tftpd32-HS.cer/.p12 + 内部测试 profile T32-testRelease.p7b（type=release、distribution-type=internaltesting、device-ids 绑定本机 UDID、内嵌 Release 证书 CN=张成岚(2059363319311009985),Release，2026-11-11~2029-11-11）；build-profile.json5 签名配置已切换 | ①切换签名 profile 后 v1（versionCode 1000000）hvigor release 构建+SignHap → 首装成功（此前 9568322 拒装根因：发布 profile distributionType=app_gallery 仅市场渠道、侧载受限，internaltesting profile 解除）→ 启动制造升级前状态：设置页配置 DHCP 池（192.168.10.200/255.255.255.0/5）保存、PUT 基准文件 sg01_base.bin（1024B，GET 回读 MD5 `22755BF4F31F13CF5B5FF8199186E5A9` 双端一致）、探针双 MAC 产生租约 .200/.201（OFFER+ACK 全通，opt51=172800s；no OFFER 系配置未保存，保存后立即出租约）；②AppScope/app.json5 bump versionCode→1000001 → hvigor release 重建重签 → `hdc install -r` 覆盖安装；③`bm dump` 核对 → 冷启动 → 行为验证 | versionCode 1000000→1000001 覆盖升级成功（install bundle successfully）；`appIdentifier=6917621457942072563` 与 firstInstallTime（v1 首装 13:06）均不变——升级而非卸装重装；升级后**未手动操作**即双服务自动恢复监听（6900 udp6 + 6700 udp，pid 20020157）——ini 服务开关保留；GET 基准文件 1024B MD5 `22755BF4…86E5A9` 与升级前一致——BaseDirectory 与 files 保留；DHCP 探针同 MAC 重分得**同 IP** .200/.201——Lease_* 持久租约保留 | release 包不可调试（shell 无法读 /data/app/el2 沙箱），指南步骤④「ini 逐项 diff」以三重行为证据替代（服务自动恢复 + GET MD5 + 同 MAC 同 IP 租约重分配）；旧 debug 版 ini 键值结构参考 `sg01_old_debug_ini_backup.txt` | 通过（2026-11-11；安装成功/配置与租约保留/服务可启动三判据全成立） | docs\test-evidence\SG-01\sg01_record.txt（时间线+命令输出）；签名材料 signature\T32-testRelease.p7b |

真机阶段可接受的平台偏差（判为“平台偏差”而非缺陷，须附证据）：

| 偏差 | 说明 |
|---|---|
| DHCP 冲突检测降级 | 鸿蒙无 raw socket，`DHCP Ping=1` 仅告警后分配（DH-07） |
| DHCP 回包方式 | 服务端按请求源地址判定是否单播：源地址非 0/非 255.255.255.255 时直接单播回源端口。PC 侧向 `255.255.255.255:6700` 广播在本机环境不可达（防火墙/无线驱动），DH-01/04/06/09 均以单播到 `10.78.218.8:6700` 完成验证；真实二层广播客户端（手机/Android）行为需在同网段终端复核（DH-01 备注） |
| 端口非 69 | 应用无法绑定 1024 以下端口，默认 6900（R5 验证提示文案） |
| 组播接收权限 | 若系统策略限制加入组，记录为平台偏差并在市场材料注明 |
| 长时任务被拒 | 部分机型/权限状态下 `TASK_KEEPING` 不可用，降级为常驻通知（UI-06） |
| UI 布局差异 | 允许与原版 Win32 布局不同，只判可读与可操作（UI-07/UI-08） |
| 内存阈值不适用 | ArkTS 运行时（含 UI 框架）常驻已 ~157MB，NF-02 的“<100MB”阈值对鸿蒙不适用；实际判据修订为 native heap Pss 无单调增长（复测 B-03 定口径；2026-11-03 七点采样 24946~25548KB 无增长趋势） |
| 休眠语义 | `power-shell suspend` 在本机（HAD-W32 / 6.1.0.117）仅熄屏，系统与网络保持唤醒，无法模拟真正深睡下的进程回收；NF-05/NF-04 的长时挂机才是有效判据 |
| 无原版同机基线 | 设备侧无法运行原版 `tftpd32.exe`（Windows PE 二进制），NF-06 的“不劣于原版 5 倍”趋势指标无法比对；NF-06 已按修订判据（绝对值+正确性）于 2026-11-03 关闭 |

本轮真机测试发现的缺陷（按定位到的代码位置）：

| 编号 | 级别 | 现象 | 根因 | 位置 |
|---|---|---|---|---|
| D5 | P1 | 双栈时只监听 IPv4，`netstat` 无 `udp6` | `AF_UNSPEC` 结果直接取 `res[0]`，未优先选 `AF_INET6` | `tftpd_main.c` 解析地址处 |
| D6 | P1 | 纯 IPv6 仍接受 IPv4（v4-mapped） | 仅监听 IPv6 时未设 `IPV6_V6ONLY=1` | `tftpd_main.c` L128 附近 |
| D7 | P1 | 组播大文件在随机块号处 `MAX RETRIES while waiting for Ack block N` 中止；重传在 <1s 内连发 50 次后放弃 | `IP_MULTICAST_LOOP=1` 使服务端收回自身 DATA；`TftpRecvTrfPacket` 丢弃后返回 0，`TftpSendFile` 走 `Rc==0 → continue`；此时 `nTimeOut>0` 会重置 `nLastToSend=nCount`，于是每轮立即重发 → `nRetries` 迅速撞到 `TFTP_MAXRETRIES(50)` | `tftpd_thread.c` L145-L164、L879、L920、L931 |
| D8 | P1 | 组播中止时 `ERROR code=0` 被发往组播组，同组所有客户端同时被打断 | `nak()` 走 `TftpSendTrfPacket`，组播模式下目标为组地址 | `tftpd_thread.c` L237 |
| D9 | P2 | 绑定 1024 以下端口失败时日志为英文 `Permission denied` | 未走 `PalSvcErrText` 的中文专用文案 | R5 结果 |
| D10 | P2 | 设置页读写的组播地址键名为 `MulticastAddrQAZwsx0$vwwwwwess`，ini 中为 `MulticastAddress` | UI 侧键名与原生设置不一致 | `Index.ets` L18-L19 |
| D11 | P3 | Syslog/SNTP/DNS 页"端口 "数值显示空白 | 页面取值未绑定到对应 Setting 键 | 各页 ArkTS |
| D12 | P2 | 根目录被硬编码，不随 ini `BaseDirectory` 变化 | `Index.ets` L63 写死 `tftpboot` | `Index.ets` L63 |

## 13. 阻塞与放行条件

- 无真机时全部用例阻塞；本文档不包含替代验证手段。
- 发布门禁：G1 与 G2 的 P0 用例 100% 通过；DH-01、NF-05、R8、R9 通过（双栈与纯 IPv6 各一次）；
  D1/D2 若无真机结论，视为未交付。
- 组播接收/ACK（D1）、DHCP 广播（D2）、IPv6 对外（D5/R8/R9）、
  正式签名与截图（D8/D9/SG-01/SG-02）曾是当前版本与“可交付”之间的全部差距；
  其中 SG-02 已于 2026-11-10、SG-01 已于 2026-11-11 真机执行闭环（见 §12），
  当前仅剩 R8 IPv6 腿待 IPv6 环境（双侧均无 IPv6，2026-11-03 复查维持）。
  后台 8h（D3/NF-05）已于 2026-10-29 真机通过（17 轮 / 480min，含 60m33s 熄屏锁屏，见 §12 NF-05），
  D3 的“长时任务被系统回收”风险据此关闭；NF-04（24h）仍需单独挂机窗口。
