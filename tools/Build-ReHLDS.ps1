param([switch]$TestSteamCallbacks)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangMSBuildArguments $compiler
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC installation not found'}
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$configuration=if($TestSteamCallbacks){'Test Fixes'}else{'Release'}
$outputDirectory=if($TestSteamCallbacks){'build/rehlds/Tests'}else{'build/rehlds/Release'}
$out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $outputDirectory)
$targets=Join-Path $script:GoldCraftRoot 'native/engine/GoldCraftEngine.targets'
$buildTargets=if($TestSteamCallbacks){'/t:ReHLDS'}else{'/t:ReHLDS,dedicated,filesystem_stdio'}
# clang-cl and lld keep the x86 MSVC ABI while performing O3/ThinLTO.
& $msbuild (Join-Path $script:GoldCraftRoot 'external/ReHLDS/msvc/ReHLDS.sln') /m:4 /nologo /v:minimal `
    $buildTargets "/p:Configuration=$configuration" /p:Platform=Win32 @clangArgs `
    "/p:GoldCraftRoot=$script:GoldCraftRoot" "/p:GoldCraftOptimizedTests=$($TestSteamCallbacks.IsPresent)" "/p:ForceImportBeforeCppTargets=$targets" `
    "/p:OutDir=$out/" /p:PostBuildEventUseInBuild=false
if($LASTEXITCODE){throw 'ReHLDS build failed'}
if($TestSteamCallbacks){
    Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'external/ReHLDS/rehlds/lib/steam_api.dll') -Destination $out -Force
    & (Join-Path $out 'swds.exe') -runGroup GoldCraftSteam
    if($LASTEXITCODE){throw 'ReHLDS Steam callback regression failed'}
    return
}
$dist=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/rehlds')
New-Item -ItemType Directory -Path $dist -Force | Out-Null
foreach($file in @('swds.dll','swds.pdb','hlds.exe','hlds.pdb','filesystem_stdio.dll')) {
    Copy-Item -LiteralPath (Join-Path $out $file) -Destination (Join-Path $dist $file) -Force
}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'external/ReHLDS/rehlds/lib/steam_api.dll') -Destination (Join-Path $dist 'steam_api.dll') -Force
Write-Output "Built the x86 ReHLDS engine and dedicated launcher in $dist"
