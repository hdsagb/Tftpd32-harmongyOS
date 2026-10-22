# Tftpd32 for HarmonyOS

Tftpd32/Tftpd64 的鸿蒙（HarmonyOS）原生适配版。沿用原版的配置键名、报文语义与界面布局，
提供 TFTP 服务器/客户端、Syslog、SNTP、DNS、DHCP 服务与 MD5、目录浏览功能。

## 上游与许可证

本项目是 [Tftpd32/Tftpd64](https://tftpd32.jounin.net/)（作者 Philippe Jounin）的衍生作品，
遵循 **GNU GPLv2** 发布，完整许可证见 [LICENSE](LICENSE)。

- 上游版权归 Philippe Jounin 所有；`entry/src/main/cpp/tftpd/` 下保留原作者版权头的文件均源自上游。
- 其余 `*_pal.c` / `*_pal.h` 等文件为 2026 年鸿蒙适配新增或改写，用于替换 Win32 API 与 GUI 消息层，
  协议逻辑保持与原版一致。
- 本仓库即对应完整源码；分发二进制时须同时提供本仓库源码与构建说明（GPLv2 §3）。

## 构建

依赖 DevEco Studio（HarmonyOS SDK 6.1.1(24) 及以上）。

```powershell
$env:DEVECO_SDK_HOME='D:\Program Files\Huawei\DevEco Studio\sdk'
$env:JAVA_HOME='D:\Program Files\Huawei\DevEco Studio\jbr'
$env:Path="$env:JAVA_HOME\bin;$env:Path"

# Debug
& 'D:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat' assembleHap `
    --mode module -p product=default -p buildMode=debug --no-daemon

# Release（-O2 -s，静态 libc++，HAP 约 1.0 MB）
& 'D:\Program Files\Huawei\DevEco Studio\tools\hvigor\bin\hvigorw.bat' assembleHap `
    --mode module -p product=default -p buildMode=release --no-daemon
```

产物：`entry/build/default/outputs/default/entry-default-unsigned.hap`，安装前需签名。

### 一键构建 + 签名 + 安装

```powershell
# 构建 release、签名并覆盖安装到已连接设备
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1

# 常用变体
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -Launch          # 装完自动启动
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -Uninstall       # 先卸载再装（签名材料变更时用）
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -BuildMode debug # 构建 debug 包
powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -NoInstall        # 只出已签名 HAP
```

脚本会自动完成：定位 DevEco 工具链 → `hvigor assembleHap` → 首次生成调试签名材料（`signature\`，含私钥，已加入 `.gitignore`）→ 用当前设备 UDID 生成并签名 debug profile → 签 HAP → `hdc install -r`。签名材料持久复用，因此后续运行可覆盖安装、保留应用数据。

## 运行说明

- 应用启动后自动启动已启用的服务；启动失败时保持停止状态，可在“设置”页手动开启。
- 鸿蒙应用无法绑定 1024 以下特权端口：TFTP 默认 69 → 6900，Syslog 514 → 5140，
  SNTP 123 → 1230，DNS 53 → 5300，DHCP 67 → 6700，均可在界面修改。
- 服务运行期间申请长时任务；设备不支持时（如模拟器返回 9800005）降级为常驻通知，
  并提示在“设置 > 电池”中允许后台活动。
- 配置文件为沙箱内 `tftpd32.ini`，键名与原版注册表键一致，可直接迁移。

## 应用信息（市场材料）

> 供应用市场提交时直接引用；截图需在真机上补拍（见[真机开发与测试方案](docs/Tftpd32-真机开发与测试方案.md) §10 SG-02）。

**一句话简介**：把 PC 上经典的 Tftpd32 搬到鸿蒙设备的本地网络工具集，一个应用提供 TFTP、Syslog、
SNTP、DNS、DHCP 五项服务与内置 TFTP 客户端。

**应用简介（约 200 字）**：Tftpd32 for HarmonyOS 是开源工具 Tftpd32/Tftpd64 的鸿蒙适配版，面向
网络运维、嵌入式开发与设备调试场景。应用内置 TFTP 服务器与客户端（支持 RFC2347/2348/2349 选项协商、
RFC2090 组播、安全级别与目录权限控制），并提供 Syslog、SNTP、DNS、DHCP 服务；配置键名与原版一致，
PC 上的 `tftpd32.ini` 可直接迁移。服务在后台通过长时任务保活，退到后台仍可继续响应请求。
应用完全本地运行，不上传任何数据，无账号与统计 SDK。

**权限用途说明**：

| 权限 | 用途 |
|---|---|
| `ohos.permission.INTERNET` | 监听与收发 TFTP/Syslog/SNTP/DNS/DHCP 报文的必需权限，仅用于局域网内与本应用建立的连接 |
| `ohos.permission.GET_NETWORK_INFO` | 枚举本机网卡地址，用于“绑定网卡（LocalIP）”与 DHCP 地址池配置 |
| `ohos.permission.KEEP_BACKGROUND_RUNNING` | 服务运行期间申请长时任务，避免应用退到后台后套接字被系统回收 |

**其它需公开的信息**：许可证 GNU GPLv2（见 [LICENSE](LICENSE)）；原作者 Philippe Jounin；
完整源码即本仓库；无付费内容、无广告、无第三方数据收集。

## 文档

- [HarmonyOS 适配方案](docs/Tftpd32-HarmonyOS适配方案.md)
- [功能测试方案](docs/Tftpd32-功能测试方案.md)
- [真机开发与测试方案](docs/Tftpd32-真机开发与测试方案.md)
- [M3/M4 计划](docs/superpowers/plans/2026-10-21-tftpd32-m3-m4-plan.md)
