# Tftpd32 for HarmonyOS：手机真机测试方案

> 版本：1.0（2026-11-11）
> 适用包：`io.github.hdsagb.tftpd32harmongyOS`，versionCode 1000002 / versionName 1.0.0
> 基线：`docs/Tftpd32-真机开发与测试方案.md` 已在 2in1（HUAWEI MateBook Pro HAD-W32，HarmonyOS 6.1.0.117）完成 G1~G7 全量执行并闭环。
> 本方案不重复协议对拍的全量设计，只做**手机形态与平台行为差异**的增量测试，并对 2in1 上已验证的协议用例做**抽样回归**（沿用原用例编号）。

---

## 1. 目的与范围

### 1.1 目的

验证同一 HAP 在手机上：

1. 能完成安装、启动、权限申请与市场分发链路；
2. 在小屏、横竖屏、分屏/悬浮窗、手势与软键盘下 UI 可用；
3. 在手机更激进的后台/省电策略下，TFTP 等服务仍满足"退后台/锁屏可用"；
4. 在 Wi-Fi、热点、蜂窝、断连恢复等手机典型网络切换下行为可预期；
5. 关键协议路径与 2in1 基线一致，性能/功耗无阻断性问题。

### 1.2 范围

| 纳入 | 不纳入（沿用既有结论） |
|---|---|
| 手机安装/签名路径、首启权限、通知与长时任务 | TFTP 报文级全量对拍（OP/RE 组合已在 2in1 通过，仅抽样回归） |
| 手机窗口形态、软键盘、字体/深色、手势 | windowsize 协商（原版 v4.74 本就不实现） |
| 后台/锁屏/重启/强停/低电量 | DHCP Ping/ARP 冲突检测（平台无 raw socket，已降级并提示） |
| Wi-Fi/热点/蜂窝/断连恢复 | IPv6 外网可达性（原 R8 IPv6 腿已按环境豁免，手机如具备条件可补测） |
| DHCP（隔离网络 + 报文探针）、组播（同 AP） | 服务端实现正确性本身（GPLv2 衍生逻辑不改动） |
| 性能/功耗/温度、升级与市场分发 | 市场审核文案与截图（SG-02 已归档，提交前复核即可） |

### 1.3 手机与 2in1 的差异（本方案的测试来源）

| 差异点 | 手机 | 对测试的影响 |
|---|---|---|
| 系统门槛 | 需 HarmonyOS 6.1 / API 23 及以上（`compatibleSdkVersion 6.1.0(23)`） | 低于该版本的手机**无法安装**，属硬门槛 |
| 窗口形态 | 支持旋转、分屏、部分机型悬浮窗 | 小屏横向布局、固定宽度控件、Tab 滚动是新风险 |
| 输入方式 | 纯触控 + 软键盘 | 端口/IP 输入、键盘遮挡、非法值校验需重测 |
| 后台策略 | 比 2in1 更激进，受电池优化/应用启动管理影响 | 8h 挂机、锁屏、低电量场景必须重测 |
| 网络 | Wi-Fi / 蜂窝 / 热点 / 频繁切换 | 断连恢复、LocalIP 变化、蜂窝不可达需定义预期 |
| 分发 | 正式包走应用市场；调试包需绑定手机 UDID | 当前仓库签名是 app_gallery 正式 profile，**不能直接 hdc 侧载** |
| 通知 | 首启需授权，锁屏展示受系统策略控制 | 通知拒绝后的降级路径需重测 |

---

## 2. 准入检查（未通过则暂停执行）

| 编号 | 检查项 | 方法 | 通过标准 |
|---|---|---|---|
| G0-1 | 系统 API 版本 | `hdc shell param get const.ohos.apiversion`、`hdc shell param get const.product.softwareVersion` | API ≥ 23 且系统 ≥ HarmonyOS 6.1；否则记录型号/版本，转为"升级系统"或"调整 compatibleSdkVersion"决策项 |
| G0-2 | 设备类型与架构 | `hdc shell param get const.product.model`；`hdc shell bm dump -n io.github.hdsagb.tftpd32harmongyOS` | 型号为手机；模块 `deviceTypes` 已含 `phone`（已确认），arm64 设备 |
| G0-3 | 调试通道 | USB 连接优先；`hdc list targets` | 目标稳定可见；网络切换/重启类用例必须走 USB，避免无线调试掉线 |
| G0-4 | 签名可安装 | 见 §4.4 路径 A/B | 能安装并启动；若 hdc 拒装，记录错误码并切换签名路径 |
| G0-5 | 网络可达 | 手机与 PC 同一 AP；`ping <DEV_IP>`、`hdc shell ping -c 3 <PC_IP>` | 双向可达，AP 未开启客户端隔离 |

> 说明：仓库历史文档中的 `com.example.myapplication` 已废弃，所有命令与取证一律使用当前 bundle `io.github.hdsagb.tftpd32harmongyOS`。

---

## 3. 测试环境

### 3.1 设备矩阵

| 编号 | 设备 | 要求 | 用途 |
|---|---|---|---|
| D1 | 主力手机 | HarmonyOS 6.1+/API 23+，arm64，6.5~6.8 英寸，8GB | 必测，执行全部 P0/P1 |
| D2 | 次级手机 | HarmonyOS 6.1，6GB，1080P（约 360vp 宽） | 必测（如具备），专测小屏布局与低内存 |
| D3 | 折叠屏 | 可选 | 展开/折叠窗口尺寸变化（等价 2in1 窗口变化） |
| D4 | 2in1 基线机 | HAD-W32 | 仅在新版本回归时做冒烟，确认无回归 |

> 若只有一台手机，D2 项合并到 D1，并在报告中标注"单一机型，小屏覆盖不足"。

### 3.2 网络拓扑

| 拓扑 | 组成 | 用途 | 关键约束 |
|---|---|---|---|
| T1 普通局域网 | 手机与 PC 接同一路由器/同一 AP | TFTP、Syslog、SNTP、DNS、客户端、组播、断连恢复 | AP 必须关闭"客户端隔离/AP 隔离"；PC 防火墙放行 UDP 6900/5140/1230/5300/6700 与 69/6901 |
| T2 隔离 LAN（DHCP 专用） | 关闭 DHCP 的路由器或独立随身 Wi-Fi；手机、PC 均设静态 IP 同网段 | DHCP 租约（探针打 6700） | **不得在 T1 上启用 DHCP 服务**，避免与路由器 DHCP 冲突；DHCP 端口 6700 非标准，操作系统自带 DHCP 客户端不会命中，必须用探针 |
| T3 手机热点 | 手机开热点，PC 连接该热点 | TFTP 服务端/客户端通路 | 必须**关闭 DHCP 服务**（系统热点自带 DHCP 占用 67，语义冲突）；hdc 走 USB |
| T4 蜂窝 | 关闭 Wi-Fi，仅数据网络 | 断网恢复与预期定义 | 蜂窝侧无公网入口，PC 不可达属预期，不作为缺陷 |

### 3.3 PC 侧工具与测试文件

| 工具/文件 | 路径 | 用途 |
|---|---|---|
| hdc | `D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe` | 安装、启停、日志、截图 |
| `get_client.py` / `put_client.py` | `C:\Users\32572\AppData\Local\Temp\` | `python get_client.py <ip> <port> <remote> <local> [timeout]`；PUT 同理 |
| `dhcp_probe.py` | `C:\Users\32572\AppData\Local\Temp\` | `python dhcp_probe.py <ip> 6700`，构造 DISCOVER 并打印 OFFER/ACK 选项 |
| `tftpd64.exe` v4.74 | `D:\trae\tfpd32\tftpd64.exe`（管理员，TftpPort=6901） | 作为"应用客户端"页的对端基准 |
| `otftp2` / `rtool` | 既有构建产物 | 选项、组播、异常报文对拍（抽样） |
| 测试文件 | `D:\trae\tfpd32\root\`（test_1b.bin / test_1k.txt / test_10m.bin） | 文件一致性与吞吐 |
| `tools\snapshot.ps1` | 仓库内 | 截图取证；手机需显式传 `-Device <手机IP:端口>`，USB 连接时可传任意值 |

### 3.4 构建、签名与安装（两条路径）

**路径 A：功能测试（推荐，DevEco 自动签名）**

1. 手机 USB 连接 DevEco，登录华为开发者账号；
2. `File > Project Structure > Signing Configs` 勾选 **Automatically generate signature**（会生成绑定当前手机 UDID 的调试 profile）；
3. `Build > Build Hap(s)/APP(s) > Build Hap(s)`，或：

```powershell
$deveco='D:\Program Files\Huawei\DevEco Studio'
$env:DEVECO_SDK_HOME="$deveco\sdk"
& "$deveco\tools\hvigor\bin\hvigorw.bat" --mode module -p product=default `
    -p buildMode=debug assembleHap --no-daemon
```

4. 安装与启动：

```powershell
$hdc='D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
& $hdc install -r "entry\build\default\outputs\default\entry-default-signed.hap"
& $hdc shell aa start -a EntryAbility -b io.github.hdsagb.tftpd32harmongyOS
```

