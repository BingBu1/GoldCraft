@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update-Launcher.ps1" %*
set "result=%ERRORLEVEL%"
pause
exit /b %result%
