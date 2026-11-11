# Tftpd32 for HarmonyOS：剩余 9 项人工测试操作指南

> 生成日期：2026-11-03。覆盖《Tftpd32-未测项目报告》当前剩余 9 项：**NF-07、UI-06、UI-07、UI-08、UI-09、SG-01、SG-02、R6**（A 类 8 项）与 **R8 IPv6 腿**（B 类 1 项）。
> 每项给出：判据（来源）、前置、步骤、预期、取证、失败处理、结果回填位置。命令均已在当前环境验证可用的写法；执行顺序建议见 §10。

---

## 0. 通用准备与约定

### 0.1 当前环境基线（2026-11-03）

| 项 | 值 |
|---|---|
| 真机 | HUAWEI MateBook Pro（HAD-W32），2in1，HarmonyOS 6.1.0.117 |
| 真机 IP | 192.168.10.128（wlan0，无线调试端口 38691） |
| PC | 192.168.10.104（Win10，同网段） |
| hdc 全路径 | `D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe` |
| 应用 | com.example.myapplication，TFTP 端口 6900（双栈绑定，监听仅出现在 `/proc/net/udp6`，`grep 1AF4` 可见） |
| 应用配置 | 安全级别=标准(1)，BaseDirectory=files，Services=61（TFTP+Syslog+SNTP+DNS） |
| 设备沙箱根目录 | `/data/storage/el2/base/haps/entry/files/tftpboot`（含 test_1b.bin / test_1k.txt / test_10m.bin / sub/） |
| PC 基准 | `D:\trae\tfpd32\tftpd64.exe`（v4.74，TftpPort=6901） |
| PC 工具目录 | `C:\Users\32572\AppData\Local\Temp\`（get_client.py / put_client.py / probe_errors.py / 测试文件） |

python 客户端用法：`python get_client.py <ip> <port> <remote> <local>`，输出 DONE/TIMEOUT 与 MD5。

### 0.2 hdc 连接（每条命令前先重连）

无线 hdc 容易掉线，**每条/每组命令前先执行**：

```powershell
$hdc="D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe"
& $hdc kill; & $hdc start; Start-Sleep 2
& $hdc tconn 192.168.10.128:38691
& $hdc list targets    # 确认输出 192.168.10.128:38691
```

### 0.3 UI 操作拍照确认规则（必守）

- **凡要在真机 UI 上输入内容、改开关、点保存/启动之前，先拍一张确认当前界面与预期一致**：

```powershell
& $hdc shell snapshot_display -f /data/local/tmp/pre_check.jpeg
& $hdc file recv /data/local/tmp/pre_check.jpeg C:\Users\32572\AppData\Local\Temp\pre_check.jpeg
```

- 关键状态变更后（服务启停、授权弹窗选择、权限变更、深色/DPI 变更、窗口形态切换）各拍一张留证，文件名按用例编号命名（如 `ui06_01_允许授权.jpeg`）。
- 布局类取证可补充控件树快照：

```powershell
& $hdc shell uitest dumpLayout -p /data/local/tmp/layout.json
& $hdc file recv /data/local/tmp/layout.json C:\Users\32572\AppData\Local\Temp\ui07_layout.json
```

### 0.4 日志取证速查

```powershell
# 快照式：读取 hilog 环形缓冲后退出（推荐，取证用）
& $hdc shell hilog -x > C:\Users\32572\AppData\Local\Temp\case_hilog.txt
# 本地过滤关键行
Select-String -Path C:\Users\32572\AppData\Local\Temp\case_hilog.txt -Pattern "9800005|continuous|Background|ERROR"

# 进程与端口
& $hdc shell "ps -ef | grep com.example.myapplication | grep -v grep"
& $hdc shell "netstat -anu | grep 6900"          # 或 grep 1AF4 /proc/net/udp6（双栈监听在 udp6）

