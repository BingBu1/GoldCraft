param([switch]$TestSteamCallbacks, [switch]$TestMapDelivery, [switch]$TestPacketEntities,
      [string]$Output='', [ValidateRange(1,2)][int]$MaxJobs=2, [switch]$Fresh, [switch]$NoPublish)
if(([int]$TestSteamCallbacks.IsPresent + [int]$TestMapDelivery.IsPresent + [int]$TestPacketEntities.IsPresent) -gt 1){throw 'Select one isolated engine fixture.'}
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
. (Join-Path $PSScriptRoot 'Build-CandidateSupport.ps1')
$compiler=Initialize-GoldCraftCompiler
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC installation not found'}
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$configuration=if($TestSteamCallbacks){'Test Fixes'}else{'Release'}
$outputDirectory=if($TestSteamCallbacks){'build/rehlds/Tests'}elseif($TestMapDelivery){'build/rehlds/Delivery'}elseif($TestPacketEntities){'build/rehlds/Packets'}else{'build/rehlds/Release'}
if($Output){$outputDirectory=$Output}
$out=Initialize-GoldCraftBuildDirectory $outputDirectory $Fresh.IsPresent
$clangArgs=Get-GoldCraftCandidateMSBuildArguments $compiler $out $MaxJobs
$targets=Join-Path $script:GoldCraftRoot 'native/engine/GoldCraftEngine.targets'
$buildTargets=if($TestSteamCallbacks -or $TestMapDelivery -or $TestPacketEntities){'/t:ReHLDS'}else{'/t:ReHLDS,dedicated,filesystem_stdio'}
$fixtureArgs=if($TestMapDelivery){@('/p:GoldCraftMapDeliveryFixture=true')}elseif($TestPacketEntities){@('/p:GoldCraftPacketEntitiesFixture=true')}else{@()}
# clang-cl and lld keep the x86 MSVC ABI while performing O3/ThinLTO.
& $msbuild (Join-Path $script:GoldCraftRoot 'external/ReHLDS/msvc/ReHLDS.sln') "/m:$MaxJobs" /nologo /v:minimal `
    $buildTargets "/p:Configuration=$configuration" /p:Platform=Win32 @clangArgs @fixtureArgs `
    "/p:GoldCraftRoot=$script:GoldCraftRoot" "/p:GoldCraftOptimizedTests=$($TestSteamCallbacks.IsPresent)" "/p:ForceImportBeforeCppTargets=$targets" `
    "/p:OutDir=$out/" /fl "/flp:LogFile=$out/msbuild.log;Verbosity=normal;Encoding=UTF-8"
if($LASTEXITCODE){throw 'ReHLDS build failed'}
if($TestMapDelivery -or $TestPacketEntities){
    Write-Output "Built the selected isolated engine fixture in $out"
    return
}
if($TestSteamCallbacks){
    Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'external/ReHLDS/rehlds/lib/steam_api.dll') -Destination $out -Force
    & (Join-Path $out 'swds.exe') -runGroup GoldCraftSteam
    if($LASTEXITCODE){throw 'ReHLDS Steam callback regression failed'}
    return
}
if($NoPublish){Write-Output "Built candidate engine in $out; dist and runtimes untouched.";return}
$dist=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/rehlds')
New-Item -ItemType Directory -Path $dist -Force | Out-Null
foreach($file in @('swds.dll','swds.pdb','hlds.exe','hlds.pdb','filesystem_stdio.dll')) {
    Copy-Item -LiteralPath (Join-Path $out $file) -Destination (Join-Path $dist $file) -Force
}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'external/ReHLDS/rehlds/lib/steam_api.dll') -Destination (Join-Path $dist 'steam_api.dll') -Force
Write-Output "Built the x86 ReHLDS engine and dedicated launcher in $dist"
