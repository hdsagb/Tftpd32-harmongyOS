﻿# build-install.ps1 —— 一键构建 + 签名 + 安装
#
# 用法（在项目根目录执行）：
#   powershell -ExecutionPolicy Bypass -File tools\build-install.ps1
#   powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -Uninstall   # 签名材料变更后先卸载
#   powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -BuildMode debug -Launch
#
# 签名材料首次运行自动生成于 signature\（含私钥，已加入 .gitignore，勿提交）。
# 后续运行复用同一私钥，保证应用可覆盖安装（升级）。
param(
    [ValidateSet('release', 'debug')]
    [string] $BuildMode = 'release',
    [switch] $NoInstall,   # 只构建 + 签名，不安装
    [switch] $Uninstall,   # 先卸载已安装版本（签名证书变化导致 9568332 时使用）
    [switch] $Launch       # 安装后启动应用
)

$ErrorActionPreference = 'Continue'   # 原生命令会向 stderr 输出进度信息，用 $LASTEXITCODE 判断失败

$Root   = Split-Path -Parent $PSScriptRoot
$SignDir = Join-Path $Root 'signature'
$Pass     = '123456'          # 本地调试签名口令（与自己生成的密钥库配套）
$AppName = 'Tftpd32'

function Write-Step([string] $msg) { Write-Host "==== $msg ====" -ForegroundColor Cyan }
function Write-Err([string] $msg)  { Write-Host "错误：$msg" -ForegroundColor Red; exit 1 }

# ---------- 1. 定位 DevEco 工具链 ----------
function Find-DevEcoHome {
    $cands = @()
    if ($env:DEVECO_SDK_HOME) { $cands += (Split-Path -Parent $env:DEVECO_SDK_HOME) }
    if ($env:DEVECO_HOME)     { $cands += $env:DEVECO_HOME }
    $cands += 'D:\Program Files\Huawei\DevEco Studio'
    $cands += 'C:\Program Files\Huawei\DevEco Studio'
    foreach ($c in $cands) {
        if ($c -and (Test-Path (Join-Path $c 'sdk\default\openharmony\toolchains\lib\hap-sign-tool.jar'))) { return $c }
    }
    Write-Err "未找到 DevEco Studio，请设置环境变量 DEVECO_HOME（例如：$env:DEVECO_HOME = 'D:\Program Files\Huawei\DevEco Studio'）"
}

$DevEco  = Find-DevEcoHome
$Java    = Join-Path $DevEco 'jbr\bin\java.exe'
$Keytool = Join-Path $DevEco 'jbr\bin\keytool.exe'
$Hdc     = Join-Path $DevEco 'sdk\default\openharmony\toolchains\hdc.exe'
$LibDir  = Join-Path $DevEco 'sdk\default\openharmony\toolchains\lib'
$SignTool = Join-Path $LibDir 'hap-sign-tool.jar'
$SdkP12   = Join-Path $LibDir 'OpenHarmony.p12'
$ProfileCert = Join-Path $LibDir 'OpenHarmonyProfileDebug.pem'
$ProfileKeyAlias = 'OpenHarmony Application Profile Debug'

foreach ($f in @($Java, $Keytool, $Hdc, $SignTool, $SdkP12, $ProfileCert)) {
    if (-not (Test-Path $f)) { Write-Err "工具链文件缺失：$f" }
}
New-Item -ItemType Directory -Force -Path $SignDir | Out-Null

# ---------- 2. 读取应用信息 ----------
$appJson = Get-Content (Join-Path $Root 'AppScope\app.json5') -Raw
$BundleName = [regex]::Match($appJson, '"bundleName"\s*:\s*"([^"]+)"').Groups[1].Value
$VersionName = [regex]::Match($appJson, '"versionName"\s*:\s*"([^"]+)"').Groups[1].Value
$VersionCode = [regex]::Match($appJson, '"versionCode"\s*:\s*(\d+)').Groups[1].Value
if (-not $BundleName) { Write-Err "无法从 AppScope\app.json5 读取 bundleName" }

