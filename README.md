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

产物：`entry/build/default/outputs/default/entry-default-unsigned.hap`，安装前需签名：

```powershell
java -jar "<SDK>\default\openharmony\toolchains\lib\hap-sign-tool.jar" sign-app `
  -keyAlias "OpenHarmony Application Release" -signAlg SHA256withECDSA -mode localSign `
  -appCertFile <appchain.pem> -profileFile <debug-profile.p7b> `
  -inFile entry\build\default\outputs\default\entry-default-unsigned.hap `
  -keystoreFile "<SDK>\default\openharmony\toolchains\lib\OpenHarmony.p12" `
  -outFile entry-signed.hap -keyPwd 123456 -keystorePwd 123456 -signCode 1
```

## 运行说明

- 应用启动后自动启动已启用的服务；启动失败时保持停止状态，可在“设置”页手动开启。
- 鸿蒙应用无法绑定 1024 以下特权端口：TFTP 默认 69 → 6900，Syslog 514 → 5140，
  SNTP 123 → 1230，DNS 53 → 5300，DHCP 67 → 6700，均可在界面修改。
- 服务运行期间申请长时任务；设备不支持时（如模拟器返回 9800005）降级为常驻通知，
  并提示在“设置 > 电池”中允许后台活动。
- 配置文件为沙箱内 `tftpd32.ini`，键名与原版注册表键一致，可直接迁移。

## 文档

- [HarmonyOS 适配方案](docs/Tftpd32-HarmonyOS适配方案.md)
- [功能测试方案](docs/Tftpd32-功能测试方案.md)
- [真机测试清单](docs/Tftpd32-真机测试清单.md)
- [M3/M4 计划](docs/superpowers/plans/2026-10-21-tftpd32-m3-m4-plan.md)
