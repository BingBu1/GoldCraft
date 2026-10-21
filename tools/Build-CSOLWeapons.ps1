param([string]$Output='build/csol-runtime-20261021',[switch]$TestWeapons)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/SandboxPaths.ps1"
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$destination=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $Output)
$common=Join-Path $destination 'common'
$arguments=Get-GoldCraftClangCMakeArguments $compiler
& cmake -S $script:GoldCraftRoot -B $common -G Ninja "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" @arguments `
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$common"
if($LASTEXITCODE){throw 'CSOL shared library configure failed'}
& cmake --build $common --target goldcraft_common --parallel 4
if($LASTEXITCODE){throw 'CSOL shared library build failed'}
$clangArgs=Get-GoldCraftClangMSBuildArguments $compiler
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
& $msbuild "$script:GoldCraftRoot/external/ReGameDLL_CS/msvc/ReGameDLL.sln" /m:4 /nologo /v:minimal `
    /p:Configuration=Release /p:Platform=Win32 @clangArgs "/p:GoldCraftRoot=$script:GoldCraftRoot" `
    "/p:ForceImportBeforeCppTargets=$script:GoldCraftRoot/native/server/GoldCraft.targets" `
    "/p:GoldCraftCommonLibrary=$common/goldcraft_common.lib" `
    "/p:GoldCraftCsolWeaponTests=$($TestWeapons.IsPresent)" `
    "/p:OutDir=$destination/regamedll/" "/p:IntDir=$destination/regamedll-obj/" /p:PostBuildEventUseInBuild=false
if($LASTEXITCODE){throw 'CSOL ReGameDLL build failed'}