# 应用 ini（配置/租约取证）
& $hdc shell cat /data/app/el2/100/base/com.example.myapplication/haps/entry/files/tftpd32.ini
```

### 0.5 测试后还原清单（逐项勾选）

| 项 | 还原目标 | 涉及用例 |
|---|---|---|
| 安全级别 | ini=1（标准）；若改过请经 UI 改回"标准"并保存 | 通用 |
| 深色模式 | 关闭 | UI-08 |
| 显示大小/字体大小 | 恢复默认档 | UI-08 |
| 应用权限 | 通知=允许、网络信息=允许（系统设置内恢复即可，无需重装） | UI-09 |
| 服务开关 | 按需恢复运行（默认开启，端口 6900） | 全部 |
| tftpd32.ini | 与当前 UI 配置一致；若测试中改过端口/级别，保存前先确认目标值 | R6/SG-01 |
| hdc 连接 | 重新执行 0.2 重连序列并确认可达 | NF-07 |
| PC 侧 tftpd64 | 若启动过，测试后关闭 | SG-02 |

### 0.6 结果回填位置

每项执行完成后回填三处：

1. **真机方案 §12 记录模板**：新增一行（编号/环境/步骤摘要/实测结果/与原版差异/结论/证据路径）；
2. **真机方案 §3 状态表**：对应组状态行更新（G1/G5/G6/G7）；
3. **未测项目报告**：对应小节（A-01 NF-07、A-02 UI-06、A-03 UI-07、A-04 UI-08、A-05 UI-09、A-06 SG-01、A-07 SG-02、A-15 R6、B-02 R8）现状改写，并联动头部一句话结论、类别表、汇总表、合计数。

---

## 1. UI-06 通知授权 / 长时任务 / 常驻通知（P1，G6）

**判据（真机 §9、§1 D3、未测 A-02）**：① 成功申请 TASK_KEEPING（不出现 9800005）；② 常驻通知按系统策略显示；③ 拒绝授权有中文引导；④ 停服后通知消失。本节做①②④，③并入 §5 UI-09 统一执行（避免重复操作）。

**前置**：hdc 可连；确认通知授权现状（设置 > 应用 > 应用管理 > Tftpd32 > 通知，2in1 路径可能为"设置 > 通知与状态栏 > 应用通知管理"）。

**步骤**：

1. 基线：`aa force-stop` 后冷启动（`hdc shell aa start -a EntryAbility -b com.example.myapplication`）。若从未授权过通知，预期首次启动弹出通知授权弹窗 → **先 snapshot 确认弹窗内容** → 点"允许"，截图。
   - 若已允许：跳过弹窗，直接进入第 2 步。
2. 进入设置页，**snapshot 确认**当前配置（端口 6900、安全级别"标准"）→ 开启服务开关 → 观察日志页出现 `TFTP 服务已启动（端口 6900）`。
3. 抓 hilog 快照，过滤 `9800005`、`has continuous task`、`ERR_HAS_CONTINUOUS_TASK`：
   - 出现 `has continuous task` 且无 `9800005` → TASK_KEEPING 申请成功（判据①）。
4. 下拉打开通知中心 → 找到 Tftpd32 常驻通知 → snapshot（判据②；通知显示与否均如实记录，"按系统策略显示"允许系统折叠/归组）。
5. 退后台（回桌面）约 1 分钟 → PC 侧探测确认后台可达：
   ```powershell
   python C:\Users\32572\AppData\Local\Temp\get_client.py 192.168.10.128 6900 test_1b.bin C:\Users\32572\AppData\Local\Temp\ui06_out.bin
   ```
6. 回应用 → 关闭服务开关 → 打开通知中心 → snapshot：通知消失（判据④）。

**预期**：① hilog 无 9800005、有 continuous task 记录；② 通知中心出现常驻通知；④ 停服后通知消失；后台探测 DONE 且 MD5 `2408AD11F9EB830DA749E2A36A29EFF7`。

**取证**：弹窗/通知中心/停服后通知区 3 张截图、hilog 快照文件、GET 输出。

**失败处理**：出现 9800005 或通知缺失 → 按 §1 D3 方向排查（KEEP_BACKGROUND_RUNNING 说明文案、电池设置放行），缺陷记入 D 表；通知被系统归组导致找不到 → 在通知中心搜索应用名，仍无则记录系统策略现象。

**回填**：§12 新增 UI-06 行；§3 G6 状态行；未测 A-02。

---

## 2. UI-07 2in1 窗口形态与键盘（P1，G6）

**判据（真机 §9、§1 D6、未测 A-03）**：布局不截断；页签可横向滚动；键盘操作完整（Tab 遍历 + Enter 激活）。

**前置**：本真机即 2in1，接键鼠（内置键盘或外接均可）；服务停止或运行均可。

**步骤**：

1. **全屏形态**：正常启动应用（窗口最大化）→ 逐页签点开（设置/TFTP/日志/DHCP/DNS/Syslog/SNTP/客户端/目录，按实际页签）→ 每页 snapshot。检查：无控件截断、文本完整。
2. **自由窗口形态**：点标题栏"还原"按钮（或从最大化拖出）使应用进入自由悬浮窗口 → 拖拽窗口边缘缩小至约 1/3 屏（此时 8 个页签必然放不下）→ 观察页签区：
   - 出现横向滚动（滚动条/箭头/可拖动）→ 判据"页签可横向滚动"满足；
   - 缩到最小可用宽度时主体内容仍可读 → snapshot 2 张（中等/最小）。
3. **分屏形态**：将应用窗口拖贴屏幕左/右边缘进入分屏（或经多任务控制中心并排另一应用）→ snapshot 检查布局。
4. **键盘操作**：鼠标点一下应用窗口聚焦后：
   - 连按 Tab：焦点在页签与页面控件间顺序移动，焦点框可见 → snapshot 焦点状态；
   - Shift+Tab 反向遍历一次；
   - 焦点落在"服务开关"上按 Enter：服务启停生效（对照日志页）；
   - 焦点落在页签上按 Enter：页签切换生效。
5. `uitest dumpLayout` 留存控件树（检查控件 bounds 不超出窗口宽高）。

**预期**：三形态布局均不截断；窄窗口页签可横向滚动；Tab/Enter 操作完整生效。

**取证**：各形态截图（全屏 1~2 张、自由窗口 2 张、分屏 1 张、键盘焦点 2 张）、layout.json。**自由窗口形态截图同时保留给 SG-02 第 7 张使用。**

**失败处理**：某形态下截断/错位 → 截图 + 记录窗口尺寸，按 D6"调整断点与页签滚动策略"提缺陷；Enter 无法激活开关 → 记录焦点落在哪一级（容器 vs 控件），提缺陷。

**回填**：§12 新增 UI-07 行；§3 G6 状态行；未测 A-03。

---

## 3. UI-08 深色 / DPI / 旋转（P2，G6）

**判据（真机 §9、未测 A-04）**：三种条件下文字与控件可读、无重叠（允许与原版布局差异）。

**前置**：UI-06/07 已完成；记录当前显示设置为默认档以便还原。

**步骤**：

1. **深色模式**：系统"设置 > 显示与亮度 > 深色模式"开启 → 回应用逐页截图（至少设置页 + 日志页）→ 还原关闭。切换前 snapshot 系统设置页确认。
2. **显示大小（DPI）**：系统"设置 > 显示与亮度 > 显示大小"调大 1~2 档（2in1 也检查"缩放"设置）→ 回应用逐页截图 → 观察文字/控件无重叠、页签仍可滚动 → 还原默认。再调"字体大小"最大档重复一次。
3. **旋转**：2in1 设备通常锁定横屏、无重力旋转。若系统无旋转选项，直接记录"设备不支持旋转，用例不适用（设备限制）"；若存在旋转入口（显示设置/快捷开关）则旋转后截图验证。

**预期**：深色下对比度足够、无黑字黑底；大 DPI/字体下无重叠截断（允许出现滚动）；旋转项按设备能力如实记录。

**取证**：深色 2 张、DPI 2 张、（旋转 1 张或不适用说明）；系统设置变更前后各 1 张。

**失败处理**：出现不可读/重叠 → 记录具体页面与档位截图，按 D6 提缺陷；**测后必须还原显示设置（见 0.5）**，否则影响 SG-02 材料截图。

**回填**：§12 新增 UI-08 行；§3 G6 状态行；未测 A-04。

---

## 4. SG-02 市场材料（P2，G7）

**判据（真机 §10、未测 A-07）**：7 张截图齐全并标注页签名；简介/权限用途说明（INTERNET / GET_NETWORK_INFO / KEEP_BACKGROUND_RUNNING）/ GPLv2 与源码地址与实际行为一致。文字材料已就绪，本节主拍截图。

**前置**：0.5 还原项全部完成（浅色、默认 DPI、权限齐全）；hdc 可连（用 snapshot_display 取图，分辨率即真机原生，原清单"竖屏 1080×2340"按 2in1 实际分辨率调整）。建材料目录 `docs\Tftpd32-市场材料\`。

**7 张截图逐一操作**（每张命名 `SG02_0N_页签名.jpeg`）：

| # | 内容 | 操作 |
|---|---|---|
| 1 | 设置页（服务"运行中"，可见端口与"启用 IPv4"） | 开启服务 → 切设置页 → snapshot |
| 2 | TFTP 页传输中（进度条 + 重传计数） | PC 后台发起 10MB GET（`python get_client.py 192.168.10.128 6900 test_10m.bin <本地>`），数秒内切到应用 TFTP 页 snapshot |
| 3 | 日志页（含 `sent N blks` 与 `Error 1` 两条对比） | 先 GET `test_1k.txt` 成功 → 再 GET 不存在文件 `python get_client.py 192.168.10.128 6900 notexist.txt <本地>`（客户端报 Error 1）→ 日志页 snapshot（两条记录同屏） |
| 4 | DHCP 页（地址池配置 + 租约表 ≥2 条） | 用 DH-01~09 同款 DHCP 探针（Temp 目录 `dhcp_probe.py`，缺失时按真机 §12 DH-01 报文格式以 scapy 重构：DISCOVER/REQUEST，flags=0x8000，两个不同 MAC，发往 `<DEV_IP>:6700`）产生 2 条租约 → DHCP 页 snapshot |
| 5 | 客户端页（一次 GET 的 done 状态） | PC 启动 tftpd64（端口 6901，根目录放 test_1k.txt）→ 应用"客户端"页填 `<PC_IP>:6901` GET → done 后 snapshot |
| 6 | 目录页（文件列表 + MD5 按钮） | 目录页 snapshot（根目录含 test_1b/1k/10m 即可） |
| 7 | 2in1 自由窗口形态 | 直接复用 §2 UI-07 拍的自由窗口截图 |

**材料一致性核对**：对照实际行为核对权限说明三条（INTERNET=网络传输与监听、GET_NETWORK_INFO=枚举网卡供绑定、KEEP_BACKGROUND_RUNNING=后台长时任务）；GPLv2 与源码地址正确。

**预期**：7 张齐全；每张能对应页签名；权限说明与实测行为无矛盾。

**失败处理**：第 4 张租约不足 → 如实少拍并在材料注明"租约表为空，运行时数据"；探针不可用时先补 DH 探针再拍。

**回填**：§12 新增 SG-02 行；§3 G7 状态行；未测 A-07；材料存 `docs\Tftpd32-市场材料\`。

---

## 5. UI-09 权限撤销降级（P2，G6，破坏性——靠后执行、测后恢复）

**判据（真机 §9、§1 D7、未测 A-05）**：撤销 GET_NETWORK_INFO 后绑定网卡降级为"全部接口"、其余功能可用；撤销通知权限后启动服务给出中文提示、服务仍可运行；恢复权限后无需重装。**本节同时覆盖 UI-06 的"拒绝授权"分支（判据③）。**

**前置**：UI-06 已完成（通知=允许基线）；服务停止；已 snapshot 记录设置页"绑定网卡"当前值。

**步骤**：

1. 系统设置 > 应用 > 应用管理 > Tftpd32 > 权限（2in1 路径可能是"应用和服务 > 应用管理"）→ **snapshot 确认当前权限列表** → 关闭"网络信息"（GET_NETWORK_INFO）。
2. 回应用 → 进入设置页 → 展开"绑定网卡"下拉 → **预期显示"全部接口"**（降级），无崩溃 → snapshot。
3. 其余功能可用：启动服务 → PC GET `test_1k.txt` 成功（MD5 `73F9EBD42260DB128B26155773C4710F`）。
4. 同一权限页关闭"通知" → 停止服务 → 重新启动服务：
   - **预期出现中文提示**（引导到设置开启通知），服务仍运行、GET 仍成功 → snapshot 提示文案（此即 UI-06 判据③"拒绝授权有中文引导"）。
5. **恢复**：权限页重新允许"通知"与"网络信息"→ 重启应用 → 设置页绑定网卡恢复网卡列表、启动服务通知恢复正常 → 记录"恢复无需重装"。

**预期**：4 项降级/引导全部成立；恢复后一切正常。

**取证**：权限列表前后、绑定网卡降级、通知中文提示、恢复后网卡列表共 5 张截图；GET 输出。

**失败处理**：撤销后崩溃/白屏 → 高优缺陷（D7 未覆盖此路径）；下拉仍列网卡 → 记录实际枚举值（可能是缓存），如实登记差异。

**回填**：§12 新增 UI-09 行；§3 G6 状态行；未测 A-05；同时补记未测 A-02 的判据③。

---

## 6. NF-07 断网 / 切热点（P2，G5，会切断 hdc 控制通道）

**判据（真机 §8、未测 A-01）**：服务不崩溃；恢复后端口重新可达（或日志明确提示需重启服务）。

**前置**：服务运行中；先抓一份 hilog 快照作断网前基线；确认可手工快速重连 Wi-Fi（记下 SSID/密码）。**已知约束**：shell 无 NET_ADMIN（`ifconfig wlan0 down` 不可用，勿再尝试）；hdc 无线调试走 wlan0，断网期间 hdc 失联属预期，测试期间仅靠 PC 侧探测取证。

**步骤**：

1. 基线：hdc 可达、GET 成功、hilog 快照存 `nf07_hilog_before.txt`；PC 开一个连续探测循环：
   ```powershell
   while ($true) {
     $t=Get-Date -Format HH:mm:ss
     python C:\Users\32572\AppData\Local\Temp\get_client.py 192.168.10.128 6900 test_1b.bin C:\Users\32572\AppData\Local\Temp\nf07_probe.bin | ForEach-Object { "$t $_" }
     Start-Sleep 10
   }   # Ctrl+C 结束；输出重定向到 nf07_probe_log.txt 更佳
   ```
2. 在真机上**手工**关闭 Wi-Fi（系统设置或控制中心）→ 立即观察应用界面是否崩溃/闪退（若应用在前台）。PC 探测循环将连续 TIMEOUT——保留输出。
3. 断开状态保持 1~2 分钟 → **手工重新连接 Wi-Fi**，等待 PC ping 通（`ping 192.168.10.128 -t` 观察）。
4. 重连后立即：
   ```powershell
   # 重连 hdc（0.2 序列）后
   & $hdc shell hilog -x > C:\Users\32572\AppData\Local\Temp\nf07_hilog_after.txt
   & $hdc shell "ps -ef | grep com.example.myapplication | grep -v grep"   # 进程是否仍为同一 pid
   & $hdc shell "netstat -anu | grep 6900"                                  # 端口是否仍在监听
   ```
5. PC 单发一次 GET 判定可达性；若不可达 → 看应用日志页/ hilog 是否有"需重启服务"类中文提示 → 有提示则重启服务后 GET，记录为"通过（带恢复机制）"。
6. 可选（切热点路径）：若存在第二热点且 PC 能同步接入该热点网段，重复 1~5；否则记录"热点路径未测"。

**预期**：进程存活（pid 不变或重启后自拉起）、无崩溃；重连后 GET 恢复 DONE（MD5 一致）或日志有明确"需重启服务"提示且重启后恢复。

**取证**：探测循环日志（断连窗口时间线）、前后 hilog、重连后 GET 输出、应用日志页截图。

**失败处理**：进程消失 → 查 hilog 崩溃记录提缺陷；端口丢失且无提示 → 提缺陷（对应 D4 网络状态监听缺失）；**测后若 hdc 一直连不上：真机上确认 Wi-Fi 已连，再执行 0.2 重连序列；仍失败改用 USB 直连。**

**回填**：§12 新增 NF-07 行；§3 G5 状态行；未测 A-01。

---

## 7. R6 端口占用（P2，G1）

**判据（真机 §4、未测 A-15）**：6900 被占时启动服务应提示"端口已被占用"（中文）、服务保持停止；释放后重启可绑定。

**现状（未测 A-15）**：仅源码级确认。已排除：SELinux 禁外部 ELF（push 二进制不可执行）、系统端口在其它 netns；且应用设 `SO_REUSEADDR`，此前探针出现"双绑成功"。

**步骤**：

1. 探针 A（shell 内占用，成本最低）：
   ```powershell
   & $hdc shell "which nc"                       # toybox nc 通常存在
   & $hdc shell "nc -u -l -p 6900"               # 挂住 UDP 6900（保持窗口/会话）
   # 另开窗口重连 hdc，确认占用生效：
   & $hdc shell "netstat -anu | grep 6900"
   ```
2. 保持占用 → 应用设置页**先 snapshot 确认** → 启动服务 → 观察提示与服务状态：
   - 若提示"端口已被占用"（中文）且服务保持"已停止" → 判据满足；
   - 若服务照常启动（SO_REUSEADDR 双绑成功）→ 如实记录，与源码级结论互证，登记为平台偏差候选（原版 Windows 语义：占用时 bind 失败）。
3. 释放（nc 会话 Ctrl+C / 断开）→ 重启应用服务 → 确认可绑定、GET 成功。
4. 探针 B（可选，更接近真实）：若能从应用市场安装可指定 UDP 监听端口的网络工具类应用，用其在应用沙箱内占用 6900 后重复 2~3（跨应用占用与 SO_REUSEADDR 的交互才是原判据本意）。

**预期**：按实测如实记录两种可能结果；释放后重启可绑定必须成立。

**取证**：nc 占用 netstat 输出、启动服务时应用提示截图、释放后 GET 输出。

**失败处理**：nc 不存在或被 SELinux 拦 → 记录输出，R6 维持阻塞；探针 B 无合适应用 → 记录"无可用第三方占用工具"，维持阻塞。

**回填**：§12 新增 R6 行（替换或追加）；§3 G1 状态行；未测 A-15。

---

## 8. R8 IPv6 腿（P1，G1，依赖 IPv6 环境——环境不具备时维持阻塞）

**判据（真机 §4 R8、未测 B-02）**：IPv6 地址族 GET 成功；日志中 IPv4 对端显示为普通 IPv4、不出现 `::ffff:`。

**现状（2026-11-03 复查维持阻塞）**：PC 无全局 IPv6（仅 ::1 与虚拟适配器 fe80）；真机 wlan0 无 inet6 地址。**双侧均无 v6 时本节不可执行。**

**环境具备性检查（每次执行前先跑）**：

```powershell
# PC 侧
netsh interface ipv6 show addresses     # 期望：WLAN 网卡有全球地址(2xxx:)或 fe80
# 真机侧（重连 hdc 后）
& $hdc shell "ifconfig wlan0"           # 期望：inet6 行存在
```

**环境构造路径（满足其一即可）**：
- A. 路由器开启 IPv6（PC 与真机同网段获得地址）——首选；
- B. USB 有线网卡直连真机并配置静态 IPv6（若系统支持）；
- C. 链路本地探针（低成本，两侧都有 fe80 即可试）：
  ```powershell
  # URL 中 zone id 的 % 要转义为 %25；WLAN 换成 PC 实际网卡名
  curl.exe -v "tftp://[<真机fe80地址>%25WLAN]:6900/test_1b.bin" -o C:\Users\32572\AppData\Local\Temp\r8_out.bin
  ```

**步骤（环境具备后）**：

1. 确认双栈：设置页"启用 IPv4/IPv6"均开、服务运行、`grep 1AF4 /proc/net/udp6` 可见监听。
2. PC 侧 IPv6 GET：
   ```powershell
   curl.exe -v "tftp://[<DEV_IP6>]:6900/test_1b.bin" -o C:\Users\32572\AppData\Local\Temp\r8_v6_out.bin
   certutil -hashfile C:\Users\32572\AppData\Local\Temp\r8_v6_out.bin MD5   # 期望 2408AD11F9EB830DA749E2A36A29EFF7
   ```
3. 同轮做一次 IPv4 GET（对照组）→ 打开日志页/ hilog 查看两条记录的"对端"字段：IPv4 对端应显示为普通 IPv4（无 `::ffff:` 前缀）。
4. 结果与未测 B-02 现状合并记录。

**预期**：IPv6 GET 成功且 MD5 一致；日志无 `::ffff:` 异常显示；IPv4 对照组不受影响。

**失败处理**：IPv6 请求超时 → `hdc shell netstat -anu | grep 6900` 确认监听仍存在，抓 hilog；链路本地探针不通（NDP/防火墙）不视为应用缺陷，换路径 A/B 再判。

**回填**：§12 R8 行追加 IPv6 腿结果；§3 G1 状态行；未测 B-02（若闭环则联动合计 9→8）。

---

## 9. SG-01 正式签名与覆盖升级（P0，G7，依赖发布证书——当前阻塞）

**判据（真机 §10、§1 D8、未测 A-06）**：正式发布证书/profile 签名后安装成功；覆盖升级后配置（tftpd32.ini）与租约（Lease_*）保留、服务可启动。

**前置**：已获得正式发布证书（.cer/.p12）与 profile（.p7b）。**未获得时本节维持阻塞**；获得后按以下步骤执行。

**步骤**：

1. 制造升级前数据（先于重装）：
   - 服务运行中，设置页点"保存配置"（当前 ini 落定：端口 6900/标准级/BaseDirectory 等）；
   - 用 DHCP 探针产生 ≥1 条租约（同 SG-02 第 4 张的方法）；
   - 留存基线：`hdc shell cat .../tftpd32.ini > sg01_ini_before.txt`。
2. 正式签名（复用真机 §2.2 手动流程，材料替换为正式证书）：
   ```powershell
   & "D:\Program Files\Huawei\DevEco Studio\jbr\bin\java.exe" -jar `
     "D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\lib\hap-sign-tool.jar" sign-app `
     -mode localSign -keyAlias <正式key别名> -keyPwd <密码> -signAlg SHA256withECDSA -signCode 1 -compatibleVersion 24 `
     -appCertFile <正式证书.cer> -profileFile <正式profile.p7b> -keystoreFile <正式.p12> -keystorePwd <密码> `
     -inFile entry\build\default\outputs\default\entry-default-unsigned.hap -outFile entry-release-signed.hap
   ```
3. 覆盖安装：`& $hdc install -r entry-release-signed.hap`。
   - **若报签名不一致无法覆盖**：如实记录——本地覆盖安装要求签名同链，市场渠道升级不受此限；此时若需继续验证数据保留，只能走真实市场升级路径，或改用同链正式 profile 重签调试侧，记录处置方式。
4. 冷启动 → 检查：端口/安全级别/根目录按升级前配置回填；DHCP 页租约保留；`cat .../tftpd32.ini` 对比 `Lease_*` 与配置键逐项一致（diff sg01_ini_before.txt）。
5. 启动服务 → PC GET 成功。

**预期**：安装成功；升级后 ini 与 Lease_* 保留；服务可启动、传输正常。

**失败处理**：数据丢失（ini 回默认）→ 高优缺陷（D8 数据迁移）；安装失败且非签名问题 → 记录错误码另查。

**回填**：§12 新增 SG-01 行；§3 G7 状态行；未测 A-06（若闭环则联动合计 9→8）。

---

## 10. 建议执行顺序与理由

| 序 | 用例 | 理由 |
|---|---|---|
| 1 | UI-06 | 先建立"通知已授权"基线；其拒绝分支延后到 UI-09 |
| 2 | UI-07 | 系统形态无副作用；顺带产出 SG-02 第 7 张截图 |
| 3 | UI-08 | 改系统显示设置，测后立即还原，为 SG-02 材料截图扫清状态 |
| 4 | SG-02 | 需在"浅色/默认DPI/权限齐全"下拍摄；先制造 DHCP 租约 |
| 5 | UI-09 | 破坏性（撤销权限），必须在 UI-06/SG-02 之后；测后恢复权限 |
| 6 | NF-07 | 断网会切断 hdc 控制通道，测后需手动重连，放在可控操作之后 |
| 7 | R6 | shell 探针独立执行；结果依赖"应用 vs 占用方"的实际行为 |
| 8 | R8 IPv6 腿 | 仅在 IPv6 环境具备时执行；不具备则维持阻塞 |
| 9 | SG-01 | 依赖正式证书到位，放最后；闭环后合计 9→8 |

> 执行期间临时打断无碍——各用例相互独立，按 0.5 还原清单处理当前状态后可随时续接。全部回填后，未测报告合计应从 9 递减，最终仅剩确实无法构造环境的项（如 R6/R8 的环境腿）如实保留阻塞说明。
