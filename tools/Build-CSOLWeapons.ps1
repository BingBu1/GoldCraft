param([string]$Output='build/csol-runtime-20261021',[switch]$TestWeapons,
      [ValidateRange(1,2)][int]$MaxJobs=2,[switch]$Fresh,[switch]$BuildAmxxModule)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/SandboxPaths.ps1"
. "$PSScriptRoot/ClangToolchain.ps1"
. "$PSScriptRoot/Build-CandidateSupport.ps1"
$compiler=Initialize-GoldCraftCompiler
$destination=Initialize-GoldCraftBuildDirectory $Output $Fresh.IsPresent
$common=Join-Path $destination 'common'
$arguments=Get-GoldCraftClangCMakeArguments $compiler
$arguments+="-DCMAKE_SHARED_LINKER_FLAGS=/machine:X86 /threads:$MaxJobs"
& cmake -S $script:GoldCraftRoot -B $common -G Ninja "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" @arguments `
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$common" "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$common"
if($LASTEXITCODE){throw 'CSOL shared library configure failed'}
$commonTargets=@('goldcraft_common')
if($BuildAmxxModule){$commonTargets+='goldcraft_amxx'}
& cmake --build $common --target @commonTargets --parallel $MaxJobs
if($LASTEXITCODE){throw 'CSOL shared library build failed'}
$clangArgs=Get-GoldCraftCandidateMSBuildArguments $compiler $destination $MaxJobs
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
& $msbuild "$script:GoldCraftRoot/external/ReGameDLL_CS/msvc/ReGameDLL.sln" "/m:$MaxJobs" /nologo /v:minimal `
    /p:Configuration=Release /p:Platform=Win32 @clangArgs "/p:GoldCraftRoot=$script:GoldCraftRoot" `
    "/p:ForceImportBeforeCppTargets=$script:GoldCraftRoot/native/server/GoldCraft.targets" `
    "/p:GoldCraftCommonLibrary=$common/goldcraft_common.lib" `
    "/p:GoldCraftCsolWeaponTests=$($TestWeapons.IsPresent)" `
    "/p:OutDir=$destination/regamedll/" /fl "/flp:LogFile=$destination/msbuild.log;Verbosity=normal;Encoding=UTF-8"
if($LASTEXITCODE){throw 'CSOL ReGameDLL build failed'}
