# A second, disposable server pair for native combat checks without desktop input.
param([switch]$PlayerFixtures)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$testRoot=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/headless-combat')
$game=Assert-SandboxPath (Join-Path $testRoot 'Half-Life')
$mc=Assert-SandboxPath (Join-Path $testRoot 'minecraft')
$existing=Get-CimInstance Win32_Process | Where-Object {
    ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($testRoot+'\',[StringComparison]::OrdinalIgnoreCase)) -or
    ($_.Name -eq 'java.exe' -and $_.CommandLine -and $_.CommandLine.Contains((Join-Path $testRoot 'server.args')))
}
if($existing){throw 'The headless fixture is running; preserve it and stop its exact owned processes first.'}
foreach($dir in @($game,$mc,(Join-Path $testRoot 'logs'))){New-Item -ItemType Directory -Path $dir -Force | Out-Null}
$source=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/Half-Life')
function Copy-TestFile([string]$From,[string]$To){
    $existingPath=[IO.Path]::GetFullPath($To)
    if(-not $existingPath.StartsWith($testRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Test asset destination outside fixture'}
    # Retained immutable assets need no write or parent-directory recreation.
    # Validate source/destination ancestors again whenever a copy is necessary.
    if(Test-Path -LiteralPath $existingPath -PathType Leaf){return}
    $fromPath=Assert-WorkspacePath $From
    $toPath=Assert-SandboxPath $existingPath
    New-Item -ItemType Directory -Path (Split-Path $toPath) -Force | Out-Null
    Copy-Item -LiteralPath $fromPath -Destination $toPath
}
foreach($name in @('hlds.exe','swds.dll','filesystem_stdio.dll','steam_api.dll')){
    Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot "dist/rehlds/$name") -Destination (Assert-SandboxPath (Join-Path $game $name)) -Force
}
foreach($name in @('steam_appid.txt','tier0.dll','vstdlib.dll','vgui.dll','vgui2.dll','SDL2.dll')){
    Copy-TestFile (Join-Path $source $name) (Join-Path $game $name)
}
foreach($mod in @('valve','cstrike')){
    foreach($directory in @('models','sound','sprites','events','gfx')){
        $assetRoot=Join-Path $source "$mod/$directory"
        if(-not(Test-Path -LiteralPath $assetRoot)){continue}
        foreach($file in Get-ChildItem -LiteralPath $assetRoot -File -Recurse){
            Copy-TestFile $file.FullName (Join-Path $game $file.FullName.Substring($source.Length+1))
        }
    }
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $source $mod) -File | Where-Object {$_.Extension -eq '.wad' -or $_.Name -in @('liblist.gam','titles.txt','skill.cfg','delta.lst')}){
        Copy-TestFile $file.FullName (Join-Path $game "$mod/$($file.Name)")
    }
}
Copy-TestFile (Join-Path $source 'cstrike/maps/cs_assault.bsp') (Join-Path $game 'cstrike/maps/cs_assault.bsp')
New-Item -ItemType Directory -Path (Join-Path $game 'cstrike/dlls') -Force | Out-Null
$gameDll=if($PlayerFixtures){'build/regamedll-headless/Release/mp.dll'}else{'build/regamedll/Release/mp.dll'}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot $gameDll) -Destination (Join-Path $game 'cstrike/dlls/mp.dll') -Force
$amxx=Assert-SandboxPath (Join-Path $game 'cstrike/addons/amxmodx')
$meta=Assert-SandboxPath (Join-Path $game 'cstrike/addons/metamod')
if(-not(Test-Path -LiteralPath $amxx)){
    New-Item -ItemType Directory -Path (Split-Path $amxx) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot '.tools/amxx-1.9.0.5303/addons/amxmodx') -Destination $amxx -Recurse
}
if(-not(Test-Path -LiteralPath $meta)){
    Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot '.tools/metamod-1.3.0.149/addons/metamod') -Destination $meta -Recurse
}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'build/native-x86/Release/goldcraft_amxx.dll') -Destination (Join-Path $amxx 'modules/goldcraft_amxx.dll') -Force
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot '.tools/reapi-5.29.0.358/addons/amxmodx/modules/reapi_amxx.dll') -Destination (Join-Path $amxx 'modules/reapi_amxx.dll') -Force
$plugins=@('goldcraft','goldcraft_headless_test')
if($PlayerFixtures){$plugins+='goldcraft_test'}
foreach($plugin in $plugins){
    Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot "dist/amxx/$plugin.amxx") -Destination (Join-Path $amxx "plugins/$plugin.amxx") -Force
}
@('goldcraft','reapi','engine','fakemeta') | Set-Content -LiteralPath (Join-Path $amxx 'configs/modules.ini') -Encoding ascii
@($plugins | ForEach-Object {"$_.amxx"}) | Set-Content -LiteralPath (Join-Path $amxx 'configs/plugins.ini') -Encoding ascii
'gamedll dlls/mp.dll' | Set-Content -LiteralPath (Join-Path $meta 'config.ini') -Encoding ascii
'win32 addons/amxmodx/dlls/amxmodx_mm.dll' | Set-Content -LiteralPath (Join-Path $meta 'plugins.ini') -Encoding ascii
$liblist=Join-Path $game 'cstrike/liblist.gam'
[regex]::Replace((Get-Content -LiteralPath $liblist -Raw),'(?m)^\s*gamedll\s+"[^"]*"','gamedll "addons/metamod/metamod.dll"') | Set-Content -LiteralPath $liblist -Encoding ascii
function TestPort([bool]$Udp){
    if($Udp){$socket=[Net.Sockets.UdpClient]::new(0);try{return $socket.Client.LocalEndPoint.Port}finally{$socket.Dispose()}}
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
    try{$listener.Start();return $listener.LocalEndpoint.Port}finally{$listener.Stop()}
}
$ports=[Collections.Generic.HashSet[int]]::new()
function UniquePort([bool]$Udp){do{$port=TestPort $Udp}while(-not $ports.Add($port));return $port}
$cluster=@{csPort=(UniquePort $true);serverPort=(UniquePort $false);minecraftPort=(UniquePort $false);minecraftRconPort=(UniquePort $false)
    serverSession=[Guid]::NewGuid().ToString('N');serverToken=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(16)).ToLowerInvariant()
    csRconToken=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32));rconToken=[Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32))}
