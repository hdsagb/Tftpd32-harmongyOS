# Tftpd32 for HarmonyOS 合规审查报告

| 项目 | 内容 |
|---|---|
| 审查对象 | `hdsagb/Tftpd32-harmongyOS`（本地 master @ `b1a20fa`，已同步远端 main） |
| 审查日期 | 2026-10-22 |
| 审查范围 | 开源许可证合规、第三方代码与素材、应用配置与权限、隐私与数据、发布前配置 |
| 审查方法 | 逐文件版权头抽样（`entry/src/main/cpp/tftpd/` 全量 34 文件）、`LICENSE`/README 比对、`module.json5`/`oh-package.json5`/`app.json5` 声明核对、`.gitignore` 与 git 跟踪清单核对 |

## 一、总体结论

**有条件合规。** 开源许可证链路（上游 EUPL-1.2 → 本项目 GPLv2）、权限最小化、无第三方依赖、无私密材料入库等主体项全部通过；存在 1 项需在下次提交前整改的问题（LICENSE 文本非逐字原文）与 1 项应在发布前整改的问题（衍生文件版权头缺失），其余为发布前配置类提示项。全部整改动作不涉及协议逻辑代码。

## 二、通过项

| # | 审查点 | 结论 | 证据 |
|---|---|---|---|
| P1 | 许可证选择合法 | 上游 PJO2/tftpd64 声明 EUPL-1.2（其 readme.txt "license clarification" 与各源文件头为准），EUPL-1.2 附录将 GPL v2.0 列为兼容许可，本项目按 GPL-2.0-only 分发合法 | README「上游与许可证」；上游 `readme.txt`、`src/_services/tftpd_thread.c` 文件头 |
| P2 | 上游署名 | README 明确作者 Philippe Jounin 与衍生作品身份，并解释上游 GitHub GPL-2.0 徽章的来历（`license.md` 历史遗留） | README L6-20（提交 `b1a20fa`） |
| P3 | 上游文件版权头保留 | `entry/src/main/cpp/tftpd/` 34 个 C/H 文件中 17 个直接沿用上游的文件均保留 "Ph.jounin"/"European Union Public License" 字样 | 全量文件头扫描（2026-10-22） |
| P4 | GPL §3 源码提供 | 仓库即完整源码（含 `tools/build-install.ps1` 构建+签名+安装脚本、宿主测试工程）；README 提供构建说明 | `git ls-files`；README「构建」 |
| P5 | 权限最小化 | 仅声明 3 项权限（INTERNET / GET_NETWORK_INFO / KEEP_BACKGROUND_RUNNING），与功能一一对应且用途已在 README 逐条公开；无位置、存储、设备信息等扩展权限 | `entry/src/main/module.json5` `requestPermissions` |
| P6 | 无第三方依赖与数据收集 | 运行时依赖仅本地 `libtftpd32.so`；devDependencies 仅 hypium/hamock 测试框架；无广告、统计、崩溃上报 SDK，README 相应声明属实 | 根/entry `oh-package.json5` |
| P7 | 私密与上游材料未发布 | `signature/`（签名私钥 p12）、`reference/`（上游源码副本）、`build/` 均在 `.gitignore` 且未入库 | `.gitignore`；`git status` 干净 |
| P8 | 第三方库（MD5）许可证保留 | `libs/md5/md5.c` 完整保留 RSA Data Security MD5C.C 原许可证头 | `md5.c` L1-14 |
| P9 | 应用图标无上游素材 | 图标为新建分层 PNG（foreground/background/startIcon），非从上游 `tftpd32.exe` 或 `.ico` 提取 | `resources/base/media` 清单 |
| P10 | 无真实密钥泄露 | 仓库内唯一的口令字面量是本地生成调试密钥库的口令（`build-install.ps1` `$Pass`），密钥库文件本身不在仓库，不构成泄密 | `tools/build-install.ps1` |

## 三、发现项与整改要求

### F1（高，下次提交前）LICENSE 文本非逐字 GPLv2 原文

`LICENSE` 首行为自加的 "GPLv2" 标题，正文段落被合并重排（如 Preamble 合并为长段），缺失 "GNU GENERAL PUBLIC LICENSE / Version 2, June 1991" 原始版式。许可证文档自身声明 *"Everyone is permitted to copy and distribute verbatim copies of this license document, but changing it is not allowed"*，重排文本存在被质疑非 GPLv2 的风险。

