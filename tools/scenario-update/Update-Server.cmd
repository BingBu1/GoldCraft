@echo off
setlocal DisableDelayedExpansion
set "PSModulePath="
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-Server.ps1" %*
set "result=%errorlevel%"
if not "%result%"=="0" echo Update failed. Read the message above.
pause
exit /b %result%
