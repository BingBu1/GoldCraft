param([string[]]$Plugins=@('goldcraft'),[string[]]$Includes=@())
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$compiler=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/amxx-1.9.0.5303/addons/amxmodx/scripting/amxxpc.exe')
if(-not(Test-Path -LiteralPath $compiler)){& (Join-Path $PSScriptRoot 'Prepare-AMXX.ps1')}
$out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/amxx')
New-Item -ItemType Directory -Path $out -Force | Out-Null
$extraIncludes=@($Includes | ForEach-Object { '-i'+(Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $_)) })
foreach($plugin in $Plugins){
    if($plugin -notmatch '^[a-z0-9_]+$'){throw 'Invalid plugin filename'}
    $source=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "amxx/$plugin.sma")
    & $compiler $source ("-i"+(Join-Path $script:GoldCraftRoot 'amxx/include')) ("-i"+(Join-Path $script:GoldCraftRoot '.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include')) ("-i"+(Join-Path (Split-Path $compiler) 'include')) @extraIncludes ("-o"+(Join-Path $out "$plugin.amxx"))
    if($LASTEXITCODE){throw "Pawn compilation failed: $plugin"}
}