# ---------- 3. 构建 ----------
Write-Step "构建 $BuildMode HAP"
Push-Location $Root
try {
    $hvigor = Join-Path $DevEco 'tools\hvigor\bin\hvigorw.bat'
    if (-not (Test-Path $hvigor)) { $hvigor = Join-Path $Root 'hvigorw.bat' }
    if (-not (Test-Path $hvigor)) { Write-Err "未找到 hvigorw.bat（检查 DevEco Studio 安装）" }

    # hvigor 需要 Node，优先使用 DevEco 自带版本
    $nodeHome = Join-Path $DevEco 'tools\node'
    if (Test-Path $nodeHome) {
        $env:NODE_HOME = $nodeHome
        $env:PATH = "$nodeHome;$env:PATH"
    }
    # PackageHap 阶段会 spawn java，需把 DevEco 自带 JBR 加入 PATH
    $env:PATH = "$(Join-Path $DevEco 'jbr\bin');$env:PATH"
    # 未配置或配置无效时指向 DevEco 自带 SDK，否则 hvigor 报 00303217
    if (-not $env:DEVECO_SDK_HOME -or -not (Test-Path $env:DEVECO_SDK_HOME)) {
        $env:DEVECO_SDK_HOME = Join-Path $DevEco 'sdk'
    }
    & $hvigor --mode module -p product=default -p buildMode=$BuildMode assembleHap --no-daemon 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Err "hvigor 构建失败（退出码 $LASTEXITCODE）" }
}
finally { Pop-Location }

$outDir = Join-Path $Root 'entry\build\default\outputs\default'
$unsigned = Get-ChildItem $outDir -Filter '*-unsigned.hap' -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $unsigned) { Write-Err "未找到未签名 HAP：$outDir" }

# ---------- 4. 准备签名材料（首次生成，之后复用私钥） ----------
Write-Step "准备签名材料"
$AppP12   = Join-Path $SignDir 'tftpd32.p12'
$RootCer  = Join-Path $SignDir 'root-ca.cer'
$SubCaCer = Join-Path $SignDir 'sub-ca.cer'
$AppChain = Join-Path $SignDir 'app-chain.cer'
$ProfileJson = Join-Path $SignDir 'profile.json'
$ProfileP7b  = Join-Path $SignDir 'profile.p7b'
$SignedHap   = Join-Path $SignDir 'entry-signed.hap'
$AppKeyAlias = 'tftpd32-key'

if (-not (Test-Path $AppP12)) {
    Write-Host '  生成应用密钥对（仅首次）'
    & $Java -jar $SignTool generate-keypair -keyAlias $AppKeyAlias -keyPwd $Pass `
        -keyAlg ECC -keySize NIST-P-256 -keystoreFile $AppP12 -keystorePwd $Pass 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Err "generate-keypair 失败" }
}

# 从 SDK 密钥库导出 CA 链（DER）。注意 keytool -exportcert 不加 -rfc 即为 DER；
# 目标文件已存在时 keytool 会交互询问覆盖，故逐个判断
if (-not (Test-Path $RootCer)) {
    Write-Host '  导出 SDK 根 CA 证书（仅首次）'
    & $Keytool -exportcert -alias 'openharmony application root ca' -keystore $SdkP12 `
        -storepass $Pass -storetype PKCS12 -file $RootCer 2>&1 | Out-Null
}
if (-not (Test-Path $SubCaCer)) {
    Write-Host '  导出 SDK 中间 CA 证书（仅首次）'
    & $Keytool -exportcert -alias 'openharmony application ca' -keystore $SdkP12 `
        -storepass $Pass -storetype PKCS12 -file $SubCaCer 2>&1 | Out-Null
}
if (-not (Test-Path $RootCer) -or -not (Test-Path $SubCaCer)) { Write-Err "SDK CA 证书导出失败" }

# 设备 UDID（debug profile 需要）
$udids = & $Hdc shell bm get --udid 2>$null | Select-String -Pattern '^[0-9A-Fa-f]{32,}$'
$Udid = if ($udids) { $udids[0].Line.Trim() } else { '' }
if (-not $Udid) { Write-Err "未获取到设备 UDID，请确认 hdc 已连接设备（hdc list targets）" }

# issuer / subject 必须使用 DN 的 DER 顺序（C,O,OU,CN），否则证书链校验会报 issuer subject failed
# 证书链只在首次生成：重新生成会产生新证书，导致 appId 变化、覆盖安装报 9568332
if (-not (Test-Path $AppChain)) {
    Write-Host "  生成应用证书链（仅首次）"
    & $Java -jar $SignTool generate-app-cert -keyAlias $AppKeyAlias -keyPwd $Pass `
        -issuer "C=CN,O=OpenHarmony,OU=OpenHarmony Team,CN=OpenHarmony Application CA" `
        -issuerKeyAlias 'OpenHarmony Application CA' -issuerKeyPwd $Pass `
        -issuerKeystoreFile $SdkP12 -issuerKeystorePwd $Pass `
        -subject "C=CN,O=OpenHarmony,OU=OpenHarmony Team,CN=$AppName Debug" `
        -validity 3650 -signAlg SHA256withECDSA `
        -keystoreFile $AppP12 -keystorePwd $Pass `
        -outForm certChain -rootCaCertFile $RootCer -subCaCertFile $SubCaCer -outFile $AppChain 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $AppChain)) { Write-Err "generate-app-cert 失败" }
}