$cluster | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $testRoot 'cluster.json') -Encoding utf8
@('hostname "GoldCraft headless combat fixture"','sv_lan 1','mp_freezetime 0','mp_roundtime 9','mp_round_infinite 1','mp_timelimit 0',
  'mp_autoteambalance 0','mp_limitteams 0','mp_autokick 0','mp_respawn_immunitytime 0','goldcraft_default_form 0','goldcraft_headless_test 1','log off',
  ('rcon_password "'+$cluster.csRconToken+'"')) | Set-Content -LiteralPath (Join-Path $game 'cstrike/server.cfg') -Encoding ascii
'// Independent test configuration.' | Set-Content -LiteralPath (Join-Path $game 'cstrike/autoexec.cfg') -Encoding ascii
# Host_Init loads valve.rc; stuffcmds is what actually executes +map/+exec.
@('ip 127.0.0.1','stuffcmds') | Set-Content -LiteralPath (Join-Path $game 'cstrike/valve.rc') -Encoding ascii
'eula=true' | Set-Content -LiteralPath (Join-Path $mc 'eula.txt') -Encoding ascii
$settings='{"biome":"minecraft:plains","features":false,"lakes":false,"layers":[{"height":1,"block":"minecraft:air"}],"structure_overrides":[]}'
@('server-ip=127.0.0.1',('server-port='+$cluster.minecraftPort),'online-mode=true','enable-rcon=true',('rcon.port='+$cluster.minecraftRconPort),
  ('rcon.password='+$cluster.rconToken),'broadcast-rcon-to-ops=false','level-name=world','level-type=minecraft\:flat',('generator-settings='+$settings),
  'generate-structures=false','spawn-protection=0','difficulty=normal','view-distance=5','simulation-distance=5','max-players=4','pause-when-empty-seconds=-1') |
    Set-Content -LiteralPath (Join-Path $mc 'server.properties') -Encoding utf8
$pack=Assert-SandboxPath (Join-Path $mc 'world/datapacks/goldcraft_headless')
$dimensions=Join-Path $pack 'data/goldcraft/dimension'
New-Item -ItemType Directory -Path $dimensions -Force | Out-Null
'{"pack":{"pack_format":48,"description":"Independent ReHLDS combat test world"}}' | Set-Content -LiteralPath (Join-Path $pack 'pack.mcmeta') -Encoding utf8
@{type='minecraft:overworld';generator=@{type='minecraft:flat';settings=($settings | ConvertFrom-Json)}} | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $dimensions 'cs_assault_f6725c06.json') -Encoding utf8
$artifacts=@('hlds.exe','swds.dll','cstrike/dlls/mp.dll','cstrike/addons/amxmodx/modules/reapi_amxx.dll','cstrike/addons/amxmodx/plugins/goldcraft_headless_test.amxx') | ForEach-Object {
    @{name=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $game $_) -Algorithm SHA256).Hash}
}
@{path=$testRoot;files=$artifacts;playerFixtures=$PlayerFixtures.IsPresent;updatedUtc=[DateTime]::UtcNow.ToString('O')} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $testRoot 'deployment.json') -Encoding utf8
Write-Output "Independent headless server pair prepared at $testRoot. No live A/B settings changed."
