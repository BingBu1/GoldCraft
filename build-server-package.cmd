@echo off
setlocal
cd /d "%~dp0"
if not exist "server-package.local.json" (
  echo Create server-package.local.json using tools\server-package-profile.example.json first.
  exit /b 1
)
python tools\Build-ServerPackage.py --profile server-package.local.json %*
exit /b %errorlevel%
