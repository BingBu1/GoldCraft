@echo off
pwsh -NoProfile -File "%~dp0tools\Build-AMXX.ps1" %*
exit /b %errorlevel%
