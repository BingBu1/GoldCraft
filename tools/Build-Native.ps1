param([ValidateSet('Release','Debug')][string]$Configuration='Release', [switch]$Server, [switch]$HeadlessFixture)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangCMakeArguments $compiler
if($HeadlessFixture -and -not $Server){throw 'The isolated headless fixture requires -Server.'}
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/native-clang-x86-$Configuration")
$output=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/native-x86/$Configuration")
$source=Get-MetaHookSourceRoot
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake434/cmake/data/bin/cmake.exe')
if(-not(Test-Path -LiteralPath $cmake)){& (Join-Path $PSScriptRoot 'Prepare-NativeBuild.ps1')}
$ctest=Join-Path (Split-Path $cmake) 'ctest.exe'
& $cmake -S $script:GoldCraftRoot -B $build -G Ninja @clangArgs "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$output" `
    "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=$output" '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' `
    "-DMETAHOOKSV_SOURCE_PATH=$source" "-DMETAHOOK_SOURCE_PATH=$source/MetaHook"
if($LASTEXITCODE){throw 'Native configure failed'}
& $cmake --build $build --config $Configuration --parallel 4
if($LASTEXITCODE){throw 'Native build failed'}
& $ctest --test-dir $build -C $Configuration --output-on-failure
if($LASTEXITCODE){throw 'Native tests failed'}
if($Server){
    $clangMSBuild=Get-GoldCraftClangMSBuildArguments $compiler
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $vs){throw 'MSVC installation not found'}
    $msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
    $serverBuild=if($HeadlessFixture){'regamedll-headless'}else{'regamedll'}
    $out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/$serverBuild/$Configuration")
    $targets=Join-Path $script:GoldCraftRoot 'native/server/GoldCraft.targets'
    & $msbuild (Join-Path $script:GoldCraftRoot 'external/ReGameDLL_CS/msvc/ReGameDLL.sln') /m:4 /nologo /v:minimal `
        "/p:Configuration=$Configuration" /p:Platform=Win32 @clangMSBuild `
        "/p:GoldCraftRoot=$script:GoldCraftRoot" "/p:ForceImportBeforeCppTargets=$targets" `
        "/p:GoldCraftHeadlessFixture=$($HeadlessFixture.IsPresent.ToString().ToLowerInvariant())" `
        "/p:OutDir=$out/" /p:PostBuildEventUseInBuild=false
    if($LASTEXITCODE){throw 'ReGameDLL build failed'}
}