**整改**：以 https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt 逐字替换 `LICENSE` 全文，不得增删改任何字符。

### F2（中，应用市场发布前）部分衍生文件缺少来源标注

17 个无上游版权头的文件中，以下经比对确认含上游移植逻辑，按 GPLv2 §2a 应带来源/修改声明：

`tftp_client.c`（移植自 `_gui/tftp_cli.c`）、`service_main.c`、`dns_hosts.c`、`bootpd_functions.h`、`tftpd_functions.h`、`dhcp.h`、`headers.h`、`_hdr_dialog_socket.h`、`console_pal.c`

**整改**：为上述文件补统一头注释，例如：

```
/* Derived from Tftpd32/Tftpd64 (Copyright (C) Philippe Jounin, EUPL-1.2),
 * modified for HarmonyOS. This file is distributed under GPLv2 as part
 * of the Tftpd32 for HarmonyOS project. */
```

其余 8 个（`*_pal.c/h`、`ip_pal.c`、`registry_pal.c`、`settings_pal.c`、`eventlog_pal.c`、`async_log_pal.c`、`file_tool_pal.c`、`pal_compat.*`）为鸿蒙适配新写文件，建议同样补一行来源声明，非强制。

### F3（低，发布前）RSA MD5 署名条款

MD5C.C 许可要求：凡提及该软件的材料须标注 *"RSA Data Security, Inc. MD5 Message-Digest Algorithm"*，衍生作品须标注 *"derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm"*。README 功能介绍提及 MD5 但未加标注（文件头已保留，属部分合规）。

**整改**：在 README「上游与许可证」补一句：本项目 MD5 实现 derived from the RSA Data Security, Inc. MD5 Message-Digest Algorithm。

### F4（低，市场发布前）应用标识为模板占位值

- `bundleName: com.example.myapplication`、`vendor: example`——需改为反域名格式自有标识（上架后不可改）；
- `app_name: "Tftpd32"`——与上游同名。上游未主张商标，但作为非官方衍生作品，建议市场名称使用描述性形式（如 "Tftpd32 for HarmonyOS"）并在简介首行注明非官方移植、与原作者无隶属关系，避免混淆；
- `versionName 1.0.0`/`versionCode 1000000` 上架前确认。

### F5（低，随手改）包描述文件缺许可证字段

`entry/oh-package.json5` 的 `"license": ""` 为空，建议填 `GPL-2.0-only`；根 `oh-package.json5` 的 description 仍是模板文本。

## 四、发布门禁（合规侧）

正式上架前须全部完成：

1. ~~F1 LICENSE 替换为逐字原文~~ ✅；
2. ~~F2 补齐衍生文件头注释~~ ✅；
3. ~~F3 README 补 MD5 署名~~ ✅；
4. ~~F5 包描述文件许可证字段~~ ✅；
5. F4 应用标识（bundleName/vendor/名称）定稿并核对真机重装；
6. 真机开发与测试方案（E2）全部用例通过（D8 正式签名、D9 截图含最终标识）。

## 五、整改记录

2026-10-22 完成 F1 / F2 / F3 / F5 整改：

- **F1**：以 `https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt` 逐字原文替换 `LICENSE`（17984 字节 / 338 行，LF，无 BOM）。
- **F2**：为 9 个衍生文件补齐统一头注释（来源 Philippe Jounin / EUPL-1.2，本项目按 GPLv2 分发）：`tftp_client.c`、`service_main.c`、`dns_hosts.c`、`bootpd_functions.h`、`tftpd_functions.h`、`dhcp.h`、`headers.h`、`_hdr_dialog_socket.h`、`console_pal.c`。
- **F3**：README「上游与许可证」补 RSA Data Security MD5 署名条目。
- **F5**：`entry/oh-package.json5` `license` 字段填 `GPL-2.0-only`；根 `oh-package.json5` `description` 替换模板文本。
- 未改动：F4 市场发布标识（bundleName/vendor/app_name）保留为发布前定稿项。

整改提交：`docs:` 许可证与衍生版权头修正（含本次报告更新）。

## 六、风险声明

本报告为工程侧自查，不构成法律意见；EUPL 与 GPLv2 的兼容性依据为 EUPL-1.2 附录兼容许可清单及上游作者声明，如需商业分发建议咨询法务。