**路径 B：仓内自签调试包（备选）**

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -BuildMode debug -Launch
```

脚本会读取当前连接设备的 UDID 写入 debug profile。项目经验显示部分真机会以 `9568257` 拒绝此类自签 profile，若失败立即回退路径 A。

**路径 C：交付验证（应用市场/内测）**

当前 `signature\Tftpd32-HSRelease.p7b` 为 `release / app_gallery` 类型，**不用于 hdc 侧载**。交付类用例（PH-54~PH-56）须通过 AGC 内测通道或正式上架安装。

> 重要：执行路径 A 会改写 `build-profile.json5` 的签名材料；功能测试结束后须恢复仓库既有的 AGC 正式签名配置再出正式包，避免把调试签名带入交付物。

---

## 4. 用例集

优先级：P0 阻断/必过；P1 核心；P2 补充。
"基线编号" 指《真机开发与测试方案》中的既有用例，手机回归时沿用同一判据。

### 4.1 PH-A 准入与安装（P0）

| 编号 | 优先级 | 步骤 | 预期结果 | 取证 |
|---|---|---|---|---|
| PH-01 | P0 | 读取 API/系统版本、型号、UDID | API ≥ 23；记录型号与 UDID；UDID 与调试 profile 一致 | 命令输出文本 |
| PH-02 | P0 | 直接 `hdc install` 当前 `entry-default-signed.hap`（app_gallery release） | 预期被拒（正式 profile 不支持侧载）；记录错误码，改用路径 A | hdc 输出 |
| PH-03 | P0 | 路径 A 安装 → 桌面图标 → 冷启动 | 安装成功；图标/名称正确；首次进入无白屏/闪退；冷启动至可交互 ≤ 2.5s（首启权限弹窗不计） | 截图 + 计时 |
| PH-04 | P0 | 首次开启服务，触发通知授权 | 弹出系统通知授权；允许后长时任务申请成功（hilog 无 `9800005`）；通知常驻可见 | 截图 + hilog 片段 |
| PH-05 | P1 | 拒绝通知授权后再开服务 | 服务仍可启动；设置页出现中文可读原因；通知降级；不崩溃 | 截图 |
| PH-06 | P1 | `bm dump` 校验版本与 CPU ABI | versionCode=1000002；ABI 含 arm64 | 命令输出 |

### 4.2 PH-UI 手机形态与交互（P0/P1）

| 编号 | 优先级 | 步骤 | 预期结果 | 取证 |
|---|---|---|---|---|
| PH-07 | P0 | 竖屏、默认字体，遍历 9 个页签（设置/TFTP/日志/Syslog/SNTP/DNS/DHCP/客户端/目录） | 页签可横向滚动且可点击；无页签被裁切；各页可滚动到底部 | 每页 1 张截图 |
| PH-08 | P0 | 小屏（约 360vp 宽）检查设置页行布局：`Text` 固定宽 160 + 开关 + 端口说明、端口/IP 输入框 | 不溢出屏幕；输入框与开关完整可点；说明文字换行不遮挡 | 截图 + `uitest dumpLayout` |
| PH-09 | P1 | 打开自动旋转，竖→横→竖各停留，切换页签与输入 | 布局自适应；横屏下内容仍可滚动；运行状态/配置值/当前页签保持；无重叠 | 横竖屏各 1 张 |
| PH-10 | P1 | 与另一应用分屏（上下分屏），在分屏内操作服务开关与页签 | 服务不中断；UI 在缩减高度下可用；页签仍可滚动 | 截图 + PC 端 GET 成功 |
| PH-11 | P2 | 悬浮窗/智慧多窗（若机型支持）缩小窗口 | 最小尺寸下关键控件可见可点；无崩溃 | 截图 |
| PH-12 | P1 | 系统字体调至最大档，重启应用 | 文字不截断、按钮不被挤出屏幕；若字号不跟随，按既有偏差记录（不得判为阻断） | 截图（与默认档对照） |
| PH-13 | P2 | 修改"显示大小"档位，重启应用 | 布局正常，无遮挡 | 截图 |
| PH-14 | P2 | 深色模式开/关，杀进程后重启 | 背景/文字对比可读；跟随系统设置 | 深/浅各 1 张 |
| PH-15 | P1 | 软键盘输入：端口、IP、目录、客户端 host/port；分别输入 `0`、`70000`、`abc`、空值、重复端口 | 数字键盘；输入框自动滚入可视区；非法值有提示或被拒绝；重复端口绑定失败有中文提示；不崩溃 | 截图 + 日志 |
| PH-16 | P1 | 手势：侧滑返回、手势导航、从最近任务上滑关闭；观察服务图标与通知 | 侧滑不误触退出；上滑关闭进程后服务停止、通知消失（预期）；重新启动应用后服务按配置恢复 | 截图 + PC 探测 |
| PH-17 | P2 | 触控目标与滚动流畅度：主开关、保存/启动按钮；长日志列表滑动 | 主控件易于点中；列表滑动无明显卡顿/掉帧 | 目测记录（可选 dumpLayout 边界） |
| PH-18 | P1 | 9 个页签往返切换 3 轮，期间观察 hilog | 配置/日志/租约不丢；长时任务不重复申请（仅一次成功记录） | hilog |

### 4.3 PH-BG 后台与生命周期（P0）

| 编号 | 优先级 | 步骤 | 预期结果 | 取证 |
|---|---|---|---|---|
| PH-19 | P0 | 启动服务后按 Home 退后台 30 分钟；PC 每 5 分钟 GET 一次 | 每次均成功；进程存活；常驻通知在通知栏可见 | 探测时间戳 + 通知截图 |
| PH-20 | P0 | 服务运行中锁屏 30 分钟；PC 每 5 分钟 GET | 锁屏期间持续可达；解锁后 UI 状态与后台一致 | 探测记录 + 锁屏截图 |
| PH-21 | P0 | 夜间挂机 8 小时（含 ≥1 小时灭屏），PC 每 30 分钟 GET + Syslog 探针；PC 关闭睡眠 | 全程有应答；无崩溃/被杀；端口始终监听；记录电量下降 | 探测日志 + 电量截图 + `ps`/`netstat` |
| PH-22 | P1 | 设置 > 应用启动管理：分别为"自动/手动（允许后台）"；观察 1 小时 | 自动管理下若被限制，记录现象；手动允许后应稳定；设置页已有中文引导 | 截图 + 探测记录 |
| PH-23 | P1 | 开启省电模式/低电量模式，重复 PH-19 的 30 分钟探测 | 记录是否被降级/中断；恢复后功能回正常 | 截图 + 探测 |
| PH-24 | P0 | `hdc shell reboot` 重启手机；不手动启动应用，PC 探测 6900 | 应用不自动启动（预期，无开机自启权限）；端口不可达 | 探测记录 |
| PH-25 | P0 | 重启后手动启动应用，检查配置/日志/租约 | 端口、根目录、各服务开关按 ini 恢复；历史日志与租约保留（以配置为准） | 截图 |
| PH-26 | P1 | 系统设置内"强行停止"，再启动应用 | 停止后端口释放；启动后恢复监听；不出现僵尸通知 | `netstat` + 截图 |
| PH-27 | P1 | 分屏+后台同时进行 4 路 10MB 传输，持续 5 分钟 | 无 ANR/闪退；传输完成率 100% 或给出明确超时错误 | 客户端输出 + hilog |

### 4.4 PH-NET 网络场景（P0/P1）

| 编号 | 优先级 | 步骤 | 预期结果 | 取证 |
|---|---|---|---|---|
| PH-28 | P0 | T1：手机与 PC 同 AP；服务端口 6900；PC `get_client.py <DEV_IP> 6900 test_1k.txt out.bin` | 下载成功，MD5/SHA256 与源一致；设置页 LocalIP 为 wlan0 地址 | 客户端输出 + 哈希 |
| PH-29 | P1 | 若 PH-28 失败：确认 AP 是否开启客户端隔离，切换至未隔离 AP 复测 | 关闭隔离后通过；将"AP 隔离"记录为环境前置而非应用缺陷 | 复测记录 |
| PH-30 | P0 | Wi-Fi 断开 60 秒后重连（hdc 走 USB）；恢复后 PC 立即 GET，等待最多 3 分钟 | 进程不崩溃；恢复后端口自动可达，或日志明确提示需重启服务；重启服务后可达 | `netstat` + 探测时间线 |
| PH-31 | P1 | Wi-Fi 关闭 → 蜂窝（关闭 Wi-Fi 与热点）→ 再开 Wi-Fi；并在 Wi-Fi/蜂窝并存时把 LocalIP 分别设为 wlan0 与蜂窝地址（等价 R7） | 应用不崩溃；LocalIP/提示随网络变化；绑定 wlan0 时 PC 可达，绑定蜂窝地址时 PC 不可达，切回 wlan0 恢复；蜂窝期间 PC 不可达属预期 | 截图 + 探测 |
| PH-32 | P1 | 飞行模式开 30 秒再关 | 服务不崩溃、无异常退出；关闭后恢复可达（必要时重启服务） | 截图 + 探测 |
| PH-33 | P0 | T3：手机开热点，PC 连热点；确认 DHCP 服务为关闭；PC GET/PUT | 传输成功；哈希一致；手机热点网段地址正确显示 | 截图 + 哈希 |
| PH-34 | P0 | T2：路由器关 DHCP，手机/PC 静态 IP 同网段；手机开 DHCP 服务；PC `python dhcp_probe.py <DEV_IP> 6700` | 返回 OFFER/ACK，地址在池内，掩码/网关/DNS 与配置一致；租约页新增记录 | 探针输出 + 租约截图 |
| PH-35 | P1 | DHCP 页修改地址池/网关/DNS 并保存（服务运行中） | 保存后服务按新配置重启；再次探针获得新池地址；无须手动重启应用 | 探针输出 + 截图 |
| PH-36 | P2 | 组播：手机开组播地址 `239.255.0.1:6969`，PC 同 AP 加入组，RRQ 带 `multicast=...&blksize=512` 下载 `test_1k.txt` | OACK 含 `multicast`；PC 收到完整文件；大文件行为与基线 D7 修复结论一致 | OACK 文本 + 哈希 |

### 4.5 PH-F 功能回归（按基线抽样，P0/P1）

| 编号 | 优先级 | 对应用例 | 手机执行要点 | 预期结果 |
|---|---|---|---|---|
| PH-37 | P0 | TF-01/02/04 | `get_client.py` 下载 1B/1KB/10MB；`put_client.py` 上传 1KB/10MB | 哈希一致；日志方向与大小正确；10MB 无超时 |
| PH-38 | P1 | OP-01/02/04/07 | `curl` 或 `otftp2` 请求 `blksize=16384/8`、`tsize=0`、`Negociate=0` | OACK 行为与 2in1 基线一致（16384/8 接受，65464 钳制；Negociate=0 不协商） |
| PH-39 | P1 | RE-01/03/05 | 发 opcode=9、短包、向 6900 直发 DATA | 返回 `ERROR code=4`，进程不崩；超时重传次数符合 MaxRetransmit |
| PH-40 | P1 | SL-01 | PC 发 RFC3164 Syslog 到 `<DEV_IP>:5140` | 日志页实时显示，来源 IP 正确；开启落盘时文件写入 |
| PH-41 | P1 | ST-01 | 用 NTP v3 探针（Python/m2tool）向 `<DEV_IP>:1230` 发 mode=3 请求；若用 `w32tm`，先确认其默认端口为 123，不能直接指向 1230 | 收到应答；记录两机时钟差（测试前先对时） |
| PH-42 | P1 | DN-01~03 | `nslookup -port=5300 <name> <DEV_IP>` A/AAAA/NXDOMAIN | 与基线一致 |
| PH-43 | P1 | TC-01/02 | 手机"客户端"页对 PC 的 tftpd64（69/6901）GET/PUT | 成功，哈希一致；再次进入页面参数回填 |
| PH-44 | P1 | 目录页 | 浏览根目录、进入 `sub/`、刷新、新建/删除测试文件（若支持） | 列表正确；根目录跟随 BaseDirectory；操作后 TFTP 可访问 |
| PH-45 | P1 | 服务开关矩阵 | 依次关闭/开启 TFTP、Syslog、SNTP、DNS、DHCP，`netstat -anu` 查端口 | 关闭即释放、开启即监听；无残留端口；UI 状态一致 |
| PH-46 | P1 | 配置持久化 | 改端口（如 6900→7000）与根目录，杀进程重启 | 值保持；用新端口 GET 成功；ini 键名与原版一致（debug 包可读） |
| PH-47 | P2 | MD-02 / 组播大文件 | 开启 `MD5=1` 后 PUT；组播 10MB 下载（如环境允许） | `.md5` 文件内容正确；大文件组播按 D7 修复后的行为完成 |

### 4.6 PH-P 性能、功耗与资源（P1）

| 编号 | 优先级 | 步骤 | 预期结果 |
|---|---|---|---|
| PH-48 | P1 | 冷启动 3 次取中位数（`aa start` 到页面可交互） | ≤ 2.5s（首启权限弹窗轮次剔除） |
| PH-49 | P1 | 10MB GET、PUT 各 5 次，记录 MB/s | 全部哈希一致；记录绝对值作为本机基线（不与 2in1 跨硬件比较），后续回归不得出现数量级下降 |
| PH-50 | P1 | 4 路并发 10MB，采样 `VmRSS` 与 native heap | 全部完成；无持续单调增长；无 OOM |
| PH-51 | P2 | 空闲 1 小时耗电：记录起始/结束电量与应用耗电排行 | 记录实测值；若明显高于系统同类常驻应用，提交评审 |
| PH-52 | P2 | 连续传输 10 分钟，记录机身温度与是否降频导致超时 | 无因过热断流；温度主观记录 |
| PH-53 | P2 | 20 次 10MB 传输后检查存储与临时文件 | 无异常残留/空间泄漏 |

> 电池/温度可用系统设置查看；如需命令采样，先 `hdc shell hidumper -ls` 查 Battery/Thermal 服务 id，再 `hidumper -s <id> -a -i`，不要凭猜测使用固定 id。

### 4.7 PH-D 交付与异常（P0/P1）

| 编号 | 优先级 | 步骤 | 预期结果 |
|---|---|---|---|
| PH-54 | P0 | 通过 AGC 内测通道安装 versionCode 1000002（app_gallery 正式签名） | 安装成功、可启动；通知/网络权限行为与调试包一致 |
| PH-55 | P1 | 旧版本 1000001 → 新版本 1000002 覆盖升级（内测或市场） | 升级成功；配置、日志、租约、根目录文件保留 |
| PH-56 | P1 | 卸载后重装 | 数据清空（预期）；首次启动重新申请通知权限；无残留进程/通知 |
| PH-57 | P1 | 端口占用：先将 TFTP 端口改为一个已被占用的值（如开启 DHCP 后再把 TFTP 指到 6700），启动服务 | 中文提示"绑定失败/端口被占用"；不崩溃；改回后恢复 |
| PH-58 | P1 | 非法输入：客户端 host 为空/域名不存在/端口 0；目录填不存在路径 | 有明确错误提示；不崩溃；服务端不受影响 |
| PH-59 | P2 | 系统备份/恢复（EntryBackupAbility） | 恢复后配置可用；或明确记录系统不支持备份该应用数据（预期差异） |
| PH-60 | P2 | 市场合规复核 | 应用名/图标/权限用途说明与实际行为一致；截图与 SG-02 归档一致 |

---

## 5. 每轮构建的最小冒烟（8 步）

新包安装后先跑以下 8 步，任一失败即停止后续用例并提缺陷：

1. 安装成功 → 冷启动 ≤2.5s（PH-03）
2. 通知授权 → 长时任务成功（PH-04）
3. PC 下载 1KB 成功（PH-28）
4. 上传 1KB 成功（PH-37）
5. 退后台 5 分钟仍可下载（PH-19 缩短版）
6. 锁屏 5 分钟仍可下载（PH-20 缩短版）
7. 竖→横旋转后布局可用（PH-09）
8. 杀进程重启后配置保持（PH-46）

---

## 6. 执行顺序

1. **准入日**：G0 全部检查 → 签名路径确定 → PH-01~06 → 冒烟 8 步。
2. **形态与后台**：PH-07~18 → PH-19~27（含夜间 8h，可跨夜）。
3. **网络专项**：PH-28~36（T2 DHCP 需先改路由器；T3 热点需关 DHCP）。
4. **功能回归与性能**：PH-37~53。
5. **交付验证**：PH-54~60（依赖 AGC 内测/审核，可与前序并行准备）。

> 网络类用例建议全程使用 USB hdc；无线调试在断网/重启后会换端口，导致取证中断。

---

## 7. 判定与出口条件

### 7.1 缺陷分级

| 级别 | 定义 | 示例 |
|---|---|---|
| S1 阻断 | 无法安装/启动、核心服务不可用、数据损坏 | 手机 API 不满足导致无法安装；TFTP 全路径失败；传输哈希不一致 |
| S2 严重 | 主要场景失败且无规避 | 锁屏即断服；升级丢配置；端口绑定失败无提示 |
| S3 一般 | 有规避路径 | 横屏输入框被遮挡但可滚动；非法值提示不明确 |
| S4 轻微 | 体验/文案 | 文案换行、间距、次要页签拥挤 |

### 7.2 出口条件

- P0 用例 100% 通过（PH-01~04、07~08、19~25、28、30、33~34、37、54）；
- P1 通过率 ≥ 95%，且无未闭环 S1/S2；
- 8 小时挂机（含锁屏）通过，有完整探测时间线；
- 市场/内测包安装与升级通过；
- 已知偏差已登记（见 §8），并在报告中说明影响与规避；
- 测试环境按 §10 还原。

> 执行进度：当前未完成的用例、取证缺口与阻断出口的项汇总见 **附录 C**（截至 2026-11-14）。

---

## 8. 已知偏差与不测项（继承基线，不重复开缺陷）

| 编号 | 现象 | 性质/处理 |
|---|---|---|
| DEV-01 | 系统字体大小不跟随（固定 vp 字号） | 2in1 UI-08 已记录；手机 PH-12 复核显示最大档字号不变。**2026-11-14 已修复**：`AppScope/resources/base/profile/configuration.json` 配置 `fontSizeScale=followSystem`、`fontSizeMaxScale=3.2`，`app.json5` 引用 `$profile:configuration`；编译通过，待调试签名重装后按 PH-13 复验 |
| DEV-02 | 无法绑定 69/67 等特权端口 | 设计约束；默认 6900/6700，UI 可改 |
| DEV-03 | DHCP Ping/ARP 冲突检测不可用 | 平台无 raw socket；DHCP 页已有提示，直接分配 |
| DEV-04 | windowsize 协商被忽略 | 与原版 v4.74 一致，非缺陷 |
| DEV-05 | IPv6 外网可达性受环境限制 | 原 R8 IPv6 腿已豁免；手机如具备 IPv6 环境可补测，不作为出口条件 |
| DEV-06 | AP 客户端隔离 / 组播被 AP 过滤 | 环境限制；记录前置条件 |
| DEV-07 | 蜂窝网络下 PC 不可达 | NAT/无公网入口，属预期 |
| DEV-08 | app_gallery 正式 profile 不能 hdc 侧载 | 分发机制约束；功能测试用调试签名，交付验证走内测/市场 |
| DEV-09 | 手机系统可能限制长时任务 | 通知拒绝或系统策略导致时，服务降级但需有中文提示（PH-05/PH-22） |

---

## 9. 风险与应对

| 风险 | 影响 | 应对 |
|---|---|---|
| 手机系统低于 HarmonyOS 6.1/API 23 | 完全无法安装 | 先升级系统；若无法升级，评估下调 `compatibleSdkVersion` 的兼容成本（需回归） |
| 调试签名与手机 UDID 不匹配 | 安装报签名/设备不符 | 用 DevEco 自动签名连接该手机生成 profile；路径 B 失败即回退路径 A |
| 无线 hdc 掉线 | 网络用例取证中断 | 网络/重启类用例改 USB；`snapshot.ps1 -Device` 指定当前地址 |
| 路由器/AP 隔离、组播过滤 | 误判为应用缺陷 | 先按 PH-29 复测并记录 AP 型号与设置 |
| DHCP 与路由器 DHCP 冲突 | 地址错乱、租约异常 | 只在 T2 隔离网络启用；T1/T3 明确关闭 DHCP |
| 电池优化杀进程 | 8h 挂机失败 | 测试前设"允许后台活动"；同时保留受限档对照（PH-22/23） |
| 正式包签名被测试改动覆盖 | 交付物带调试签名 | 功能测试后恢复 `build-profile.json5` 的 AGC 材料再出包；出包后核验 profile 类型 |

---

## 10. 取证、记录与还原

### 10.1 取证规范

- 文件名：`PH-<编号>_<步骤>_<yyyyMMdd_HHmmss>.jpeg|txt|log`，统一存 `%TEMP%\tftpd32_phone\`。
- 关键界面变更（授权、启停、旋转、深色、字体、分屏、断网）各留 1 张截图。
- 网络类用例保留 PC 端命令原始输出（含时间戳），必要时连续探测形成时间线。
- 日志：`& $hdc shell hilog -x > <case>_hilog.txt`，过滤 `9800005|continuous|ERROR|Tftpd`。
- 截图脚本：`powershell -ExecutionPolicy Bypass -File tools\snapshot.ps1 -Name PH-07 -OutDir C:\Users\32572\AppData\Local\Temp\tftpd32_phone -Device <手机IP:端口>`。

### 10.2 记录模板

| 用例 | 设备/系统 | 步骤 | 实际结果 | 证据 | 结论 | 缺陷 |
|---|---|---|---|---|---|---|
| PH-03 | D1 / 6.1.x | 安装→冷启动 | 安装成功，启动 1.8s | 截图、计时 | 通过 | — |
| PH-09 | D1 | 竖→横 | 横屏设置页第二行说明被裁切，可滚动 | 横屏截图 | 失败（S3） | BUG-xx |
| … | | | | | | |

### 10.3 测试后还原清单

| 项 | 还原目标 | 涉及用例 |
|---|---|---|
| 系统设置 | 自动旋转、深色模式、字体/显示大小、省电模式恢复默认 | PH-09/12/13/14/23 |
| 通知权限 | 恢复"允许" | PH-05 |
| 应用启动管理 | 恢复"自动管理"或产品建议档位 | PH-22 |
| 服务配置 | 端口 6900、根目录默认、DHCP 关闭、组播留空 | PH-34~36/46 |
| 网络 | 关闭热点/飞行模式，恢复 Wi-Fi 与 PC 同 AP | PH-31~33 |
| 签名 | 恢复 AGC 正式签名配置，重新出正式包 | 路径 A |
| 测试数据 | 删除上传/下载测试文件，避免占满存储 | PH-37/53 |

---

## 11. 交付物

1. 本方案（测试依据）。
2. 执行记录表（按 §10.2 逐用例回填，含证据路径）。
3. 缺陷清单（编号、级别、复现步骤、日志/截图、状态）。
4. 手机测试报告（结论 + 已知偏差 + 与 2in1 基线的差异说明 + 出口判定）。

---

## 附录 A：第一轮（准入日）执行记录（2026-11-12）

测试环境：设备 ADA-AL00U，系统 `ADL-AL00U 7.0.0.111(SP10C00E111R1P2)`（OpenHarmony-7.0.0.111），ABI `arm64-v8a`；PC `10.188.157.198` ↔ 手机 `10.188.157.226`，TFTP 端口 6900；USB hdc（调试自动签名）。

| 用例 | 步骤 | 实际结果 | 证据 | 结论 |
|---|---|---|---|---|
| PH-01 | `hdc list targets -v`、`param get` 读型号/版本/ABI | 设备识别正常；型号 ADA-AL00U；系统 7.0.0.111；ABI arm64-v8a | hdc 命令输出 | 通过 |
| PH-02 | `hdc install` AGC `release/app_gallery` 正式包 | 被拒，错误码 `9568322 signature verification failed due to not trusted app source`，符合预期 | hdc 输出 | 通过（预期被拒，改走路径 A） |
| PH-03 | 自动签名安装 → 桌面图标 → 冷启动 | 安装成功，图标/名称正确，无白屏/闪退；冷启动至可交互 668ms | `XPerf appStartMonitor e2e:668` | 通过 |
| PH-04 | 通知授权 → 开启服务 → 查 hilog | 首版 `TASK_KEEPING` 三方申请被拒（9800005）；改 `DATA_TRANSFER` + `backgroundModes:["dataTransfer"]` 后重测，`OnContinuousTaskStart`、`continuousTaskId: 6`，通知常驻；无 9800005 | hilog 片段 | 通过（修复后重测） |
| PH-06 | `bm dump -n` 校验版本与 ABI | versionCode=1000002、versionName=1.0.0；ABI 含 arm64-v8a | bm dump 输出 | 通过 |
| 冒烟 3 | PC 下载 `smoke_1k.bin` | 1024B 下载成功，MD5 `9EE0A0E0C0BC0F1FF29D663D1FDF0743` | tftp_get_loop 输出 | 通过 |
| 冒烟 4 | PC 上传 1KB | 上传成功，手机侧 MD5 与源一致 | 哈希比对 | 通过 |
| 冒烟 5 | Home 退后台 300s 持续 GET | `SUMMARY ok=128 fail=0`，进程未冻结 | 探测时间线 + hilog | 通过 |
| 冒烟 6 | 锁屏 300s 持续 GET（先 Home 再 `power-shell suspend`） | `SUMMARY ok=124 fail=0`；截图确认锁屏 + 通知栏"正在运行数据传输任务"；进程 cgroup `1:freezer:/`（未冻结） | 探测输出、`s6.jpeg` | 通过（5 分钟窗口） |
| 冒烟 8 | `aa force-stop` → `aa start`，比对 `tftpd32.ini` | 重启后 ini 794B、MD5 `9e4e63d62929e97d66cb0edce23a68b2` 不变（mtime 更新为启动时重写）；服务自动启动（`continuousTaskId: 8`），GET 3/3 正确 | ini 哈希 + hilog + 探测 | 通过 |
| 冒烟 7 | 竖→横旋转 | 首测不旋转：`module.json5` 未声明 `orientation`，窗口被固定竖屏；补 `"orientation": "auto_rotation"` 重装后旋转成功（2776×1224），Tab 栏 9 个页签完整、文本换行无裁切、滚动正常、传输正常（2/2）；横屏右侧留白偏多（S4 观感建议） | `PH-09_landscape_20261112.jpeg`、`PH-09_landscape_scroll_20261112.jpeg` | 通过（修复后重测） |
| PH-05 | 系统设置关闭本应用「允许通知」→ 关服务开关 → 再开 | 服务正常启停（停止后探测 `fail=1`，重启后 `ok=2 fail=0`）；长时任务仍申请成功（`continuousTaskId: 11`、`StartBackgroundRunningExecuteCB ... notification 6`），无 1600004/9800005；通知栏仍显示系统下发的「Tftpd32 正在运行数据传输任务…」 | `PH-05_before.jpeg`、`PH-05_on.jpeg`、`PH-05_shade.jpeg` + hilog | 通过（行为差异：通知关闭后系统长时任务通知仍显示，未触发降级提示，后台能力不受影响） |

**遗留观察（息屏 Doze 冻结边界，影响 PH-19/PH-20）**

- `DATA_TRANSFER` 长时任务在退后台/锁屏 5 分钟内持续有效，TFTP 全程可达（冒烟 5/6 实测）。
- 息屏约 10 分钟后系统进入 Doze：`resource_schedule_service` 结束长时任务（`RecentContinuousTask onContinuousTaskStop`），应用被 `Freeze pid` 置入 `freezer:/Frozen`，`StandbyExecutor` 执行 `BreakSocketsPid` 销毁该 uid 的 socket，TFTP 立即不可达；`power-shell wakeup` 不足以解冻，需 `aa start` 切前台才恢复（恢复后无需重启服务）。
- 后续轮次（PH-19/20/22）需验证：系统"应用耗电管理 → 允许后台活动"白名单能否避免该冻结；如仍冻结，按 DEV-09（系统限制类）登记偏差，并在报告中给出规避指引。

**观感/建议项（S4）**

1. ~~竖屏下设置页长提示文字右边缘被裁切~~：已修复（DHCP 端口说明加 `layoutWeight(1)` 自动换行，第二轮 PH-08 复测确认）。
2. 横屏（2776×1224）下表单项仍沿用竖屏固定宽度（标签 80/160/180vp），右侧约半屏留白。建议横向时对内容限宽居中，或改为两列（标签列 + 控件列）。
3. 「客户端」页服务器地址与本地文件路径输入框内文字超出框宽被截断（单行输入可横向拖动查看），建议长文本加省略号/跑马灯提示。
4. （非缺陷备注）悬浮窗（约 1/3 屏宽）下端口说明与根目录路径右缘裁切、数字键盘覆盖窗体、缩至最小尺寸后关键控件不可操作：经用户实测确认均为鸿蒙小窗/软键盘的系统设计行为，不判为应用缺陷，无需修改。
5. （既知偏差）系统「字体大小」调至最大档后应用字号未跟随（与默认档逐行比对一致）。代码中字号为无单位数值（fp），理论上应随系统缩放；建议后续排查 ArkUI 的 maxFontScale/窗口字号继承，或对关键行显式使用 vp 固定排版。不判为阻断。

---

## 附录 B：第二轮（形态与后台）执行记录（2026-11-12，2026-11-14 补录 PH-21）

测试环境同附录 A（ADA-AL00U / OpenHarmony-7.0.0.111 / arm64-v8a，PC 10.188.157.198 ↔ 手机 10.188.157.226:6900）。

| 用例 | 步骤 | 实际结果 | 证据 | 结论 |
|---|---|---|---|---|
| PH-07 | 竖屏遍历 9 个页签（设置/TFTP/日志/Syslog/SNTP/DNS/DHCP/客户端/目录），页签栏横向滚动 | 9 页签均可达且可点击；页签栏 `BarMode.Scrollable` 滚动正常；DNS 页（`端口 5300`、hosts.txt 编辑框）与 DHCP 页（`端口 6700`、地址池）内容正确 | `PH-07_Set/TFTP/Log/Syslog/SNTP/DNS/DHCP/Client/Dir.jpeg`、`PH-07_tabbar_scrolled.jpeg` | 通过（页签栏首尾页签滚动时部分露出，属 Scrollable 设计，S4） |
| PH-08 | 约 360vp 宽检查设置页行布局（1224px / 3.2 ≈ 382vp） | 首测「端口 6700（在 DHCP 页签修改）」右缘断字；加 `layoutWeight(1)` 重装后自动换行，开关/输入框完整可点，说明文字不遮挡 | `PH-07_00.jpeg`（修复前）、`PH-08_after_fix.jpeg`（修复后） | 通过（修复后重测） |
| PH-09 | 竖屏→横屏→竖屏旋转，观察状态与服务 | 横屏（2776×1224）：标题/9 页签/「运行中」/端口 6900/各端口值全部保持，布局无裁切；转回竖屏同样保持；两段各探测 `ok=3 fail=0`，MD5 一致（首次一次探测收到重传 DATA，属正常重传） | `PH-09_landscape_rotate2.jpeg`、`PH-09_portrait_back.jpeg` | 通过 |
| PH-10 | 与「页汀」应用上下分屏，在本应用（上半区）操作页签与服务开关 | 分屏期间 PC GET 连续成功（`ok=2 fail=0`，MD5 一致）；页签栏横向滚动可露出「客户端/目录」；关闭主开关 →「已停止」且 6900 监听消失，再开启 →「运行中」且 6900 重新监听、GET 再次成功；「客户端」页在缩减高度下正常渲染，内容区可滚动（底部按钮需滚动露出） | `PH-10_pre.jpeg`、`PH-10_tabscroll.jpeg`、`PH-10_off.jpeg`、`PH-10_on.jpeg`、`PH-10_client.jpeg` | 通过 |

> 补充观察（2026-11-12 14:40 前后）：USB 重新连接后应用进程存活（pid 21588），`netstat` 仍显示 `udp6 [::]:6900` 监听、PC `ping` 正常，但 TFTP GET 全部超时；`aa start` 拉回前台后立即恢复（`ok=2 fail=0`）。与冒烟 6 一致，进一步佐证「进程被系统冻结 → socket 失效 → 回前台解冻恢复」，该现象并入 PH-19/20/21 的风险项跟踪。

| 用例 | 步骤 | 实际结果 | 证据 | 结论 |
|---|---|---|---|---|
| PH-11 | 智慧多窗：从侧边栏拖成悬浮窗，尝试缩到最小尺寸 | 拖至约 1/3 屏宽时可操作：标题/页签/端口值可见，服务保持运行（6900 监听、GET `ok=2 fail=0`）；小窗内说明文字按窗口宽度裁切、软键盘按系统尺寸覆盖窗体、缩至系统最小尺寸后控件不可操作——经用户实测确认均为鸿蒙小窗/软键盘的系统设计行为，非应用缺陷 | `PH-11_float.jpeg`、`PH-11_click.jpeg`、`PH-11_kb_close2.jpeg` | 通过（小窗行为属系统设计，记 S4 第 4 项备注） |
| PH-12 | 系统「字体大小」调至最大档，重启应用后检查设置页 | 应用字号未随系统字号变化，与默认档截图逐行比对一致：无截断、无按钮被挤出屏幕；属既知偏差（见 S4 第 5 项），非阻断 | `PH-12_maxfont.jpeg`（对照 `PH-08_after_fix.jpeg`） | 通过（记录偏差）。2026-11-14 已按 DEV-01 增加 `fontSizeScale=followSystem` 配置（编译通过、HAP 已含该 profile），真机复验待调试签名重装 |
| PH-13 | 系统「显示大小」调至较大档，重启应用后检查设置页 | 应用字号随「显示大小」放大，布局随之重排：DHCP 端口说明自动折成 3 行、根目录路径以省略号收尾、页签栏可滚动露出更多页签；无控件遮挡/溢出/截断，主开关与输入框完整可点 | `PH-13_displaysize.jpeg` | 通过 |
| PH-14 | 系统深色模式开启后重启应用 | 深色主题跟随系统：背景为黑、文字浅色、输入框深灰、主按钮蓝色，层级与对比清晰可读，无低对比不可见控件 | `PH-14_dark.jpeg` | 通过 |
| PH-15 | 设置页「端口」输入：空值、`abc`、`70000`；并在 Syslog 端口同为 5140 时观察重复端口 | 数字键盘自动弹出、输入框自动位于键盘上方可见；`abc` 被输入类型拦截无法录入；空值保存回退默认 6900（ini 校验通过）；**`70000` 未被拒绝，直接写入 ini（`TftpPort=70000`），而服务仍显示「运行中」并继续监听旧端口 6900，界面显示与实际监听不一致且无任何提示**；重复端口 5140 时 TFTP 绑 IPv6、Syslog 绑 IPv4，双栈各自成功、无中文告警（属地址族差异，不判缺陷） | `PH-15_focus.jpeg`、`PH-15_empty.jpeg`、`PH-15_val70000.jpeg`、`PH-15_after_save.jpeg`、`PH-15_conflict.jpeg`、`PH-15_final.jpeg` | 不通过（缺陷 PH-D1：端口越界未校验/无提示，UI 与实际监听状态不一致，P1） |
| PH-16 | 深色模式恢复默认；左右边缘侧滑返回；最近任务卡片上滑关闭；再重新启动应用 | 左右边缘侧滑（8→700、1216→500）均未退出应用，页面与「运行中」状态保持（pid 不变）；上滑关闭后进程消失（`pidof` 无输出）、`netstat` 无 6900 监听、通知栏收缩后无本应用常驻通知（仅剩 USB/资讯等系统通知）；`aa start` 重启后服务自动按 ini 恢复：端口 6900、状态「运行中」、`udp6 [::]:6900` 监听、PC GET `ok=2 fail=0`（MD5 一致）；另测返回键仅退到后台（进程存活、服务继续监听），属系统默认行为 | `PH-16_before_notif.jpeg`、`PH-16_notif.jpeg`、`PH-16_relaunch.jpeg`、`PH-16_backswipe.jpeg`、`PH-16_backswipe_right.jpeg`、`PH-16_backkey.jpeg` | 通过 |
| PH-18 | 9 个页签（设置/TFTP/日志/Syslog/SNTP/DNS/DHCP/客户端/目录）往返切换 3 轮（共 27 次切页），期间观察配置、日志与长时任务 | 页签栏横向滚动可露出「客户端/目录」，滚动本身不误切页签；3 轮后：进程 pid 不变（46285）、日志页内容前后逐行一致（无新增「TFTP 服务已启动」条目，说明页签重建未触发二次 autoStart）、`TftpPort=6900` 未变、`udp6 [::]:6900` 持续监听；通知栏仅 1 条本应用常驻通知（无重复长时任务通知） | `PH-18_log_before2.jpeg`、`PH-18_log_after2.jpeg`、`PH-18_tabswipe.jpeg`、`PH-18_client_after.jpeg`、`PH-18_notif_single.jpeg` | 通过（hilog 缓冲区已滚动，长时任务「仅申请一次」以进程内日志与单条常驻通知佐证） |
| PH-17 | 日志页快速上下滑动；触控目标尺寸；滚动后观察位置是否被抢走 | 触控目标实测（dumpLayout）：主开关 122×68px（36×20vp 系统默认）、端口输入框 405×135px、下拉选择 388×135px、页签 162~215×189px，除 Toggle 本身为系统标准尺寸外均 ≥ 约 39vp 高；闲置时滚到顶端后连续 dumpLayout 2 次（间隔 1.5s）首项 bounds 完全一致，无自发跳动；**但用户报告「滚到第一行后约 1s 弹回第二行」，复现确认根因：`subscribe` 回调对任意事件（含传输进度 `trf`）无条件执行 `logScroller.scrollEdge(Edge.Bottom)`，在任何滚动位置都会被强制拉到底部（实测在顶端执行一次 PC GET 后视图直接跳到列表末尾）** | `PH-17_top1.jpeg`、`PH-17_top2.jpeg`、`PH-17_flick_a.json`、`PH-17_flick_b.json`、`PH-17_after_transfer.jpeg` | 不通过（缺陷 PH-D2：日志页无条件自动滚到底部，用户无法停留在已滚动位置；P1） |
| PH-19 | 退后台（Home）保持服务 30 分钟，期间 PC 每约 2 分钟 GET 一次 smoke_1k.bin 并抓 `netstat` 队列 | **修复前（对照）**：16:19 退后台后 0~8 分钟完全正常（queue=0、ok=1 fail=0），**10 分钟起冻结**——pid 存活、6900 仍监听但 recv 队列单调增长（2368→18944）、PC GET 全部超时；根因为 DATA_TRANSFER 长时任务超 10 分钟无进度更新被系统取消（hilog `SYSTEM_CANCEL_DATA_TRANSFER_NOT_UPDATE`），进程随后被 Doze 冻结。**修复**（Index.ets）：注册 `continuousTaskCancel` 回调被取消即重申请、每 4 分钟 `updateBackgroundRunning` 刷新、`onPageShow` 回前台校验。**修复后（16:39 重测）**：32 分钟 16 次探测全程 `queue=0、ok=1 fail=0`、MD5 一致、pid 1278 不变；hilog 证实 17:39:28 任务 29 被系统取消（cancelReason 1）后 **1 秒内自动重申请成功为任务 30**（另有 `UpdateBackgroundRunning` 返回 -1 证实刷新对 dataTransfer 不生效，实际靠取消重申请兜底）；回前台 UI 正常显示「运行中」；退后台 65 分钟后复测 GET 仍成功 | `PH-19_probe.csv`（修复前：10 分钟拐点 queue=2368）、`PH-19_probe2.csv`（修复前换网对照）、`PH-19b_home.jpeg`、`PH-19b_probe.csv`（修复后 16 行全通过）、`PH-19b_front.jpeg` | 通过（修复后重测；残留风险：任务 30 亦有 10 分钟寿命上限，依赖取消回调持续重申请，长周期已由 65 分钟挂机佐证） |
| PH-20 | 服务运行中锁屏（`power-shell suspend` 灭屏）30 分钟；PC 每 5 分钟 GET 并抓 pid/队列/cgroup 冻结状态；解锁后核对 UI | 17:54 锁屏，7 轮探测（17:55~18:25）**全程 `ok=1`、MD5 一致、pid 1278 不变、queue=0，cgroup 始终在 freezer 根组未进入 frozen**——灭屏 Doze 下 10 分钟拐点同样平稳通过（优于修复前退后台场景），说明长时任务取消重申请机制在灭屏下依然生效；18:26 唤醒后无锁屏密码拦截直达桌面，拉起应用 UI 显示「运行中」、端口 6900，与锁屏期间后台状态一致；前台复测 GET `ok=1 fail=0`（MD5 一致） | `PH-20_probe.csv`（7 行全通过）、`PH-20_unlock.jpeg`（唤醒后桌面）、`PH-20_app.jpeg`（解锁后 UI） | 通过 |
| PH-23 | 开启省电模式（`power-shell setmode 601`），退后台重复 PH-19 的 30 分钟探测（16 轮×2 分钟），结束后恢复正常模式（600） | 18:33 开启省电模式，前台基准 GET `ok=2 fail=0`；退后台 16 轮探测 **15/16 成功、MD5 全部一致、pid 1278 不变、queue=0、未冻结**——省电模式下长时任务未被降级取消；18:44:15 单轮出现 `bad pkt op=3 blk=1`（DATA 包错乱，疑似 Wi-Fi 空口 UDP 丢包/乱序的客户端解析失败），**前后轮与后续 10 轮全部正常、进程与队列无异常**，且 18:33 前的 PH-20 测试同链路从未出现，倾向偶发网络抖动而非省电降级；19:05 回前台 UI「运行中」正常，复测 GET `ok=2 fail=0`；恢复正常模式成功 | `PH-23_home.jpeg`、`PH-23_probe.csv`（16 行，仅 1 轮失败）、`PH-23_front.jpeg` | 通过（省电模式未导致服务降级/中断；单次 UDP 包错乱定性为偶发网络抖动，不判缺陷） |
| PH-26 | 服务运行中（pid 1278）经 `aa force-stop` 强行停止；验证 pid 消失、端口释放、通知栏无残留；再启动应用验证服务恢复 | 19:07 强停前 pid 1278、`udp6 [::]:6900` 监听中；`aa force-stop` 成功后 **pid 消失、6900 端口完全释放**（netstat 无记录）；通知栏检查（截图）**无 Tftpd32 残留通知**（仅系统 USB 调试等通知）；19:09 重新启动应用，新 pid 35813，**6900 重新监听**、UI 自动显示「运行中」（服务自动启动逻辑生效），PC GET `ok=1 fail=0`、MD5 一致 | `PH-26_after.jpeg`（强停后桌面）、`PH-26_notif2.jpeg`（通知栏无残留）、`PH-26_restart.jpeg`（重启后 UI） | 通过 |
| PH-24 | 服务运行中执行 `hdc shell reboot`；重启后**不手动启动应用**，检查进程与端口 | 重启前已记录 ini 基准（TftpPort=6900、BaseDirectory、SecurityLevel=1 等）；重启后系统正常回到 `ADL-AL00U 7.0.0.111(SP10C00E111R1P2)`；`pidof` 无输出、`netstat` 无 6900 监听——**应用未自启动，端口不可达，符合预期**（无开机自启权限）；重启后设备停在锁屏（AOD 微亮），唤醒+上滑解锁进入桌面（期间弹出「USB 连接方式」系统对话框，选「仅充电」关闭）；重启后 Wi-Fi 短暂休眠导致 PC↔手机 ping 超时（ARP 双侧完整、手机 ping 网关正常），解锁后链路自然恢复，PC→手机 ping 0% 丢包 | `PH-24_screen.jpeg`（重启后锁屏 19:19）、`PH-24_home4.jpeg`（解锁后桌面） | 通过（不自动启动符合预期） |
| PH-25 | 重启后手动 `aa start` 启动应用，核对配置/文件/服务恢复 | 19:36 启动应用 pid 16465，**服务自动恢复「运行中」**、`udp6 [::]:6900` 监听；ini 逐键与重启前基准一致（TftpPort=6900、SecurityLevel=1、Timeout=3、MaxRetransmit=6、WinSize=1、Negociate=1、Services=1、BaseDirectory 指向 tftpboot）；tftpboot 根目录 4 个文件全部保留（smoke_1k.bin / smoke_512b.bin / tftp_opt_128k.bin / tftp_probe_65535.bin）；UI 回填正确（端口 6900、安全级别「标准」、超时 3、重传 6、选项协商开、WinSize 1、根目录路径）；PC GET smoke_1k.bin 成功（3 块 1024 字节，MD5 `9EE0A0E0...FDF0743` 与基准一致）；进程 cgroup 在 freezer 根组未冻结 | `PH-25_ui.jpeg`（重启后 UI）、`ph25_get.bin`（GET 产物） | 通过（配置/文件/服务全部按 ini 恢复） |
| PH-27 | 应用与「页汀」上下分屏（本应用在上半区、UI 显示「运行中」端口 6900），PC 以 4 线程并发 GET `test_10m.bin`（10MB，512 字节块），同时观察进程/端口/UI | **首轮（脚本严格模式）**：`ch0/ch2/ch3` 成功（10MB、MD5 `B743A96F...F580DB`、各 153.6~153.8s），`ch1` 在 57.5s 报 `bad op=3 blk=5455 exp=5456` 中止——该包为**重复 DATA 块**（上一 ACK 丢失后服务端重传），探测脚本按「块号不符即中止」判定，属客户端容错缺失、非应用缺陷；**按 RFC1350 修正脚本（重复块重发 ACK 后继续）后复测**：**4/4 全部成功**，各 173.8~174.8s、MD5 全部一致、累计容忍 6 次重复块；全程无 ANR/闪退，进程 pid 16465 不变、`udp6 [::]:6900` 持续监听，分屏 UI 保持「运行中」；聚合吞吐约 240 KB/s（单流约 60 KB/s，512 字节块逐块 ACK 的固有开销） | `PH-27_split.jpeg`（分屏态基准）、`PH-27_after.jpeg`（4/4 完成后分屏 UI） | 通过（4/4 完成、无 ANR；服务端重传行为正常，首轮失败为测试脚本未处理重复块） |
| PH-28 | T1：PC `get_client.py 192.168.10.130 6900 smoke_1k.bin`；并展开设置页「绑定网卡（LocalIP）」下拉核对地址枚举 | 下载成功：3 块、1024 字节、MD5 `9EE0A0E0...FDF0743` 与源一致；下拉正确枚举出 `全部接口` 与 `192.168.10.130`（与 `netstat` 的 wlan0 地址一致，无蜂窝地址混入）；默认值为「全部接口」，`uitest` 坐标注入点选该地址时仅关闭弹窗、不触发 `onSelect`（弹窗为独立子窗口，`dumpLayout` 不含其节点），实际绑定切换改由人工点选在 PH-31 复核 | `ph28_out.bin`、`PH-28_ipick.jpeg`（下拉枚举）、`PH-28_wlan0b.jpeg`（选中后仍显示全部接口）、`d1.json` | 通过（GET/MD5 与 LocalIP 枚举通过；绑定切换受自动化注入限制，转入 PH-31） |
| PH-37 | 1B/1KB PUT→GET 回环；10MB GET/PUT（PH-27 已覆盖） | `test_1b.bin`（内容 `A`）PUT 后 GET 一致（1 块 1024 字节，MD5 `7FC56270...ACBE29`）；`test_1k_up.bin` PUT→GET 回环 MD5 一致（3 块 1024 字节，无残块）；日志方向与大小正确（`Write request`/`Read request`、`sent 3 blks, 1024 bytes`）；10MB PUT/GET 于 PH-27 4 路并发覆盖，MD5 `B743A96F...F580DB` | `ph37_1b.bin`、`ph37_1k_back.bin`、日志页截图 | 通过 |
| PH-40 | PC 发 RFC3164 Syslog 到 `192.168.10.130:5140`；切 Syslog 页核对 | Syslog 页实时显示来源 `192.168.10.104` 与完整报文 `<14>Nov 12 21:10:00 phone-test app: PH-40 syslog probe from PC`（62 字节，与发送端一致）；日志页有「Syslog 服务已启动（端口 5140）」；落盘未开启（`szSyslogFile` 为空，按原版语义仅内存显示），落盘写入项因应用沙箱文件 shell 不可读，未取证 | `PH-40_syslog.jpeg`、`PH-40_log.jpeg` | 通过（实时显示与来源 IP 正确；落盘写入未取证，不判缺陷） |
| PH-41 | Python NTP v3 探针（mode=3）打 `192.168.10.130:1230` | 应答 48 字节：`li=0 vn=3 mode=4 stratum=10 refid=127.0.0.1`；`org_sec` 正确回显客户端发送时间戳；`xmt` 与 PC 本机时钟差约 3~4s（与基线 0.3s 同量级，属两机时钟差）；`refid` 为 127.0.0.1 系 `SntpdProc` 用 `gethostname/gethostbyname` 取本机地址在鸿蒙下返回回环，随上游实现 | `PH-41_sntp.txt`（探针输出）、`sntpd.c` 第 105 行（1900 纪元换算正确） | 通过（应答正常、stratum=10 与基线一致；refid 回环属上游实现） |
| PH-42 | 手工 DNS 探针打 `192.168.10.130:5300`：A、AAAA、不存在域名 | A 查询（`example.com`）：rcode=0、逐条 CNAME(`example.com`)+A `104.20.23.154`（ttl 300）；AAAA 查询（qtype=28）：rcode=0、`an=2`，回答仍为 CNAME+A——`CreateAnswerMsg` 只按解析到的地址生成记录，不按 qtype 过滤，IPv4 解析分支仅产出 A，属**原版同构行为**；不存在域名：rcode=4（与基线 DN-03 一致）；id 全部匹配 | `dns_probe2.py` 终端输出、`dns.c` `CreateAnswerMsg` 实现 | 通过（A 与 NXDOMAIN 与基线一致；AAAA 返回 A 系原版实现固有行为，不记缺陷） |
| PH-45 | 依次开关 DHCP/Syslog/SNTP/DNS，用 `netstat` 核对端口绑定与释放 | 开启 Syslog/SNTP/DNS 后 `0.0.0.0:5140`、`0.0.0.0:1230`、`0.0.0.0:5300` 同时监听；开启 DHCP 后 `0.0.0.0:6700` 出现，关闭后立即消失；DNS 关闭后 `5300` 释放、再开启重新监听；全程 TFTP `udp6 [::]:6900` 不受影响、UI 状态与 `netstat` 一致 | `PH-45_dhcp.jpeg`、`netstat` 输出（开关前后对比） | 通过（端口随开关正确创建/释放，无泄漏） |
| PH-30 | T1：`svc wifi disable` 断开 Wi-Fi 60 秒后 `svc wifi enable` 恢复（hdc 走 USB），恢复后 PC 立即 GET | 21:16:02 关闭 Wi-Fi；+10s 与 +60s 两次核对：**pid 16465 不变、`udp6 [::]:6900` 持续监听、wlan0 无 IP**，进程未崩溃亦未退出；21:17:10 开启 Wi-Fi，+3s 即重新获得 `192.168.10.130`；21:17:19 首次 GET 即成功（3 块 1024 字节、MD5 `9EE0A0E0...FDF0743` 一致，耗时约 0.7s），**无需重启服务** | `PH-30` 终端时间线输出（DISABLE_T0/+10s/+60s/ENABLE_T0/GET_T0）、`ph30_out.bin` | 通过（断网 60s 进程与端口保持，恢复后自动可达） |
| PH-32 | 控制中心开启飞行模式 30 秒后关闭，全程观察进程/端口，关闭后 GET | 21:18:15 开启飞行模式（Wi-Fi 与蜂窝同时断开、wlan0 无 IP）；**+30s 核对 pid 16465 不变、`udp6 [::]:6900` 持续监听、无崩溃**；21:19:37 关闭飞行模式，+6s 恢复 `192.168.10.130`；随后 GET 成功（3 块 1024 字节、MD5 与基准一致），服务全程未中断、无须重启 | `PH-32_airplane_on.jpeg`（飞行模式态截图）、`ph32_out.bin`、`netstat`/`ifconfig` 时间线输出 | 通过 |
| PH-46 | 设置页「端口」6900→6910 后点「保存配置」，`aa force-stop`+`aa start` 重启应用核对 ini 与监听，再还原 6900 | 21:47 保存后沙箱 ini 写入 `TftpPort=6910`，但运行中的进程仍监听 6900（端口仅在服务启动时读取）；21:48:00 重启后 pid 54908、`udp6 [::]:6910` 监听、UI 端口显示 6910；改回 6900 保存并重启后 pid 55286、`udp6 [::]:6900`、ini `TftpPort=6900` | `PH-46_6910_after_restart.jpeg`、ini 全文（终端记录） | 通过 |
| PH-48 | `aa force-stop` 静置 1s 后冷启动 3 次，从 `aa start` 计时到首帧可交互（每轮 `snapshot_display` 抓屏，按体积判别：桌面壁纸约 148KB、应用页 >200KB） | 3 轮耗时 1.05s / 0.92s / 0.95s，中位数 **0.95s**、最大 1.05s；3 轮均为冷启动，无权限弹窗干扰（通知授权已在 PH-04 完成） | `PH-48_coldstart.txt` | 通过（≤2.5s） |
| PH-49 | PC 当客户端对 `192.168.10.130:6900` 做 10MB GET×5、PUT×5，逐次记录耗时与吞吐，并比对 SHA256 | GET：98.91 / 108.64 / 102.18 / 117.71 / 114.72s（0.08~0.10 MB/s，均值 0.092），5/5 SHA256 = `21CDF320...DC0B1` 与源一致；PUT：100.67 / 119.27 / 98.31 / 94.06 / 140.05s（0.07~0.11 MB/s），5/5 服务端接收 10485760 字节、20480 块；回读 `ph49_up.bin` SHA256 一致 | `PH-49_result.txt` | 通过（哈希全部一致；吞吐受 512B 停等协议限制，作本机基线） |
| PH-50 | 4 路并发 10MB GET（`ph27_4way.py`），并发期间每 12s 采样 `VmRSS` 与 `hidumper --mem`（native heap / ark ts heap / Heap Alloc） | 4/4 成功，单路 10485760 字节、MD5 `B743A96F...80DB` 一致，总墙钟 165.6s（聚合约 0.24 MB/s），重复块 12/81924（0.015%）；VmRSS 97696kB → 峰值 101368kB（+3.8%），native heap Pss 20933→21162kB（+1.1%），ark ts heap Pss 10897→13262kB（Heap Alloc 50956→50999kB）；结束后 10s 采样无继续增长、无泄漏趋势 | `PH-50_result.txt`、`ph50_4way.out.txt` | 通过（4/4 完成，内存增幅 <5% 且稳定） |
| PH-52 | PC→手机连续 10 分钟 10MB GET 循环（7 轮），每 120s 采样 `hidumper --cpufreq` 与 `VmRSS`；温度受系统权限限制（`hidumper -s ThermalService` 对 shell 返回空、`/sys/class/thermal` 不可读）改以 cpufreq 降频与传输表现间接判定 | 7 轮全部成功（每轮 90~97s、20481 块 / 10485760 字节、MD5 `B743A96F...80DB` 全一致），总时长 674.6s、失败 0；全程 cpu0 保持 1430000~1530000kHz **无降频**、VmRSS 101800→103844→99636kB 波动（无单调增长）；机身温感正常、无断流无超时 | `PH-52_result.txt` | 通过（10 分钟连续传输无过热断流/降频） |
| PH-53 | 20 次 10MB GET/PUT 交替传输（07:45~08:13），前后对比沙箱 `du -sk`、`tftpboot` 文件数、`.tmp/.part` 残留与 `ph53_up.bin` 尺寸 | **20/20 全部成功、0 失败**（GET 79.2~84.5s、PUT 78.2~82.1s，MD5 `B743A96F...80DB` 全部一致）；文件数 19→19 不变，`.tmp/.part` 残留为空；`du` 20995→31091kB，增量 10096kB 恰等于 `ph53_up.bin` 从上次中断残留 153600B 完整覆盖为 10485760B（+10.09MB）——**空间增长完全由目标文件本身解释，无泄漏**。补充观察（首轮中断实测）：PUT 传输中断后服务端按目标名直接落盘留下半截文件（无 `.tmp` 中转），与原版 tftpd32 行为一致，不算缺陷 | `PH-53b_result.txt`（含 BEFORE/AFTER 基线）、`PH-53_storage_baseline.txt` | 通过（20 次大文件传输后无异常残留/空间泄漏；中断半截文件与原版一致） |
| PH-43 | 手机「客户端」页对 PC 参考 TFTP 服务端（`192.168.10.104:6901`，`pc_tftp_server.py`）执行 GET 与 PUT，核对客户端状态/MD5 与 PC 侧日志、落盘；另测参考服务端不可达时的错误路径 | 表单经 UI 输入后正确回填（服务器 192.168.10.104、端口 6901、远端 `test_1k.txt`、本地 `.../tftpboot/client_dl.bin`、块大小 0），并在下次进入时自动回填（`client_form.txt` 记忆生效）；**GET**：`状态 done 块 4 1902/1902 错误码 0`、MD5 `AD7FF548...C68A`，PC 日志 `RRQ from ('192.168.10.130', 41928) ... tsize\x000`，手机端 `md5sum` 与源文件一致；**PUT**（远端改 `ph43_up.bin`）：`状态 done 块 4 1902/1902`、MD5 同上，PC 侧落盘 1902 字节、MD5 一致；**错误路径**：参考服务端未监听时客户端以 `状态 error 错误码 -1` + 中文「传输超时（对端无响应）」结束，按钮恢复可点、无崩溃 | `PH-43_result.txt`、`pc_server.log`/`pc_server2.log`（RRQ 记录）、`lay_now.json`（状态文本） | 通过（GET/PUT 双向正确、MD5 三方一致；PUT 取证根目录受本机写入限制改用临时目录，属测试环境受限而非应用缺陷） |
| PH-44 | 目录页：核对根目录列表与大小、PC 上传新文件后点「刷新」、点新文件「MD5」按钮 | 页头显示 `目录：/data/storage/el2/base/haps/entry/files/tftpboot`（跟随 BaseDirectory），列表 19 项名称+字节数与沙箱目录一致；PC 侧 `put_client.py` 上传 `ph44_new.txt`（1902 字节/4 块）后列表**不会自动刷新**，点「刷新」后新文件出现在列表末尾；点其「MD5」按钮得到 `ph44_new.txt: ad7ff548d3fc47ef1fbcd82159d8c68a`，与源文件 MD5 一致；页内仅提供「刷新」与逐项「MD5」，无进入子目录/新建/删除入口（当前目录也无子目录），对应步骤的「若支持」条件不满足 | `PH-44_result.txt`、`lay_dir.json`/`lay_dir2.json`/`lay_dir3.json` | 通过（浏览/刷新/MD5 均正确；子目录与新建删除 UI 未提供，不判缺陷） |
| PH-57 | 端口被占用：设置页开启「DHCP 服务器」占住 6700，把 TFTP 端口改为 6700 并保存、重启应用，观察提示与监听 | DHCP 开启后 `udp 0.0.0.0:6700` 被占用；TFTP 端口改 6700 保存并 `force-stop`+`aa start` 重启后进程存活（pid 39075），但 `netstat` 中 **6900 无监听、6700 仍只属 DHCP**，设置页仍显示「服务 运行中」、TFTP 页仅「当前无传输」，**无任何占用/绑定失败中文提示**，hilog 亦无绑定失败记录；PC 向 6700 发 RRQ 超时（证明 TFTP 未在此端口服务）；恢复端口 6900、关闭 DHCP 并重启后 `udp6 [::]:6900` 恢复监听，PC GET `smoke_1k.bin` 成功（1024B/3 块，MD5 `9EE0A0E0...FDF0743`） | `PH-57_result.txt`、`lay_set3.json`、`lay_tf.json`、6700 RRQ 超时记录、`ph57_restore.bin` | **不通过 → 缺陷 PH-D4**（端口占用时状态误报「运行中」且无中文提示，用户无法识别服务未生效；不崩溃、配置恢复正常） |
| PH-58 | 客户端页非法输入：服务器地址清空、填入不存在域名 `no.such.host`、端口填 `0`，各发起 GET | 空 host：约 5s 返回 `状态 error 错误码 -1` +「主机名解析失败（网络不可达或 DNS 不可用）」；`no.such.host`：约 30s 后 `错误码 -1` +「传输超时（对端无响应）」；端口 `0`：约 30s 后同样「传输超时」；三次均不崩溃、按钮恢复可点；期间 PC GET 手机 6900 `smoke_1k.bin` 正常（MD5 `9EE0A0E0...FDF0743`），服务端未受影响；目录页无输入控件，用例「目录填不存在路径」不适用 | `PH-58_result.txt`、`lay_a.json`/`lay_b.json`/`lay_b2.json`/`lay_c.json`/`lay_c3.json`、`ph58_chk.bin` | 通过（有明确错误提示、不崩溃、服务端不受影响）。观察项已于 2026-11-14 修复：客户端页新增发起前校验（空服务器地址、端口越界、远端/本地为空、块大小越界均即时提示且不发请求），状态行中文化；`no.such.host` 等格式合法的不存在域名仍受系统 DNS 超时约束，记为平台限制（见附录 D.4） |
| PH-38 | 选项协商：RRQ 带 `blksize=16384/8/65464`、`tsize=0`、1024+tsize，及无选项；再把设置页「选项协商」关闭保存重启，重复选项请求 | 协商开启：`OACK blksize=16384`、`OACK blksize=8`、`blksize=65464` **钳制为 16384**、`OACK blksize=1024 tsize=1024`、`OACK tsize=1024`，无选项时 `DATA block=1 len=512`（传统模式）；协商关闭（保存配置 + `force-stop`/`aa start` 重启）后四种请求全部退化为 `DATA block=1 len=512`、**无 OACK**，与 2in1 基线「Negociate=0 不协商」一致；恢复开关并重启后 OACK 行为回归基线 | `PH-38_39_result.txt`、`ph38b.py` 输出 | 通过（OACK 选项与钳制规则、Negociate=0 行为均与基线一致） |
| PH-39 | 异常报文：发 opcode=9、1 字节短包、截断 RRQ、向 6900 直发 DATA/ACK；另做「RRQ 后永不应答」的重传探针 | 五类异常全部返回 `ERROR code=4 "Illegal TFTP operation"`（与基线 RE-01/03/05 一致），进程存活、随后正常 GET 仍成功；重传探针：DATA 重发 6 次（0.0/1.0/3.0/6.1/9.0/12.1s，间隔≈3s 等于「超时(秒) 3」），第 7 次不再发送并回 `ERROR code=0`，约 19.1s 后静默，**重传次数=6 与设置「重传次数 6」一致** | `PH-38_39_result.txt`、`ph38_39.py` 输出 | 通过（异常码与重传次数符合基线；观察项：重传耗尽后置 ERROR code=0，与原版收尾行为一致） |
| PH-51 | 熄屏空闲 1 小时（09:11~10:11，服务保持运行、期间不做任何传输），每 5 分钟采样 pid / 6900 监听 / `/proc/<pid>/stat` CPU ticks / 电量；结束时读「设置 > 电池 > 耗电量排行 > Tftpd32 > 应用耗电详情」 | 13 次采样 **pid 40462 全程不变、`udp6 [::]:6900` 全程监听**；CPU `utime+stime` 335→2106 ticks（+1771 ticks ≈ 17.7s / 60.4min ≈ **0.49% 单核占用**）；电量 capacity 恒为 100（USB 供电、系统显示「已充满电」，百分比不可观测），`remainingEnergy` 2952→2945、温度 22.0→20.0℃、电流≈0；应用耗电详情：前台 32m56s（94.46→96.49 mAh）、后台 5m34s→6m06s（14.62→**14.89 mAh，+32s / +0.27mAh**）；耗电排行 Tftpd32 79%→67%、主屏幕 18%、设置 12%，今日亮屏 33→39 分钟；锁屏状态常驻通知可见「正在运行数据传输任务，删除通知后任务将停止」；解锁后复测 GET `smoke_1k.bin` 成功（3 块 1024 字节，MD5 `9EE0A0E0...FDF0743`） | `PH-51_idle_log.txt`（13 行采样）、`PH-51_notification_lock.jpeg`（锁屏常驻通知）、`PH-51_end_app_energy.jpeg`、`lay_bat4.json`（排行）、`lay_batapp2.json`（耗电详情）、`ph51_after.bin` | 通过（1h 熄屏空闲进程/端口/长时任务通知全部正常，CPU <0.5%、后台耗电约 0.27mAh/h；百分比耗电因 USB 供电充满不可观测，改用 mAh 计数与 CPU 采样判定） |
| PH-22 | 「设置 > 应用启动管理」分别在自动/手动（允许后台）下观察 1 小时；本机先核对入口是否存在 | 设置内搜索「应用启动管理」「耗电管理」均返回「没有匹配的结果」——**HarmonyOS 7.0.0.111 已无「应用启动管理」入口**；「应用和元服务 > 应用」列表（滚动至末尾「指南针」）**不含本应用**（hdc 侧载调试包不参与该列表管理），故无逐应用启动管理开关可切换；改以系统默认（自动）管理下的 1 小时熄屏空闲观察（与 PH-51 同一时段）：长时任务未被取消、进程未被冻结、6900 持续监听、通知常驻，耗电详情后台 6m06s / 14.89 mAh、排行占比 67%；另：应用内设置页中文引导写「设置 > 电池 > 应用耗电管理」，实际系统路径为「设置 > 电池 > 耗电量排行 > Tftpd32」打开的「应用耗电详情」 | `lay_srch.json`/`lay_srch3.json`（搜索无结果）、`lay_apl3.json`（应用列表末尾无本应用）、`lay_bat4.json`、`lay_batapp2.json`、`PH-51_idle_log.txt` | 环境受限（本 ROM 无「应用启动管理」入口、侧载包不在应用列表，自动/手动两档无法切换）；以系统自动管理下 1h 观察替代，服务稳定无中断。观察项：应用内引导文案的路径名称与系统实际路径不一致，建议改为「电池 > 耗电量排行 > Tftpd32」 |
| PH-33 | T3 热点场景：手机开「个人热点」（DHCP 服务保持关闭），PC 连该热点后 GET `smoke_1k.bin`、PUT 8KB 文件，核对哈希与热点网段地址 | 热点开启（设备名临时改为 ASCII `TTftpd32AP`、密码 `12345678`）后手机提供 `192.168.49.0/24`（网关 `192.168.49.1`），PC 自动获取 `192.168.49.6`；`ping 192.168.49.1` 3/3 通（3~11ms）；热点页显示「已连接设备 1 台」；GET `smoke_1k.bin` → 3 块 1024B、MD5 `9EE0A0E0...FDF0743` 与源一致；PUT `ph33_up.bin` 8192B/16 块，手机侧 `md5sum` = `d877d9b0803a0f5eca76d434ef4711ec` 与 PC 源一致；手机 `wlan0` 仍为 `192.168.10.130`（热点为 WLAN 共享模式，uplink 未断） | `ph33_get.bin`、`ph33_up.bin`、`PH-33_hotspot_1device.jpeg`、`lay_hot4.json`（热点已开）、`lay_hot5.json`（1 台） | 通过（热点网段地址正确、GET/PUT 均成功且哈希一致；观察项：测试后 `rm` 沙箱内 `ph33_up.bin` 被拒（shell 无沙箱写权限），该文件待重装/发版前清理） |
| PH-36 | 组播：设置页填组播地址 `239.255.0.1` / 端口 `6969` 并「保存配置」（ini 写入 `MulticastAddress=239.255.0.1`、`MulticastPort=6969`）；PC 侧 `mcast_get.py` 发 RRQ `smoke_1k.bin` 带 `multicast=239.255.0.1:6969`，ACK#0 后加入组播组接收 DATA；随后同法下载 `test_10m.bin` | RRQ（32 字节）后服务器以单播 TID `192.168.10.130:38664` 回 `OACK multicast=239.255.0.1,6969`（29 字节，opcode=6）；ACK#0 后 **DATA 全部由 `192.168.10.130:6969` 组播发出**：blk1/blk2 各 512B、blk3 payload 0（末块）→ 共 1024B，PC 侧 MD5 `9EE0A0E0C0BC0F1FF29D663D1FDF0743` 与手机 `smoke_1k.bin` 一致；10MB：20481 块、10,485,760B **完整**（末块 payload 0），MD5 `B743A96F573CBAFD8571518E38F580DB` 与手机 `test_10m.bin` 一致；吞吐约 **3.3 KB/s（6.7 块/s，≈150ms/块）**，10MB 全程约 50 分钟，PC 探针记录 6 次超时重传，手机日志页成片 `Ack block N ignored (received twice)` 及两次 `长时任务被系统取消（原因码 1），正在重新申请`；迟滞机理：Wi‑Fi 组播帧按 DTIM 缓冲投递（单帧往返百毫秒级）+ `WinSize=1` 使 `ExtraWinSize=WinSize/包长=0`（无窗口流水，一块一个 ACK 往返）+ 丢块需等 `TftpSelect` 超时 `(Timeout 3+3)/4=1s` 才重传，叠加期间屏幕反复自动熄灭加剧 Wi‑Fi 省电 | `PH-36_mcast_small.txt`、`mcast_get.py` 输出、`ph36_get.bin`、`ph36_10m.bin` | 通过（OACK 含 multicast、小文件与 10MB 均完整且哈希一致）；观察项：组播吞吐受 DTIM 缓冲与无窗口流水限制（10MB≈50min），属协议/无线特性非应用缺陷，如需提速可增大 WinSize |
| PH-47 | 开启 `MD5=1` 后 PUT 校验 `.md5`；组播 10MB 大文件见 PH-36 | 先备份 ini，将 `MD5=0` 改为 `MD5=1` 后用 `hdc file send` 覆写沙箱 `tftpd32.ini`，`aa force-stop` + `aa start` 重启后 `udp6 [::]:6900` 恢复监听（`cat` 确认 `MD5=1` 生效）；PUT `ph47_md5.txt`（26B/1 块）成功，手机 tftpboot 生成 `ph47_md5.txt`（26B）与 **`ph47_md5.txt.md5`（32B）**，其内容 `af717e2015fbd77b6d3db5f9a430e2e7` 与 PC 源文件 MD5 一致；组播 10MB 由 PH-36 完成（20481 块、哈希一致），组播路径按代码设计不生成 `.md5` | `ph47_md5.txt`、`ph36_10m.bin`、手机 `md5sum` 输出 | 通过（`.md5` 文件生成且内容正确；组播大文件按 D7 修复后行为完整收尾）；遗留：ini 中 `MD5=1` 与组播地址需在发版前恢复为 `MD5=0` / 组播留空 |
| PH-31 | 网络切换：①AP `CMCC-bfm2-5G`（192.168.10.x）→公司 Wi-Fi（10.188.157.x）；②`svc wifi disable` 断 Wi-Fi 后 `svc wifi enable` 恢复；③设置页「绑定网卡」分别绑定有效地址与失效地址后 GET | ①切换后进程未退出，仍监听 `udp6 [::]:6900`，PC（10.188.157.198）首测 GET 即成功（1024B/3 块，MD5 `9EE0A0E0...FDF0743`），**无需重启服务**；②断 Wi-Fi 期间 wlan0 无地址、pid 不变、6900 持续监听，恢复后立即取得 `10.188.157.226` 并可 GET；③`LocalIP=10.188.157.226` 绑定时 GET 正常；`LocalIP=192.168.10.130`（已失效）时仍以 `udp6 [::]:6900` 监听且 GET 正常（回落全接口，属容错行为） | `hdc` 终端时间线（`ifconfig`/`netstat`/`pidof`）与 PC GET 回读产物 | 通过（跨网段与断网恢复均无需重启服务；本机 `ifconfig` 仅 `wlan0`/`lo`，无蜂窝数据接口，蜂窝分支记环境受限） |
| PH-35 | DHCP 页把地址池 `10.188.157.230` 改为 `10.188.157.240`，点「保存并应用」，用两个新 MAC 的 DISCOVER/REQUEST 探针核对实际分配 | 保存后界面提示「已保存，DHCP 服务已按新配置重启」，ini 落盘 `IP_Pool=10.188.157.240`，`0.0.0.0:6700` 持续监听；服务掩码由 57 增至 61（DHCP 已启用）；改前探针（MAC 末尾 `aa/bb`）获 `.230`/`.231`，改后同法探针获 `.240`/`.241`——**运行中改配置可热重启生效，无需重启应用** | DHCP 探针终端输出（改前 `.230/.231`、改后 `.240/.241`）、界面「保存并应用」截图、ini 片段 | 通过（保存即提示并重启 DHCP，实际分配随新地址池变化） |
| PH-56 | `hdc uninstall` 卸载应用，核对沙箱目录与进程/端口；再重装同一 `entry-default-signed.hap`，首启核对数据、权限弹窗与传输 | 卸载成功，`/data/app/el2/100/base/io.github.hdsagb.tftpd32harmongyOS` 目录消失（无残留），pid 与 6900 监听均不存在；重装后首启自动重建 `tftpboot`（空）与默认 ini（`BaseDirectory=.../files/tftpboot`、`TftpPort=6900`、`Services=1`）并自动进入「运行中」；**无权限弹窗**（仅有 system_grant 权限，已由安装时授予）；WRQ 上传 + RRQ 回读 1024B，MD5 `9EE0A0E0...FDF0743` 一致 | `hdc uninstall`/`install` 输出、`pidof`/`netstat` 核对、WRQ+RRQ 回读 MD5 | 通过（卸载无残留、重装重建默认配置、权限与传输正常；重装后 shell 无法写沙箱属系统 uid 限制，改用 TFTP 上传验证） |
| PH-59 | 系统备份/恢复能力核对：检查 `module.json5` 备份扩展声明、`backup_config.json`、`EntryBackupAbility` 实现与安装包内扩展注册 | 已声明 `EntryBackupAbility`（`type: backup`、`metadata ohos.extension.backup → $profile:backup_config`）；`backup_config.json` 为 `allowToBackupRestore: true`；`EntryBackupAbility.onBackup/onRestore` 为仅打 hilog 的 no-op，依赖系统默认沙箱备份；`bm dump` 确认包内 `extensionTypeName: "backup"`、`process: ...:backup`、`resource: $profile:backup_config` | `module.json5`、`backup_config.json`、`EntryBackupAbility.ets`、`bm dump -n` 输出 | 通过（配置级）；本 ROM 系统「备份与恢复」入口不可用，端到端备份未触发，记环境受限 |
| PH-60 | 市场合规复核：应用名/图标/版本、权限清单与用途说明、隐私声明、第三方 SDK | 应用名 `Tftpd32`（`app_name` 与 `EntryAbility_label` 一致，桌面图标与名称已在 PH-03 核对）；图标为分层图标 `layered_image`（background/foreground/startIcon/startIcon_1024 齐备）；版本 `versionName 1.0.0` / `versionCode 1000002`；权限仅 `INTERNET`、`GET_NETWORK_INFO`、`KEEP_BACKGROUND_RUNNING` 三项，均为 system_grant 且 `PRIVACY.md` 第三节逐项给出用途；`PRIVACY.md` 存在并声明完全本地运行、不收集/不上传数据、无广告与统计 SDK；`oh-package.json5` 运行时依赖仅本地 `libtftpd32.so`（`@ohos/hypium`/`@ohos/hamock` 为 devDependencies） | `module.json5`、`app.json5`、`string.json`、`layered_image.json`、`PRIVACY.md`、`oh-package.json5` | 通过（名称/图标/版本/权限/隐私声明均齐全，无第三方 SDK） |
| PH-D1 | 缺陷回归（端口校验）：设置页端口输入 `70000` 后「保存配置」；再在停止态保持该非法值直接打开「服务」开关 | 两处均被拦截：日志页记录「端口“70000”无效：应为 1~65535 的整数，配置未保存」，ini 仍为 `TftpPort=6900`；停止态下开关保持「已停止」，`netstat` 无 6900 监听（旧版会静默回退 6900 并启动） | `lay3.json`~`lay7.json`（按钮与日志节点）、`hdc shell cat tftpd32.ini` 输出 | 通过 |
| PH-D2 | 缺陷回归（日志吸底）：日志页上翻查看历史后由 PC 发 RRQ 产生新日志，观察视口；再滚回底部重复一次 | 上翻状态下新日志追加后**顶部可见行保持不变**（`TFTP 服务启动失败…` 仍在 y481），未被强制拉回底部；滚到底部（`end of transfer 467` 贴视口下沿）后再发 GET，视口随新日志整体上移、最新 `transfer 468` 停在底部——自动吸底正确恢复 | `layJ.json`（上翻态）、`layK.json`（新日志后）、`layM.json`/`layN.json`（回底后） | 通过 |
| PH-D3 | 缺陷回归（线程回收）：连续「开→关→开」3 轮，逐轮核对 pid、6900 监听，并在运行态用 PC GET 验证；关闭期间核对端口释放 | pid 全程不变（无 SIGSEGV/闪退），每轮关闭后 6900 立即释放、开启后 `udp6 [::]:6900` 恢复监听；多次重启后 PC GET `ph56_smoke.bin` 均成功（1024B/3 块、哈希一致），重装后新进程（pid 36640）同样稳定 | 逐轮 `pidof`/`netstat` 输出、`d2_get.bin`/`d2_get2.bin` | 通过 |
| PH-D4 | 缺陷回归（启动状态判定）：开启 Syslog（5140）并把 TFTP 端口设为 69（应用无权限绑定的特权端口）后启动 | 新版本正确显示「服务 已停止」，日志记录「TFTP 服务启动失败…」与「无权限绑定该端口（1024 以下为系统特权端口）」；`netstat` 无 69 监听（旧版会仅凭 Syslog 启动成功就误报「运行中」）。回归中另发现并修复残留：启动失败时已启动成功的 Syslog 原会继续监听 5140，而界面与 ini 均显示未启用；现改为失败即回滚 `stopAll()`，同场景下 5140 不再残留，恢复正常配置后可立即重新启动 | `layA.json`/`layB.json`（状态与错误日志）、`layD.json`（回滚后无监听）、`netstat` 输出 | 通过 |
| PH-D5 | 缺陷回归（横屏布局）：手机旋转到横屏（窗口 2776×1224px，密度 3.25）后查看设置页 | 设置内容整体限宽约 720vp（≈2350px）：左边界 213px、右侧最长文本到 2563px，左右留白对称（各约 213px），即 `constraintSize({ maxWidth: 720 })` + `Row` 居中生效；旋转过程中服务未中断（pid 36640 不变、`udp6 [::]:6900` 持续监听） | 横屏 `dumpLayout` 节点边界（`服务` 213~483、端口输入框 483~888、IPv6 说明右端 2563）、`netstat`/`pidof` | 通过 |
| PH-21 | 夜间挂机 8 小时（00:38:29→08:38:40，全程 `power-shell suspend` 灭屏；PC 侧 `SetThreadExecutionState` 关闭睡眠），每 30 分钟一次 TFTP GET `ph21_probe.bin` + Syslog 探针，逐轮采集 `pidof`/`netstat`/`ifconfig wlan0`/电量 | **17/17 轮全部成功**（n=0~16，间隔 30 分钟）：每轮 GET 3 块 1024 字节、MD5 `B2EA9F7F...A8855B` 一致；**pid 15887 全程不变、`udp 0.0.0.0:5140` + `udp6 [::]:6900` 全程监听**，无崩溃/无被杀/无冻结（长时任务取消回调重申请机制在 8h 尺度稳定）；设备 Syslog 页统计收到 `PH21 CHK 0`~`16` 共 17 条（来源 192.168.10.104，无缺失）；结束时屏幕 `Current State: SLEEP`（累计灭屏 >8h）、BatteryService capacity 100% / chargingStatus 3 / pluggedType 1（USB 充电）——电量下降按用户决定记「充电中不适用」。前序轮次（23:43~00:13）因 PC/手机换网（10.188.157.x → CMCC-bfm2-5G）致 n=1 失败，已改为每轮动态取 wlan0 地址；正式轮次全程稳定于 `192.168.10.130` | `ph21_20261114_003829.txt`/`.jsonl`（17 轮逐条记录）、`ph21_end_syslog.jpeg`（设备 Syslog 页 17 条终态） | 通过（8h 全程应答、端口稳定、无冻结；电量下降项因 USB 充电不适用） |
| PH-34 | T1（CMCC-bfm2-5G）同网段 PC `192.168.10.104` / 手机 `192.168.10.130`；应用启用 DHCP 服务（6700 端口），配置地址池 `192.168.10.240`×10、掩码 `255.255.255.0`、网关 `192.168.10.1`、DNS `192.168.10.1`、租期 2880 分钟；PC 用 `dhcp_probe.py` 以两个不同 MAC 依次发 DISCOVER→REQUEST | 两台虚拟客户端均收到 **OFFER + ACK**：A=`192.168.10.240`、B=`192.168.10.241`，均在池内且递增分配；选项与配置逐项一致：opt1=`255.255.255.0`、opt3(router)=`192.168.10.1`、opt6(dns)=`192.168.10.1`、opt51=172800s（=2880 分钟）、opt54(srv_id)=`192.168.10.130`、opt66(TFTP)=`192.168.10.130`；设备 DHCP 页「租约表（2）」新增两条 `已确认` 记录（`.240`/`02:11:22:33:44:88`、`.241`/`02:11:22:33:44:99`，时间 11-14 09:50）。**环境前置偏差**：T2 要求的「路由器关 DHCP + 手机/PC 静态 IP 同网段」在现有 AP 上不可满足，改在 T1 执行；因应用 DHCP 监听非标准端口 6700（路由器 DHCP 与系统客户端均走 67），实测未对局域网真实设备产生分配。测试后已还原（`Services=9`、池/掩码/网关/DNS 清空、6700 释放、6900/5140 恢复；GET `ph21_probe.bin` 自检 MD5 `B2EA9F7F...A8855B` 一致） | `dhcp_probe.py` 全量输出（DISCOVER/OFFER/REQUEST/ACK 选项）、`ph34_dhcp.json`/`ph34_lease.json`（DHCP 配置页与租约表节点）、`ph34_lease.jpeg`（租约页截图） | 通过（判据全部满足；T2 前置以 T1 + 非标准端口 6700 替代，见 C.4） |
| PH-40 | PC 发 3 条 RFC3164 Syslog 到 `192.168.10.130:5140`（`<94>`/`<92>`/`<94>`，间隔 0.4s）；先在设置页把「Syslog 日志文件」填为 `…/files/tftpboot/device_syslog.txt` 并「保存配置」→ 重启应用使 syslog 服务按新配置建文件 → 发报文后分别用 Syslog 页、目录页与 TFTP 取回三种途径核对 | **落盘写入通过**：重启后文件即被创建（GET 得 0 字节、`MD5=D41D8CD9…427E`，证明 `SyslogCreateLogFile` 在服务启动时执行）；发 3 条后文件增至 **286 字节**、`MD5=A0AF230EB9954F8CE733252BB19D6697`，逐行追加且格式与源码 `%24.24s;%s; %s\r\n` 一致：`Sat Nov 14 11:21:38 2026;192.168.10.104; <94>Nov 14 11:21:39 PH40-TEST 1 …`，来源 IP 取自报文源地址（`192.168.10.104`）而非报文正文；Syslog 页实时显示同 3 条（逆序，来源 IP 独立成行）与文件内容逐字一致；目录页显示 `device_syslog.txt 286`，与 TFTP 取回字节数吻合。配置经 UI 保存后 ini 键 `SaveSyslogFile` 已登记（BUG-011 修复在手机形态下复验有效）。测试后还原（`SaveSyslogFile=` 清空、`Services=9`、6900/5140 正常，GET `ph21_probe.bin` MD5 `B2EA9F7F…A8855B` 一致）；测试文件保留在 tftpboot 作为取证物 | `docs/test-evidence/PH-40/ph40_sys.jpeg`（Syslog 页 3 条）、`ph40_sys2.json`（页面节点）、`ph40_syslog.txt`（落盘原文 286B）、`ph40_dir.json`（目录页 286 字节） | 通过（实时显示 + 来源 IP + 落盘写入三项判据齐备） |

---

## 附录 C：未完成测试清单（截至 2026-11-14）

统计口径：用例全集 PH-01~PH-60 共 60 条；附录 A/B 已记录 55 条通过（含 PH-D1~D5 缺陷回归通过、PH-21 跨夜挂机、PH-34 DHCP 租约、PH-40 Syslog 落盘），本附录只收录**尚未形成有效结论**的项。

### C.1 汇总

| 分类 | 数量 | 用例 | 对出口的影响 |
|---|---|---|---|
| 未执行（需排期） | 2 | PH-54、PH-55 | PH-54 为 P0，阻断 §7.2 出口条件 |
| 部分完成 / 取证缺口 | 2 | PH-22、PH-59 | 不阻断，但报告中须标注覆盖不足 |
| 条件豁免（无失败前置） | 1 | PH-29 | 无影响，保留为换网失败排查项 |
| 环境受限分支（不影响结论） | 4 | PH-34 T2 前置、PH-31 蜂窝分支、PH-36 吞吐观察、DEV-05 IPv6 | 无影响，按 §8 偏差登记 |

> P0 缺口的执行状态：PH-21（8h 挂机）与 PH-34（DHCP 租约）已于 2026-11-14 通过（见附录 B）；仅 PH-54（市场包安装）仍**未执行**，因此当前尚不能判定通过出口条件。

### C.2 未执行用例

| 用例 | 优先级 | 未完成原因 / 前置条件 | 建议动作 |
|---|---|---|---|
| PH-54 | P0 | AGC 内测通道安装正式包需版本 1000002 已上传 AGC 并开启内测、测试账号在名单内；当前仅有 hdc 调试签名（正式 profile 侧载被拒已在 PH-02 取证） | AGC 上传并开启内测 → 手机从内测链接安装 → 核对启动、权限、通知行为与调试包一致；可与 PH-55 同批次执行 |
| PH-55 | P1 | 旧版 1000001 → 1000002 覆盖升级前置同 PH-54（需 1000001 历史包在内测/市场可得） | 先装 1000001 并产生配置/日志/租约/根目录文件，再经内测升级到 1000002，逐项核对保留情况 |

### C.3 部分完成 / 取证缺口

| 用例 | 优先级 | 已完成部分 | 缺口与建议动作 |
|---|---|---|---|
| PH-22 | P1 | 系统默认管理下 1 小时熄屏观察通过（长时任务未取消、进程未冻结、端口持续监听） | ROM 环境限制：本 ROM 已无「应用启动管理」入口（搜索无结果），侧载包也不在「应用和元服务」列表，自动/手动两档无法切换；按 DEV-09 登记环境差异后关闭（用户决定不再测该项）。遗留建议：应用内引导文案路径（「电池 > 应用耗电管理」）与系统实际路径（「电池 > 耗电量排行 > Tftpd32」）不一致，建议改文案 |
| PH-59 | P2 | 配置级核对通过（backup 扩展声明、`allowToBackupRestore: true`、no-op `onBackup/onRestore`、`bm dump` 中扩展注册） | 本 ROM「备份与恢复」入口不可用，端到端备份未触发；在具备系统备份的机型执行一次「备份 → 清数据 → 恢复」核对配置与根目录，否则按「系统不支持」登记差异关闭 |

### C.4 条件豁免与环境受限分支

| 项 | 说明 | 处理 |
|---|---|---|
| PH-29（P1） | 仅在 PH-28 失败时执行（排查 AP 客户端隔离）；PH-28 已在同 AP 下通过，前置不成立 | 豁免执行；保留为后续换网失败的排查项，不判缺陷 |
| PH-34 T2 前置 | 现有 AP 不允许关闭 DHCP，手机与 PC 地址均由路由器分配，T2「隔离 LAN + 静态 IP」无法搭建 | 改在 T1 同网段执行：应用 DHCP 监听非标准端口 6700，路由器 DHCP 与系统客户端均走 67，实测未命中、未对真实设备分配地址；探针判据全部满足（见附录 B PH-34 行）。用户已决定该项关闭，不再补测 T2 版本 |
| PH-31 蜂窝分支 | 本机 `ifconfig` 仅 `wlan0`/`lo`，无蜂窝数据接口；Wi‑Fi 关闭/恢复、绑定有效与失效 LocalIP 均已通过 | 蜂窝下 PC 不可达按 DEV-07 属预期；如需覆盖，换插卡机型补测 |
| PH-36 吞吐观察 | 组播 10MB 约 50 分钟（DTIM 缓冲 + `WinSize=1` 无窗口流水），手机日志出现重复 ACK 与长时任务重申请 | 属协议/无线特性非缺陷；如需提速，增大 WinSize 后复测吞吐并记录 |
| DEV-05 IPv6 | 无 IPv6 出口环境，R8 IPv6 腿继续豁免 | 不作为出口条件；环境具备时补测外网可达性 |

### C.5 S4 观察项（2026-11-14 全部处理完毕）

| 来源 | 观察项 | 状态与处理 |
|---|---|---|
| 附录 A S4-2 | 横屏右侧约半屏留白 | **已关闭**（PH-D5：720vp 限宽居中），从观察项移除 |
| 附录 A S4-3 | 「客户端」页服务器地址、本地文件路径输入框文字超框宽被截断（单行可横向拖动） | **已修复（真机复验通过）**：服务器/远端文件/本地文件三个长文本输入框启用 `TextOverflow.Ellipsis`；真机输入超长地址后文字不再溢出输入框，聚焦/滚动时以省略号提示被截断内容（证据 `docs/test-evidence/PH-58/s43_fix_ellipsis.jpeg`） |
| 附录 A S4-5 / DEV-01 | 系统「字体大小」最大档字号不跟随（与默认档逐行一致） | **已修复（待真机复验）**：`AppScope` 新增 `resources/base/profile/configuration.json`（`fontSizeScale=followSystem`、`fontSizeMaxScale=3.2`），`app.json5` 以 `configuration: "$profile:configuration"` 引用；需调试签名重装后按 PH-13 复验 |
| 附录 B PH-58 | 客户端非法输入无输入期校验；不存在域名仅报「传输超时」且约 30s 才反馈 | **已修复**：客户端页新增发起前校验（服务器地址/端口 1~65535/远端文件/本地文件/块大小 0 或 8~65464），非法输入即时红字提示且不发起请求；状态行由英文码改为中文（解析/连接中、传输中…）。原生「主机名解析失败（网络不可达或 DNS 不可用）」文案保留；不存在的域名仍受系统 DNS 解析超时约束（ArkTS 无同步解析超时接口），记平台限制 |

### C.6 发版前还原 / 清理清单（非用例）

| 项 | 现状 | 目标 |
|---|---|---|
| 签名配置 | `signature\build-profile.agc-backup.json5` 仍在，说明 `build-profile.json5` 仍为调试签名 | 恢复 AGC 正式签名材料后再出正式包，并核验 profile 类型 |
| 设备 ini | `MD5=1`、组播 `239.255.0.1:6969`（PH-47/PH-36 遗留） | 恢复 `MD5=0`、组播留空、LocalIP 回「全部接口」、端口 6900 |
| 沙箱测试文件 | `ph33_up.bin`、`ph44_new.txt`、`ph47_md5.txt`、`ph53_up.bin` 及 10MB 测试文件（shell 无写权限无法直接删） | 重装或经「目录页」清理，避免占用存储 |
| 手机热点 | SSID 临时改为 ASCII `TTftpd32AP` | 恢复原 SSID；PC 侧删除该热点 Wi‑Fi 配置 |
| PC 参考服务端 | 6901 端口的 `pc_tftp_server.py` 及其落盘目录（含测试用 `refroot` 临时目录） | 停止进程并删除目录 |
| 客户端页表单 | 服务器地址/端口/远端/本地为测试值，已被表单记忆回填 | 清空或恢复默认 |
| 临时取证 | `%TEMP%\tftpd32_phone`、`lay*.json`、探测 csv 等 | 报告归档后清理 |

---

## 附录 D 缺陷与待优化项全量汇总（2026-11-14）

> 汇总范围：`Tftpd32-功能测试方案.md`（模拟器 / 2in1 基线）、`Tftpd32-真机开发与测试方案.md`（2in1 真机基线）、本方案附录 A/B（手机真机）三处记录。
> 说明：`BUG-001` 为缺陷模板示例行（字段内容为 `...`），不是真实缺陷，不计入；`D1~D4`、`D9`（风险表）为风险项而非缺陷。

### D.1 已修复缺陷：功能与协议（模拟器 / 2in1）

| 编号 | 来源用例 | 现象 | 严重级 | 修复要点 |
|---|---|---|---|---|
| BUG-002 | UI-02 | 传输行文件名消失 | P1 | stat 刷新按 `id` 保留 trf 帧的文件名 |
| BUG-003 | UI-02 | 时长恒为 0s | P1 | ForEach key 加入 `file/peer/bytes/ret/done/rc`，值变触发重渲染 |
| BUG-004 | UI-02 | `ret` 恒 0 | P1 | 改用 `dwTotalTimeOut`（原版 FD_TIMEOUT 列） |
| BUG-005 | UI-02 | 结束后行立即消失、无 100%/STPD/ERR | P1 | 新增 `C_TFTP_TRF_END` 转发，按 `Keep transfer Gui` 秒数清除 |
| BUG-006 | DN-01 | A 记录 RDATA 长度为 0 | P0 | `dns.c` 固定 `htons(4)` 并只拷贝 4 字节 |
| BUG-007 | MD-01 | MD5 与 PC 不一致 | P1 | `UINT4` 由 `unsigned long` 改 `uint32_t` |
| BUG-008 | DN-03 | AAAA 查询无应答 | P2 | `DNS_AAAA` 定义改为 28 并兼容 2 |
| BUG-009 | DN-04 | 畸形查询收到上一次应答 | P1 | 每轮 `anslen=0`，仅生成应答才发送 |
| BUG-010 | UI-04 | WinSize 保存 2 重启回填 1 | P1 | 整型键改用 `getInt` |
| BUG-011 | SL-02 / UI-04 | `SaveSyslogFile` 保存后未持久化 | P2 | `settings_pal.c` 的 `tKeys[]` 补登记 |
| BUG-012 | CF-01 | 迁移原版 ini 后根目录拼出 ENOENT | P1 | 读取值裁掉尾部 CR/LF/空白 |
| BUG-013 | UI-05 / DN-01 | DNS 页保存 hosts.txt 崩溃 | P1 | `writeText` 加 try/catch、自动建目录 |
| BUG-014 | UI-01 / UI-05 | 2in1 标题与图标名显示 `label` | P1 | 资源名统一为 `Tftpd32` |

### D.2 已修复缺陷：2in1 真机基线

| 编号 | 现象 | 严重级 | 修复要点 |
|---|---|---|---|
| D5 | 双栈时只监听 IPv4，无 `udp6` | P1 | 地址解析优先取 `AF_INET6`，双栈绑定 |
| D6 | 纯 IPv6 仍接受 IPv4（v4-mapped） | P1 | 设 `IPV6_V6ONLY=1` |
| D7 | 组播大文件随机块号 `MAX RETRIES` 中止 | P1 | 关闭 `IP_MULTICAST_LOOP`，修正 `Rc==0` 重传计时 |
| D8 | 组播中止时 `ERROR code=0` 发往组地址 | P1 | `nak()` 组播模式改单播回源 |
| D9 | 绑定 <1024 端口日志为英文 `Permission denied` | P2 | 走 `PalSvcErrText` 中文文案 |
| D10 | 组播地址键名错误（`MulticastAddrQAZwsx…`） | P2 | UI 键名改为 `MulticastAddress` |
| D11 | Syslog/SNTP/DNS 页端口数值空白 | P3 | 绑定对应 Setting 键 |
| D12 | 根目录硬编码，不随 `BaseDirectory` 变化 | P2 | 页面根目录跟随 ini |

### D.3 已修复缺陷：手机真机

| 编号 | 现象 | 严重级 | 修复要点 |
|---|---|---|---|
| PH-D1 | 端口越界无校验、无提示 | P1 | 设置页端口输入校验与中文提示 |
| PH-D2 | 日志页无条件吸底，无法上翻历史 | P1 | 仅停留在底部时跟随，上翻保持位置 |
| PH-D3 | 并发传输后服务线程不回收 | P1 | 传输结束线程回收与唤醒管道非阻塞修复 |
| PH-D4 | 端口占用时状态误报「运行中」且无提示 | P1 | 启动失败回退「已停止」并给出绑定失败原因 |
| PH-D5 | 横屏右侧约半屏留白、内容截断 | P1 | 720vp 限宽居中布局 |
| PH-19 缺陷 | 退后台约 10 分钟后进程被冻结：pid 与端口仍在，但 recv 队列堆积、对端全部超时 | P1 | `DATA_TRANSFER` 长时任务超 10 分钟无进度被系统取消，随后进程 Doze 冻结；注册 `continuousTaskCancel` 取消即重申请 + 每 4 分钟刷新 + `onPageShow` 校验 |

### D.4 2026-11-14 本轮修复（观察项与优化）

| 编号 | 现象 | 级别 | 处理 | 验证 |
|---|---|---|---|---|
| PH-22 观察项 | 引导文案「设置 > 电池 > 应用耗电管理」与 HarmonyOS 7.0 实际路径不符 | S4 | `Index.ets` 改为「设置 > 电池 > 耗电量排行」→ 应用耗电详情允许后台活动；功能测试方案同步 | 真机复验通过：设置页引导文案已显示「设置 > 电池 > 耗电量排行」 |
| S4-3 | 客户端页服务器/远端/本地路径输入框超宽被截断 | S4 | 三个 `TextInput` 启用 `TextOverflow.Ellipsis` | 真机复验通过：超长地址不再溢出输入框，以省略号提示截断；证据 `docs/test-evidence/PH-58/s43_fix_ellipsis.jpeg` |
| S4-5 / DEV-01 | 系统字体大小最大档字号不跟随 | S4 | 新增 `configuration.json`（`fontSizeScale=followSystem`、`fontSizeMaxScale=3.2`）并在 `app.json5` 引用 | 编译通过且 HAP 已含该 profile；真机复验待做（需改系统字体档位后对比截图） |
| PH-58 | 客户端非法输入无输入期校验；不存在域名仅报「传输超时」约 30s | S3/S4 | 新增发起前校验（服务器/端口 1~65535/远端/本地/块大小 0 或 8~65464），非法输入即时红字提示且不发请求；状态行中文（解析/连接中、传输中…） | 真机复验通过：远端留空、端口填 `6900abc` 均即时红字提示且未发请求（证据 `docs/test-evidence/PH-58/`） |

### D.5 平台偏差与环境受限（非缺陷，不修复）

| 编号 | 说明 | 处理 |
|---|---|---|
| DEV-02 | 无法绑定 69/67 等特权端口 | 设计约束，默认 6900/6700，可配置 |
| DEV-03 | DHCP Ping/ARP 冲突检测不可用 | 平台无 raw socket，DHCP 页提示后直接分配 |
| DEV-04 | windowsize 协商被忽略 | 与原版 v4.74 一致，非缺陷 |
| DEV-05 | IPv6 外网可达性无环境 | 豁免，不作为出口条件 |
| DEV-06 | AP 客户端隔离 / 组播被过滤 | 环境限制，记录前置条件 |
| DEV-07 | 蜂窝下 PC 不可达 | NAT/无公网入口，属预期 |
| DEV-08 | app_gallery 正式 profile 不能 hdc 侧载 | 分发机制约束，交付验证走内测/市场 |
| DEV-09 | 手机系统限制长时任务 | 通知拒绝/系统策略下服务降级，已有中文提示 |
| PH-36 | 组播 10MB 约 50 分钟、重复 ACK | 协议/无线特性非缺陷；如需提速增大 WinSize 后复测 |

### D.6 未执行项（非缺陷，阻断/待排期）

| 编号 | 说明 | 状态 |
|---|---|---|
| PH-54（P0） | 市场包安装与启动核对（需 AGC 上传 1000002 并开启内测） | 未执行，阻断 §7.2 出口条件 |
| PH-55（P1） | 旧版 1000001 → 1000002 覆盖升级 | 未执行，前置同 PH-54 |
