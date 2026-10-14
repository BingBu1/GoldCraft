@echo off
setlocal DisableDelayedExpansion
rem Avoid inheriting a PowerShell 7 module path into Windows PowerShell 5.1.
set "PSModulePath="
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-GoldCraft.ps1" %*
set "goldcraftResult=%errorlevel%"
if not "%goldcraftResult%"=="0" echo Installation did not finish. See the error above.
pause
exit /b %goldcraftResult%