$chain = [IO.File]::ReadAllText($AppChain)
$leaf  = ($chain -split '(?=-----BEGIN CERTIFICATE-----)')[1].TrimEnd() + "`n"
[IO.File]::WriteAllText((Join-Path $SignDir 'app-leaf.pem'), $leaf, [Text.Encoding]::ASCII)

$now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$profile = @{
    'version-name' = $VersionName
    'version-code' = [int]$VersionCode
    'uuid'         = 'fe686e1b-3770-4824-a938-961b140a7c98'
    'validity'     = @{ 'not-before' = $now - 86400; 'not-after' = $now + 31536000 }
    'type'         = 'debug'
    'bundle-info'  = @{
        'developer-id'           = 'OpenHarmony'
        'development-certificate' = $leaf
        'bundle-name'            = $BundleName
        'apl'                    = 'normal'
        'app-feature'            = 'hos_normal_app'
    }
    'acls'         = @{ 'allowed-acls' = @('') }
    'permissions'  = @{ 'restricted-permissions' = @('') }
    'debug-info'   = @{ 'device-ids' = @($Udid); 'device-id-type' = 'udid' }
    'issuer'       = 'pki_internal'
}
$profile | ConvertTo-Json -Depth 6 | Set-Content $ProfileJson -Encoding ascii

Write-Host "  签名 profile"
& $Java -jar $SignTool sign-profile -mode localSign -keyAlias $ProfileKeyAlias -keyPwd $Pass `
    -profileCertFile $ProfileCert -inFile $ProfileJson -signAlg SHA256withECDSA `
    -keystoreFile $SdkP12 -keystorePwd $Pass -outFile $ProfileP7b 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Err "sign-profile 失败" }

# ---------- 5. 签名 HAP ----------
Write-Step "签名 HAP"
& $Java -jar $SignTool sign-app -mode localSign -keyAlias $AppKeyAlias -keyPwd $Pass `
    -appCertFile $AppChain -profileFile $ProfileP7b -inFile $unsigned.FullName `
    -signAlg SHA256withECDSA -keystoreFile $AppP12 -keystorePwd $Pass `
    -outFile $SignedHap -signCode 1 -compatibleVersion 24 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $SignedHap)) { Write-Err "sign-app 失败" }

$uart = [math]::Round((Get-Item $unsigned.FullName).Length / 1KB, 1)
$sart = [math]::Round((Get-Item $SignedHap).Length / 1KB, 1)
Write-Host "  未签名 $uart KB → 已签名 $sart KB"

if ($NoInstall) {
    Write-Step "完成（-NoInstall：未安装）"
    Write-Host "  产物：$SignedHap"
    exit 0
}

# ---------- 6. 安装 ----------
Write-Step "安装到设备"
$targets = & $Hdc list targets 2>$null | Where-Object { $_ -and $_ -notmatch 'Empty' }
if (-not $targets) { Write-Err "没有可用设备，请启动模拟器或连接真机（hdc list targets）" }

if ($Uninstall) {
    Write-Host "  卸载 $BundleName"
    & $Hdc uninstall $BundleName 2>&1 | Out-Null
    $installArgs = @('install')
}
else { $installArgs = @('install', '-r') }

$installArgs += $SignedHap
$out = & $Hdc @installArgs 2>&1 | Out-String
Write-Host $out.Trim()

if ($out -match 'install bundle successfully') {
    Write-Host "安装成功：$BundleName ($VersionName)" -ForegroundColor Green
}
elseif ($out -match '9568332|sign info inconsistent') {
    Write-Err "签名信息与已安装版本不一致。首次切换签名材料请加 -Uninstall 重新安装：`n    powershell -ExecutionPolicy Bypass -File tools\build-install.ps1 -Uninstall"
}
else {
    Write-Err "安装失败，见上方 hdc 输出"
}

if ($Launch) {
    & $Hdc shell aa start -a EntryAbility -b $BundleName 2>&1 | Out-Null
    Write-Host "已启动 $BundleName"
}
