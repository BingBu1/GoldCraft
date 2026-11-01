param([Parameter(Mandatory)][string]$Assets,[Parameter(Mandatory)][string]$Output,[string]$Python='python')
$ErrorActionPreference='Stop'
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$Assets=[IO.Path]::GetFullPath($Assets)
$Output=[IO.Path]::GetFullPath($Output)
foreach($p in @($Assets,$Output)) {
    if(![IO.Path]::GetFullPath($p).StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Build paths must be inside the workspace' }
}
$env:DOTNET_CLI_HOME=Join-Path $workspace '.tools/dotnet-home'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$env:NUGET_PACKAGES=Join-Path $workspace '.tools/nuget-packages'
$obj=Join-Path $workspace 'build/client-launcher-20261101/obj/'
$icon=Join-Path $Output 'icon/launcher.ico'
& $Python (Join-Path $PSScriptRoot 'icon.py') --output $icon
if($LASTEXITCODE -ne 0){throw 'Launcher icon generation failed'}
dotnet publish (Join-Path $PSScriptRoot 'GoldCraftLauncher.csproj') -c Release -r win-x64 --self-contained true -m:1 -o $Output "-p:LauncherAssets=$Assets" "-p:LauncherIcon=$icon" "-p:BaseIntermediateOutputPath=$obj" "-p:MSBuildProjectExtensionsPath=$obj" "-p:BaseOutputPath=$workspace/build/client-launcher-20261101/bin/" -p:UseSharedCompilation=false
if($LASTEXITCODE -ne 0){throw 'Launcher build failed'}
