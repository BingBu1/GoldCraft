@echo off
setlocal DisableDelayedExpansion
set "PSModulePath="
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-Server.ps1" -Restore %*
set "patchExit=%errorlevel%"
pause
exit /b %patchExit%
