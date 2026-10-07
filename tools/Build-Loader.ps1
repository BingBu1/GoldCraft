param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC installation not found'}
Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -winsdk=10.0.26100.0' | Out-Null
$env:VSLANG='1033'
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe')
if(-not(Test-Path -LiteralPath $cmake)){throw 'Install workspace cmake==3.31.10 first (pinned Capstone uses CMake 3 policies).'}
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$source=Get-MetaHookSourceRoot
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/metahook-20261007-ninja-$Configuration-sdk26100")
$prefix=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/metahook')
& $cmake -S (Join-Path $source 'MetaHook') -B $build -G Ninja `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja" '-DMETAHOOK_BUILD_SDL=OFF' "-DCMAKE_INSTALL_PREFIX=$prefix"
if($LASTEXITCODE){throw 'Loader configure failed'}
$log=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/logs/metahook-build.log')
New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null
& $cmake --build $build --parallel 4 *> $log
if($LASTEXITCODE){
    Get-Content -LiteralPath $log | Select-String -Pattern 'FAILED:|error |fatal error|LNK[0-9]+' | Select-Object -First 16
    throw "Loader build failed; full log: $log"
}
Get-Content -LiteralPath $log -Tail 6
& $cmake --install $build
if($LASTEXITCODE){throw 'Loader staging failed'}
