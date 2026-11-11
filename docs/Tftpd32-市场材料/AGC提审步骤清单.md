# AGC 提审步骤清单（华为应用市场上架）

> 生成日期：2026-11-11。分工：**AGC 后台操作（建应用、备案、证书、上传、提审）由用户完成**；工程侧（改标识、写材料、重签打包、真机核验）由助手完成。
> 依据：合规审查报告发布门禁、真机方案 SG-01/SG-02、剩余9项指南 §4、未测报告（0 项未闭环）。

## 一、已定稿应用标识（上架后不可改项）

| 字段 | 定稿值 | 来源 |
|---|---|---|
| 应用名称（市场展示名） | Tftpd32 | [AppScope string.json](../../AppScope/resources/base/element/string.json) app_name |
| bundleName | io.github.hdsagb.tftpd32harmongyOS | [AppScope app.json5](../../AppScope/app.json5)（2026-11-11 更换，Index.ets BUNDLE_NAME 已同步） |
| vendor | hdsagb | app.json5 |
| versionName / versionCode | 1.0.0 / 1000002 | app.json5 |
| 版权/许可证 | GNU GPL v2（上游 Tftpd32/Tftpd64，作者 Philippe Jounin，EUPL-1.2）；非官方移植声明已写入应用简介与应用内「关于」 | README「应用信息」「上游与许可证」 |
| 隐私政策 | 仓库根 [PRIVACY.md](../../PRIVACY.md)（已代拟，无数据收集声明） | 2026-11-11 新增 |

## 二、材料清单

### 已就绪
| 材料 | 位置 | 说明 |
|---|---|---|
| 一句话简介 | [README.md](../../README.md)「应用信息」 | 「把 PC 上经典的 Tftpd32 搬到鸿蒙设备的本地网络工具集……」 |
| 应用简介（约 200 字） | 同上 | 含非官方移植声明与「完全本地运行，不上传任何数据」 |
| 权限用途说明 ×3 | 同上 | INTERNET / GET_NETWORK_INFO / KEEP_BACKGROUND_RUNNING |
| 7 张真机截图 | [docs/test-evidence/SG-02/](../test-evidence/SG-02) | SG02_01 设置页 ~ SG02_07 自由窗口（jpeg，真机原生分辨率） |
| 隐私政策文本 | [PRIVACY.md](../../PRIVACY.md) | 无账号、无数据收集、权限用途、本地存储说明 |
| LICENSE | [LICENSE](../../LICENSE) | 逐字 GPLv2 原文（F1 已整改） |
| 上架候选代码 | 本仓库（versionCode 1000002，含「启用 IPv6」开关） | 需按 §三 重签后打包 |

### 待办（用户侧）
| # | 待办 | 说明 |
|---|---|---|
| U1 | Gitee 建仓库并推送 | gitee.com/hdsagb/Tftpd32-harmongyOS（含 PRIVACY.md）；大陆访问稳定，审核可打开 |
| U2 | AGC 实名认证账号 | 个人/企业开发者账号 |
| U3 | AGC 创建 HarmonyOS 应用 | 包名 io.github.hdsagb.tftpd32harmongyOS、名称 Tftpd32、类别建议「工具 > 系统工具」 |
| U4 | App 备案（工信部） | 经 AGC/华为云备案系统提交个人主体备案（App 名称、包名、平台 HarmonyOS）；**中国区提审必填备案号** |
| U5 | AGC 发布证书 + 发布 Profile | 证书管理 → 新建发布证书（.cer/.p12）+ 发布 Profile（.p7b，绑定上述包名）→ 下载后提供给助手重签 |
| U6 | 版权证明材料 | 免费开源应用通常填版权归属声明即可；若审核要求补充软著证书再提供 |
| U7 | AGC 表单填写与上传 | 按 §四 逐项填写 → 上传 .app → 提审 |

