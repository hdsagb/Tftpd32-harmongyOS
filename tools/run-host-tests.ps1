# run-host-tests.ps1 —— 构建并运行 C 代码宿主（PC）单元测试
#
# 用法（项目根目录执行，无需真机/模拟器）：
#   powershell -ExecutionPolicy Bypass -File tools\run-host-tests.ps1
#   powershell -ExecutionPolicy Bypass -File tools\run-host-tests.ps1 -Config Debug
#   powershell -ExecutionPolicy Bypass -File tools\run-host-tests.ps1 -Clean
#
# 依赖：Visual Studio 2022（含 C++ 工作负载）+ 其自带的 CMake；
# 不参与 HAP 构建，只编译 tftpd 下的 bootpd_util.c、ip_pal.c、settings_pal.c。
param(
    [ValidateSet('Release', 'Debug')]
    [string] $Config = 'Release',
    [switch] $Clean
)

$ErrorActionPreference = 'Continue'

$Root     = Split-Path -Parent $PSScriptRoot
$SourceDir = Join-Path $Root 'entry\src\main\cpp\tests\host'
$BuildDir  = Join-Path $Root 'build\host-tests'
$Exe       = Join-Path $BuildDir "$Config\tftpd32_host_tests.exe"

function Write-Step([string] $msg) { Write-Host "==== $msg ====" -ForegroundColor Cyan }
function Write-Err([string] $msg)  { Write-Host "错误：$msg" -ForegroundColor Red; exit 1 }

function Find-CMake {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * `
              -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
              -property installationPath
        if ($vs) {
            $c = Join-Path $vs.Trim() 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $c) { return $c }
        }
    }
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    Write-Err '未找到 CMake，请安装 Visual Studio 2022 的“使用 C++ 的桌面开发”工作负载'
}

$CMake = Find-CMake

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "清理 $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path (Join-Path $BuildDir 'CMakeCache.txt'))) {
    Write-Step '配置宿主测试工程'
    & $CMake -S $SourceDir -B $BuildDir -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { Write-Err 'CMake 配置失败' }
}

Write-Step "构建宿主测试（$Config）"
& $CMake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { Write-Err '编译失败' }

Write-Step '运行宿主单元测试'
& $Exe
$rc = $LASTEXITCODE
if ($rc -eq 0) { Write-Host '全部用例通过' -ForegroundColor Green }
else           { Write-Host "存在失败用例（exit=$rc）" -ForegroundColor Red }
exit $rc
