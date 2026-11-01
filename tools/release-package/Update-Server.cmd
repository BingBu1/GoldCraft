@echo off
setlocal DisableDelayedExpansion
set "PSModulePath="
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-Server.ps1" %*
set "patchExit=%errorlevel%"
if not "%patchExit%"=="0" echo Update FAILED. Read the error above. Do not start with partially copied files.
pause
exit /b %patchExit%