### 待办（助手侧）
| # | 待办 | 触发条件 |
|---|---|---|
| A1 | build-profile.json5 切换发布签名材料 → hvigor assembleApp 重签打包 .app | 收到 U5 的发布证书/Profile |
| A2 | 新 bundleName 全新安装核验（真机）：卸载旧包 → 安装上架包 → 服务自启/GET/配置保留冒烟 | A1 出包后（并入真机 D8/D9 收尾） |
| A3 | 导出 216×216 应用图标 PNG（layered_image 前景+背景合成） | 随时可做，AGC 建应用时上传 |

## 三、打包与签名说明（助手侧执行细节）

- 上架包必须用 **AGC 发布证书 + 发布 Profile** 签名（当前 build-profile.json5 指向 signature/T32-testRelease.p7b 内测 profile，仅用于真机调试与 SG-01 升级验证，不可用于上架）。
- 打包命令（hvigor，env：NODE_HOME=$deveco\tools\node，PATH 前插 node+jbr\bin，DEVECO_SDK_HOME=$deveco\sdk）：
  `hvigorw --mode project -p product=default -p buildMode=release assembleApp --no-daemon`
- 产物：`build\outputs\default\Tftpd32-harmongyOS-default-signed.app`（重签后同路径覆盖）。
- 新 bundleName 与设备上已装的 com.example.tftpd32harmongyOS 互不兼容：核验时需先卸载旧包，全新安装（SG-01 覆盖升级链路不延续，属预期）。

## 四、AGC 提审表单字段对照（用户侧照填）

| AGC 字段 | 填写值 |
|---|---|
| 应用名称 | Tftpd32 |
| 应用包名 | io.github.hdsagb.tftpd32harmongyOS（建应用时选定后不可改） |
| 应用类别 | 工具（或：软件服务 > 系统工具，按 AGC 现行分类树） |
| 上架国家/地区 | 中国（如需海外分发另行勾选，隐私政策需相应语言版） |
| 版本号（包内自动读出） | versionName 1.0.0 / versionCode 1000002 |
| 版本说明（更新日志） | 「首次发布：TFTP 服务器/客户端（RFC2347/2348/2349 选项协商、RFC2090 组播）与 Syslog、SNTP、DNS、DHCP 服务；配置键名与原版 Tftpd32 一致，tftpd32.ini 可直接迁移。」 |
| 一句话简介 | README「应用信息」第 1 条 |
| 应用简介 | README「应用简介」段落 |
| 截图 | SG02_01~SG02_06（自由窗口 1 张可选附） |
| 应用图标 | 216×216 PNG（待办 A3 导出） |
| 隐私政策 URL | `https://gitee.com/hdsagb/Tftpd32-harmongyOS/blob/master/PRIVACY.md`（U1 完成后生效，提审前自查可打开） |
| 版权信息 | 「本应用为开源软件（GNU GPL v2），版权归属 Tftpd32 for HarmonyOS 项目贡献者；上游 Tftpd32/Tftpd64 © Philippe Jounin（EUPL-1.2）。」 |
| 权限用途 | AGC 自动读取 requestPermissions 后逐条填写 README 权限用途说明对应文案 |
| App 备案号 | U4 完成后取得，必填 |
| 分发方式 | 免费 |
| 是否含广告/内购/账号系统 | 均否（与 PRIVACY.md、README 声明一致） |

## 五、风险与注意

1. **bundleName 已更换**：AGC 建应用必须选 io.github.hdsagb.tftpd32harmongyOS；此前 AGC 若已按旧包名建过应用/Profile，全部作废重建。
2. **合规门禁状态**（合规报告 §四）：1~5 全部 ✅；第 6 条已执行用例全部通过，R8 IPv6 腿经决策豁免（未测报告 B-02），D8 待发布 Profile 重签后闭环。
3. GitHub 仓库（github.com/hdsagb/Tftpd32-harmongyOS）为源码公开主仓库（GPL §3）；Gitee 侧仅作隐私政策载体与镜像，二者内容保持一致。
4. PC WLAN IPv6 绑定（测试遗留）尚未还原禁用，与上架无关但需处理：管理员 PowerShell 执行 `Disable-NetAdapterBinding -Name 'WLAN' -ComponentID ms_tcpip6`。
