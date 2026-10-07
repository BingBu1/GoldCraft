param([switch]$TestFixtures)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/Half-Life')
$running=Get-CimInstance Win32_Process | Where-Object { $_.ExecutablePath -eq (Join-Path $game 'hlds.exe') }
if($running){throw 'Stop the verified sandbox ReHLDS process before changing its DLLs.'}
$runtime=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/amxx-1.9.0.5303/addons/amxmodx')
$metaSource=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/metamod-1.3.0.149/addons/metamod')
$reapi=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/reapi-5.29.0.358/addons/amxmodx')
if(-not(Test-Path -LiteralPath $runtime)){& (Join-Path $PSScriptRoot 'Prepare-AMXX.ps1')}
$amxx=Assert-SandboxPath (Join-Path $game 'cstrike/addons/amxmodx')
$meta=Assert-SandboxPath (Join-Path $game 'cstrike/addons/metamod')
if(-not(Test-Path -LiteralPath $amxx)){
    New-Item -ItemType Directory -Path (Split-Path $amxx) -Force | Out-Null
    Copy-Item -LiteralPath $runtime -Destination $amxx -Recurse
    $plugins=Assert-SandboxPath (Join-Path $amxx 'configs/plugins.ini')
    Copy-Item -LiteralPath $plugins -Destination (Join-Path $amxx 'configs/plugins.stock.ini')
    '; GoldCraft development server. Original defaults: plugins.stock.ini' | Set-Content -LiteralPath $plugins -Encoding ascii
}
if(-not(Test-Path -LiteralPath $meta)){Copy-Item -LiteralPath $metaSource -Destination $meta -Recurse}
$liblist=Assert-SandboxPath (Join-Path $game 'cstrike/liblist.gam')
$backup=Assert-SandboxPath ($liblist+'.before-amxx')
if(-not(Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $liblist -Destination $backup}
$liblistText=Get-Content -LiteralPath $liblist -Raw
if($liblistText -notmatch '(?m)^\s*gamedll\s+"[^"]*"'){throw 'Missing Windows gamedll entry'}
$liblistText=[regex]::Replace($liblistText,'(?m)^\s*gamedll\s+"[^"]*"','gamedll "addons/metamod/metamod.dll"')
Set-Content -LiteralPath $liblist -Value $liblistText -Encoding ascii -NoNewline
function Add-ConfigLine([string]$Path,[string]$Line){
    $destination=Assert-SandboxPath $Path
    $lines=if(Test-Path -LiteralPath $destination){@(Get-Content -LiteralPath $destination)}else{@()}
    if($Line -notin $lines){Add-Content -LiteralPath $destination -Value $Line -Encoding ascii}
}
Add-ConfigLine (Join-Path $meta 'config.ini') 'gamedll dlls/mp.dll'
Add-ConfigLine (Join-Path $meta 'plugins.ini') 'win32 addons/amxmodx/dlls/amxmodx_mm.dll'
Add-ConfigLine (Join-Path $amxx 'configs/modules.ini') 'goldcraft'
Add-ConfigLine (Join-Path $amxx 'configs/modules.ini') 'reapi'
Copy-Item -LiteralPath (Join-Path $reapi 'modules/reapi_amxx.dll') -Destination (Join-Path $amxx 'modules/reapi_amxx.dll') -Force
foreach($include in Get-ChildItem -LiteralPath (Join-Path $reapi 'scripting/include') -File){
    Copy-Item -LiteralPath $include.FullName -Destination (Assert-SandboxPath (Join-Path $amxx "scripting/include/$($include.Name)")) -Force
}
Copy-Item -LiteralPath (Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/native-x86/Release/goldcraft_amxx.dll')) -Destination (Join-Path $amxx 'modules/goldcraft_amxx.dll') -Force
$pluginNames=@('goldcraft');if($TestFixtures){$pluginNames+='goldcraft_test'}
foreach($name in $pluginNames){
    Copy-Item -LiteralPath (Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "dist/amxx/$name.amxx")) -Destination (Join-Path $amxx "plugins/$name.amxx") -Force
    Copy-Item -LiteralPath (Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "amxx/$name.sma")) -Destination (Join-Path $amxx "scripting/$name.sma") -Force
    Add-ConfigLine (Join-Path $amxx 'configs/plugins.ini') "$name.amxx"
}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'amxx/include/goldcraft.inc') -Destination (Join-Path $amxx 'scripting/include/goldcraft.inc') -Force
$files=@('cstrike/liblist.gam','cstrike/addons/metamod/metamod.dll','cstrike/addons/amxmodx/dlls/amxmodx_mm.dll','cstrike/addons/amxmodx/modules/goldcraft_amxx.dll','cstrike/addons/amxmodx/modules/reapi_amxx.dll')
$files+=@($pluginNames | ForEach-Object {"cstrike/addons/amxmodx/plugins/$_.amxx"})
$evidence=@($files | ForEach-Object { $path=Assert-SandboxPath (Join-Path $game $_); @{name=$_;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash} })
@{amxx='1.9.0.5303';metamod='1.3.0.149';reapi='5.29.0.358';files=$evidence;fixtures=[bool]$TestFixtures} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/amxx-deployment.json')) -Encoding utf8
Write-Output 'Sandbox chain installed: ReHLDS -> Metamod-R -> ReGameDLL + AMXX + ReAPI + GoldCraft natives.'
