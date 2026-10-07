param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$source=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'external/SyPB/Project SyPB')
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC installation not found'}
$msbuild=Join-Path $vs 'MSBuild/Current/Bin/MSBuild.exe'
foreach($project in @(@{Name='bot';Path='SyPB_BOT/SyPB Bot.vcxproj'},@{Name='api';Path='SyPB_API/SyPB API.vcxproj'})){
    $out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/sypb/$Configuration")
    $intermediate=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/sypb/obj/$Configuration/$($project.Name)")
    New-Item -ItemType Directory -Path $out,$intermediate -Force | Out-Null
    & $msbuild (Join-Path $source $project.Path) /m:4 /nologo /v:minimal `
        "/p:Configuration=$Configuration" /p:Platform=Win32 /p:PlatformToolset=v145 `
        "/p:OutDir=$out/" "/p:IntDir=$intermediate/" /p:PostBuildEventUseInBuild=false
    if($LASTEXITCODE){throw "SyPB $($project.Name) build failed"}
}
