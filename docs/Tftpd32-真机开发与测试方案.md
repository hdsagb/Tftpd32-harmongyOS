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
| D4 | 休眠/唤醒与网络切换后的端口恢复 | 目前无网络状态监听，套接字在启动时一次性创建 | NF-07、NF-08 通过 | 若唤醒或切网后端口丢失：引入网络状态订阅，在 `netAvailable` 后自动重启服务并写日志 |
| D5 | IPv6 对外形态与 `LocalIP` 绑定 | 监听地址族由 `Enable IPv4`/`Enable IPv6` 决定，`TftpBindLocalInterface` 按 `LocalIP` 绑定 | R7~R9 通过 | 双栈下 IPv4 对端被显示为 v4-mapped 时补归一化；绑定失败需在日志给出可读原因 |
| D6 | 2in1 窗口形态与键盘操作 | 全量支持全屏/分屏/自由窗口，Tab 遍历与 Enter 激活已实现 | UI-07 通过 | 按真机窗口尺寸调整断点与页签滚动策略 |
| D7 | 权限拒绝后的降级 | 网卡枚举与通知均有失败分支 | UI-09 通过（撤销权限后界面可用、有中文提示） | 网卡枚举失败时下拉固定为“全部接口”并解释原因；权限引导文案补齐 |
| D8 | 正式签名与覆盖升级 | 当前仅有本地调试签名（`tools/build-install.ps1`） | SG-01 通过 | 申请发布证书与 profile；确认升级后 `tftpd32.ini` 与租约表保留 |
| D9 | 市场材料截图 | 文字材料已就绪，截图必须真机拍摄 | SG-02 通过（§10 的 7 张清单） | 按清单补拍，标注页签名 |

触发式开发项（只有真机测试暴露问题时才产生，修复同样在本阶段完成）：

- NF-04/NF-05 出现崩溃、内存单调增长或端口丢失 → 定位并修复（含 D4 的恢复机制）。
- NF-06 吞吐明显劣于原版基线 → 调优默认 `blksize`、减少拷贝与日志写入。
- R5/R6 的提示文案与真机实际错误码不一致 → 按真机返回码修正中文提示。
- UI-07/UI-08 在真机尺寸下截断或错位 → 调整断点与页签滚动策略。

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
| G1 通路 | R1 安装/启动/端口监听；R2 局域网可达；R3 启停；R4 前后台；R5 特权端口；R6 端口占用；R7 网卡绑定；R8 双栈；R9 纯 IPv6；R10 配置迁移 | P0/P1/P2 | D4/D5 | 未执行 |
| G2 报文对拍 | TF-01/02/04、OP-01~08、RE-01/03/05、TC-01/02、SL-01、ST-01、DN-01~03、MD-02 | P0/P1 | D5 | 未执行 |
| G3 组播 | MC-01 OACK 协商、MC-02 组播接收、MC-03 组播 ACK、MC-04 双客户端、MC-05 非法地址 | P1/P2 | D1 | 未执行 |
| G4 DHCP | DH-01~05 广播版、DH-06 多网卡、DH-07 Ping 降级、DH-08 释放、DH-09 option 82 | P0/P1/P2 | D2 | 未执行 |
| G5 非功能 | NF-01 冷启动、NF-02 并发内存、NF-04 24h、NF-05 后台 8h 含锁屏、NF-06 吞吐、NF-07 切网、NF-08 休眠唤醒 | P1/P2 | D3/D4 | 未执行 |
| G6 平台形态 | UI-06 长时任务/通知、UI-07 2in1、UI-08 深色/DPI/旋转、UI-09 权限拒绝 | P1/P2 | D3/D6/D7 | 未执行 |
| G7 交付 | SG-01 正式签名与升级、SG-02 市场材料 | P0/P2 | D8/D9 | 未执行 |

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

## 8. G5 非功能

| 编号 | 优先级 | 步骤 | 预期 |
|---|---|---|---|
| NF-01 | P1 | 冷启动计时：`aa start` 到 PC 首次 GET 成功 | ≤2s（3 次中位数） |
| NF-02 | P1 | 同时发起 10 个 TFTP 传输，`hdc shell "cat /proc/$(pidof com.example.myapplication)/status"` 观察 VmRSS | <100MB，无持续增长 |
| NF-04 | P1 | 连续挂机 24h，每小时探测一次 TFTP/Syslog，记录 VmRSS | 无崩溃、无单调内存增长、端口始终可达 |
| NF-05 | P1 | 开启服务后退到后台 8h（其中 ≥1h **锁屏灭屏**），PC 每 30min 发 TFTP GET + Syslog | 全程有应答；系统未杀进程；锁屏期间同样可达 |
| NF-06 | P2 | 10MB GET/PUT 各 5 次记录 MB/s；同机原版 v4.74 同操作对比 | 不劣于原版同机结果的 5 倍（趋势指标） |
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
|  |  |  |  |  |  |  |

真机阶段可接受的平台偏差（判为“平台偏差”而非缺陷，须附证据）：

| 偏差 | 说明 |
|---|---|
| DHCP 冲突检测降级 | 鸿蒙无 raw socket，`DHCP Ping=1` 仅告警后分配（DH-07） |
| 端口非 69 | 应用无法绑定 1024 以下端口，默认 6900（R5 验证提示文案） |
| 组播接收权限 | 若系统策略限制加入组，记录为平台偏差并在市场材料注明 |
| 长时任务被拒 | 部分机型/权限状态下 `TASK_KEEPING` 不可用，降级为常驻通知（UI-06） |
| UI 布局差异 | 允许与原版 Win32 布局不同，只判可读与可操作（UI-07/UI-08） |

## 13. 阻塞与放行条件

- 无真机时全部用例阻塞；本文档不包含替代验证手段。
- 发布门禁：G1 与 G2 的 P0 用例 100% 通过；DH-01、NF-05、R8、R9 通过（双栈与纯 IPv6 各一次）；
  D1/D2 若无真机结论，视为未交付。
- 组播接收/ACK（D1）、DHCP 广播（D2）、后台 8h（D3/NF-05）、IPv6 对外（D5/R8/R9）、
  正式签名与截图（D8/D9/SG-01/SG-02）是当前版本与“可交付”之间的全部差距。
