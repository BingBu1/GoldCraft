param()
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC installation not found'}
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
$out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/rehlds/Release')
& $msbuild (Join-Path $script:GoldCraftRoot 'external/ReHLDS/msvc/ReHLDS.sln') /m:4 /nologo /v:minimal `
    '/t:ReHLDS,dedicated,filesystem_stdio' /p:Configuration=Release /p:Platform=Win32 /p:PlatformToolset=v145 `
    "/p:OutDir=$out/" /p:PostBuildEventUseInBuild=false
if($LASTEXITCODE){throw 'ReHLDS build failed'}
$dist=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/rehlds')
New-Item -ItemType Directory -Path $dist -Force | Out-Null
foreach($file in @('swds.dll','swds.pdb','hlds.exe','hlds.pdb','filesystem_stdio.dll')) {
    Copy-Item -LiteralPath (Join-Path $out $file) -Destination (Join-Path $dist $file) -Force
}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'external/ReHLDS/rehlds/lib/steam_api.dll') -Destination (Join-Path $dist 'steam_api.dll') -Force
Write-Output "Built the x86 ReHLDS engine and dedicated launcher in $dist"
