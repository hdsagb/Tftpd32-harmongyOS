@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
title Hyper-V 智能开关

:: ============================================================
::  Hyper-V 智能开关
::  重复运行即反向操作：检测到开着就关，检测到关着就开。
::
::  用法：双击运行（脚本会自动请求管理员权限）
::  参数： /dryrun   只检测并预演将要做什么，不修改任何设置
::         /reboot   执行完直接重启（默认改为询问）
::         /nopause  结束后不等按键，便于被其它脚本调用
::  日志： %TEMP%\hyperv-toggle.log
:: ============================================================

set "MODE=切换"
set "NOPAUSE=0"
set "AUTOREBOOT=0"
for %%p in (%*) do (
  if /i "%%~p"=="/dryrun"  set "MODE=预演"
  if /i "%%~p"=="/reboot"  set "AUTOREBOOT=1"
  if /i "%%~p"=="/nopause" set "NOPAUSE=1"
)

:: ---------------- 自动提权 ----------------
net session >nul 2>&1
if errorlevel 1 (
  echo.
  echo [提示] 需要管理员权限，正在请求提权，请在 UAC 弹窗点"是"。
  powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0' -ArgumentList '%*'"
  exit /b
)

set "LOG=%TEMP%\hyperv-toggle.log"

:: ---------------- 第一步：检测当前状态 ----------------
(
  echo ==================================================
  echo   Hyper-V 智能开关            模式: !MODE!
  echo   %DATE% %TIME%
  echo ==================================================
  echo.
  echo ---- [1/3] 检测当前状态 ----

  set "HVL="
  for /f "tokens=2" %%a in ('bcdedit /enum {current} ^| findstr /i "hypervisorlaunchtype"') do set "HVL=%%a"

  set "FSTATE="
  for /f "usebackq delims=" %%a in (`powershell -NoProfile -Command "(Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform).State"`) do set "FSTATE=%%a"

  set "HVP="
  for /f "usebackq delims=" %%a in (`powershell -NoProfile -Command "(Get-CimInstance Win32_ComputerSystem).HypervisorPresent"`) do set "HVP=%%a"

  echo   hypervisorlaunchtype = [!HVL!]       Off 表示已关，Auto 表示已开
  echo   HypervisorPlatform   = [!FSTATE!]
  echo   HypervisorPresent    = [!HVP!]       True 表示虚拟机监控程序已随本次开机启动

  :: 判定规则：启动类型为 Auto 且 WHPX 功能已启用，才算真正开启
  set "CUR=OFF"
  if /i "!HVL!"=="Auto" if /i "!FSTATE!"=="Enabled" set "CUR=ON"

  echo.
  if "!CUR!"=="ON"  echo   当前判定: Hyper-V 处于【已开启】状态
  if "!CUR!"=="OFF" echo   当前判定: Hyper-V 处于【已关闭】状态
  echo.
) > "%LOG%" 2>&1
type "%LOG%"

:: ---------------- 预演模式：到此为止 ----------------
if /i "!MODE!"=="预演" (
  if "!CUR!"=="ON"  echo [预演] 正式运行时将执行：关闭 Hyper-V
  if "!CUR!"=="OFF" echo [预演] 正式运行时将执行：开启 Hyper-V
  echo [预演] 未修改任何设置。日志: %LOG%
  if "!NOPAUSE!"=="0" pause
  exit /b 0
)

if "!CUR!"=="ON"  goto :DO_OFF
if "!CUR!"=="OFF" goto :DO_ON

:: ---------------- 第二步 A：关闭 Hyper-V ----------------
:DO_OFF
echo.
echo ---- [2/3] 关闭 Hyper-V ----
echo   正在执行，请稍候，命令行可能 1-3 分钟没有输出，属正常现象。
echo ---- [2/3] 关闭 Hyper-V ---- >>"%LOG%"
bcdedit /set hypervisorlaunchtype off >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] bcdedit hypervisorlaunchtype=off) else (echo   [完成] bcdedit hypervisorlaunchtype=off)
dism /online /disable-feature /featurename:HypervisorPlatform /norestart >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] 禁用 HypervisorPlatform) else (echo   [完成] 禁用 HypervisorPlatform)
dism /online /disable-feature /featurename:VirtualMachinePlatform /norestart >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] 禁用 VirtualMachinePlatform) else (echo   [完成] 禁用 VirtualMachinePlatform)
dism /online /disable-feature /featurename:Microsoft-Hyper-V-All /norestart >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] 禁用 Microsoft-Hyper-V-All) else (echo   [完成] 禁用 Microsoft-Hyper-V-All)
set "ACTION=已关闭 Hyper-V"
goto :AFTER

:: ---------------- 第二步 B：开启 Hyper-V ----------------
:DO_ON
echo.
echo ---- [2/3] 开启 Hyper-V ----
echo   正在执行，请稍候，命令行可能 1-3 分钟没有输出，属正常现象。
echo ---- [2/3] 开启 Hyper-V ---- >>"%LOG%"
bcdedit /set hypervisorlaunchtype auto >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] bcdedit hypervisorlaunchtype=auto) else (echo   [完成] bcdedit hypervisorlaunchtype=auto)
dism /online /enable-feature /featurename:HypervisorPlatform /all /norestart >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] 启用 HypervisorPlatform) else (echo   [完成] 启用 HypervisorPlatform)
dism /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart >>"%LOG%" 2>&1
if errorlevel 1 (echo   [失败] 启用 VirtualMachinePlatform) else (echo   [完成] 启用 VirtualMachinePlatform)
set "ACTION=已开启 Hyper-V"
goto :AFTER

:: ---------------- 第三步：复核 + 重启 ----------------
:AFTER
set "HVL2="
for /f "tokens=2" %%a in ('bcdedit /enum {current} ^| findstr /i "hypervisorlaunchtype"') do set "HVL2=%%a"
echo.
echo ---- [3/3] 复核 ----
echo   操作结果: !ACTION!
echo   复核 hypervisorlaunchtype = [!HVL2!]
echo   需要重启电脑后生效。
echo.
echo >>> !ACTION!，请重启电脑后生效。日志: %LOG%
(
  echo.
  echo ---- [3/3] 复核 ----
  echo 操作结果: !ACTION!
  echo 复核 hypervisorlaunchtype = [!HVL2!]
  echo 需要重启电脑后生效。
) >>"%LOG%"

if "!AUTOREBOOT!"=="1" goto :REBOOT_NOW
choice /c YN /n /m "是否立即重启以生效？按 Y 立即重启，按 N 稍后手动重启: "
if errorlevel 2 goto :NO_REBOOT

:REBOOT_NOW
echo 将在 60 秒后重启电脑，如需取消请执行: shutdown /a
shutdown /r /t 60 /c "重启以应用 Hyper-V 开关"
goto :END

:NO_REBOOT
echo 已取消重启，请稍后自行重启电脑使其生效。

:END
if "!NOPAUSE!"=="0" pause
exit /b 0
