# Tftpd32 真机测试辅助：自动重连 hdc 并截屏取证
# 用法：
#   powershell -ExecutionPolicy Bypass -File tools\snapshot.ps1                 # 默认：时间戳命名，存 $env:TEMP
#   powershell -ExecutionPolicy Bypass -File tools\snapshot.ps1 -Name ui07      # 指定文件名前缀
#   powershell -ExecutionPolicy Bypass -File tools\snapshot.ps1 -OutDir D:\tmp  # 指定输出目录
param(
    [string]$Name = '',                         # 文件名前缀，留空用 snap
    [string]$OutDir = "$env:TEMP",              # 输出目录
    [string]$Device = '192.168.10.128:42217'    # 无线调试地址（重开无线调试/换网后 IP 和端口都会变，需同步更新）
)

$hdc = 'D:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe'
$remote = '/data/local/tmp/snap.jpeg'

# 1. 重连（kill/start/tconn；重复 tconn 报"已存在"不影响使用）
& $hdc kill | Out-Null
& $hdc start | Out-Null
Start-Sleep 2
& $hdc tconn $Device

# 2. 截图到设备
& $hdc shell snapshot_display -f $remote

# 3. 拉取到电脑（带时间戳防覆盖）
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$prefix = if ($Name) { $Name } else { 'snap' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$local = Join-Path $OutDir "${prefix}_${stamp}.jpeg"
& $hdc file recv $remote $local

# 4. 校验并打开（以本地文件存在为最终成功判据）
if (-not (Test-Path $local)) {
    Write-Error "截图失败：检查设备连接（tconn 输出）与屏幕状态后重试"
    exit 1
}
$kb = [math]::Round((Get-Item $local).Length / 1KB)
Write-Host "已保存: $local ($kb KB)" -ForegroundColor Green
Invoke-Item $local
