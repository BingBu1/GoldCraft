param([ValidatePattern('^[a-z0-9][a-z0-9-]{0,40}$')][string]$Instance='cs-client-a', [switch]$Renderer,
      [ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$instanceRoot=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance")
$game=Assert-SandboxPath (Join-Path $instanceRoot 'Half-Life')
if(-not(Test-Path -LiteralPath (Join-Path $instanceRoot 'copy-manifest.json'))){throw 'Finish an independently verified sandbox copy first.'}
$running=Get-CimInstance Win32_Process -Filter "Name='MetaHook.exe' OR Name='hlds.exe'" |
    Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)}
if($running){throw 'Stop this sandbox instance before deploying changed DLLs.'}

function New-LocalPort {
    $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
    try {$listener.Start();return $listener.LocalEndpoint.Port} finally {$listener.Stop()}
}
function New-LocalUdpPort([Collections.Generic.HashSet[int]]$Reserved) {
    do {
        $socket=[Net.Sockets.UdpClient]::new([Net.Sockets.AddressFamily]::InterNetwork)
        try {
            $socket.Client.ExclusiveAddressUse=$true
            $socket.Client.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
            $candidate=$socket.Client.LocalEndPoint.Port
        } finally {$socket.Dispose()}
    } while(-not $Reserved.Add($candidate))
    return $candidate
}
function New-Identity { return [Guid]::NewGuid().ToString('N') }
function New-Secret {
    $bytes=[byte[]]::new(16)
    [Security.Cryptography.RandomNumberGenerator]::Fill($bytes)
    return [Convert]::ToHexString($bytes).ToLowerInvariant()
}

$clusterPath=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cluster.json')
if(-not(Test-Path -LiteralPath $clusterPath)){
    @{serverPort=(New-LocalPort);serverSession=(New-Identity);serverToken=(New-Secret);csPort=27045} |
        ConvertTo-Json | Set-Content -LiteralPath $clusterPath -Encoding utf8
}
$cluster=Get-Content -LiteralPath $clusterPath -Raw | ConvertFrom-Json
if(-not $cluster.PSObject.Properties['minecraftPort']){
    $cluster | Add-Member -NotePropertyName minecraftPort -NotePropertyValue 25575
    $cluster | Add-Member -NotePropertyName minecraftRconPort -NotePropertyValue 25576
    $cluster | Add-Member -NotePropertyName rconToken -NotePropertyValue (New-Secret)
    $cluster | ConvertTo-Json | Set-Content -LiteralPath $clusterPath -Encoding utf8
}
$configPath=Assert-SandboxPath (Join-Path $instanceRoot 'instance.json')
if(-not(Test-Path -LiteralPath $configPath)){
    @{name=$Instance;game=$game;clientPort=(New-LocalPort);clientSession=(New-Identity);clientToken=(New-Secret);profileId=(New-Identity)} |
        ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8
}
$config=Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if($config.game -ne $game){throw 'Instance path mismatch'}
if(-not $config.PSObject.Properties['playerName']) {
    $playerName=switch($Instance){'cs-client-a'{'GoldCraft_A'} 'cs-client-b'{'GoldCraft_B'} default{'GC_'+$config.profileId.Substring(0,12)}}
    $config | Add-Member -NotePropertyName playerName -NotePropertyValue $playerName
}
if($config.playerName -notmatch '^[A-Za-z0-9_]{1,16}$'){throw 'Invalid sandbox Minecraft player name'}
$reservedUdp=[Collections.Generic.HashSet[int]]::new()
[void]$reservedUdp.Add([int]$cluster.csPort)
foreach($other in Get-ChildItem -LiteralPath (Join-Path $script:GoldCraftRoot 'sandbox') -Directory) {
    $otherConfig=Join-Path $other.FullName 'instance.json'
    if(Test-Path -LiteralPath $otherConfig) {
        $saved=Get-Content -LiteralPath $otherConfig -Raw | ConvertFrom-Json
        foreach($field in @('csHostPort','csClientPort')) {
            if($saved.PSObject.Properties[$field]){[void]$reservedUdp.Add([int]$saved.$field)}
        }
    }
}
foreach($field in @('csHostPort','csClientPort')) {
    if(-not $config.PSObject.Properties[$field]) {
        $config | Add-Member -NotePropertyName $field -NotePropertyValue (New-LocalUdpPort $reservedUdp)
    }
}
$config | ConvertTo-Json | Set-Content -LiteralPath $configPath -Encoding utf8

$pluginDir=Assert-SandboxPath (Join-Path $game 'cstrike/metahook/plugins')
$configDir=Assert-SandboxPath (Join-Path $game 'cstrike/metahook/configs')
$gameDataDir=Assert-SandboxPath (Join-Path $game 'cstrike/metahook/gamedata')
foreach($directory in @($pluginDir,$configDir,$gameDataDir)){New-Item -ItemType Directory -Path $directory -Force | Out-Null}
$runtimeStage=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/metahook-runtime')
$runtimeManifest=Join-Path $runtimeStage 'manifest.json'
if(-not(Test-Path -LiteralPath $runtimeManifest)){throw 'Run Prepare-MetaHookRuntime.ps1 before deployment to preserve normal plugins and lighting.'}
$runtime=Get-Content -LiteralPath $runtimeManifest -Raw | ConvertFrom-Json
foreach($file in $runtime.files){
    $source=Assert-WorkspacePath (Join-Path $runtimeStage $file.path)
    if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256){throw "Staged runtime changed: $($file.path)"}
}
foreach($file in $runtime.files){
    $source=Assert-WorkspacePath (Join-Path $runtimeStage $file.path)
    $destination=Assert-SandboxPath (Join-Path $game $file.path)
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'build/native-x86/Release/GoldCraft.dll') -Destination (Join-Path $pluginDir 'GoldCraft.dll') -Force
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'build/regamedll/Release/mp.dll') -Destination (Join-Path $game 'cstrike/dlls/mp.dll') -Force
Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot 'dist/metahook/MetaHook.exe') -Destination (Join-Path $game 'MetaHook.exe') -Force
foreach($file in Get-ChildItem -LiteralPath (Join-Path $script:GoldCraftRoot 'dist/metahook/svencoop/metahook/gamedata') -File){
    Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $gameDataDir $file.Name) -Force
}
$pluginsFile=Join-Path $configDir 'plugins.lst'
$enableRenderer=$Renderer -or ((Test-Path -LiteralPath $pluginsFile) -and ((Get-Content -LiteralPath $pluginsFile) -contains 'Renderer_AVX2.dll'))
if($enableRenderer) {
    & python (Join-Path $PSScriptRoot 'Build-ClientGameData.py') --client (Join-Path $game 'cstrike/cl_dlls/client.dll') --existing-catalog $gameDataDir
    if($LASTEXITCODE){throw 'CS client gamedata identity verification failed'}
    $clientCatalog=Assert-SandboxPath (Join-Path $gameDataDir 'goldcraft-cs')
    New-Item -ItemType Directory -Path $clientCatalog -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $script:GoldCraftRoot 'dist/gamedata/goldcraft-cs') -Filter '*.json' | Copy-Item -Destination $clientCatalog -Force
    $rendererDist=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/renderer/svencoop')
    if(Test-Path -LiteralPath (Join-Path $rendererDist 'metahook/plugins/Renderer_AVX2.dll')) {
        Copy-Item -LiteralPath (Join-Path $rendererDist 'metahook/plugins/Renderer_AVX2.dll') -Destination (Join-Path $pluginDir 'Renderer_AVX2.dll') -Force
        foreach($relative in @('renderer','metahook/dlls','metahook/gamedata/renderer')) {
            $destination=Assert-SandboxPath (Join-Path $game "cstrike/$relative")
            New-Item -ItemType Directory -Path $destination -Force | Out-Null
            Get-ChildItem -LiteralPath (Join-Path $rendererDist $relative) -Force | Copy-Item -Destination $destination -Recurse -Force
        }
    }
    if(-not(Test-Path -LiteralPath (Join-Path $pluginDir 'Renderer_AVX2.dll'))){throw 'Renderer_AVX2.dll is missing from this independent sandbox copy'}
    $shaderDir=Assert-SandboxPath (Join-Path $game 'cstrike/renderer/shader')
    New-Item -ItemType Directory -Path $shaderDir -Force | Out-Null
    Get-ChildItem -LiteralPath (Join-Path $script:GoldCraftRoot 'native/client/shaders') -Filter '*.glsl' | Copy-Item -Destination $shaderDir -Force
    foreach($mapLighting in Get-ChildItem -LiteralPath (Join-Path $script:GoldCraftRoot 'native/client/maps') -Filter '*_entity.txt') {
        $lightingFile=Assert-SandboxPath (Join-Path $game "cstrike/maps/$($mapLighting.Name)")
        # Use the normal installation's complete lighting when available;
        # replacing it with one sun used to discard all warehouse lamps.
        if(Test-Path -LiteralPath (Join-Path $runtimeStage "cstrike/maps/$($mapLighting.Name)")){continue}
        $defaults=Get-Content -LiteralPath (Join-Path $game 'cstrike/renderer/default_entity.txt') -Raw
        $defaults+[Environment]::NewLine+(Get-Content -LiteralPath $mapLighting.FullName -Raw) | Set-Content -LiteralPath $lightingFile -Encoding ascii
    }
    # MetaHook inserts plugins at the head and invokes LoadClient in reverse file order.
    # Renderer must wrap GoldCraft so its internal r_params receives our final smoothed camera.
    $runtime.plugins | Set-Content -LiteralPath $pluginsFile -Encoding ascii
} else {'GoldCraft.dll' | Set-Content -LiteralPath $pluginsFile -Encoding ascii}
@'
hostname "GoldCraft isolated development"
sv_lan 1
mp_freezetime 0
mp_roundtime 9
mp_timelimit 0
mp_autoteambalance 0
mp_autokick 0
mp_limitteams 0
mp_auto_join_team 1
humans_join_team "CT"
sv_password ""
developer 1
log on
mh_pluginlist
'@ | Set-Content -LiteralPath (Join-Path $game 'cstrike/goldcraft_test.cfg') -Encoding ascii
if($enableRenderer) {
    @('exec goldcraft_renderer.cfg','r_version') | Add-Content -LiteralPath (Join-Path $game 'cstrike/goldcraft_test.cfg') -Encoding ascii
}
foreach($name in @('autoexec.cfg','server.cfg','listenserver.cfg')){
    'exec goldcraft_test.cfg' | Set-Content -LiteralPath (Join-Path $game "cstrike/$name") -Encoding ascii
}
# The independently copied configuration already contains the user's bindings.
# Never replace it with a reduced test keymap (that previously removed +reload).
$clientConfig=Assert-SandboxPath (Join-Path $game 'cstrike/config.cfg')
@('console "1"','fps_max "100"','gl_vsync "0"','cl_filterstuffcmd "1"',('name "'+$config.playerName+'"')) |
    Add-Content -LiteralPath $clientConfig -Encoding ascii
& (Join-Path $PSScriptRoot 'Initialize-MinecraftSandbox.ps1') -Instance $Instance -Loader $Loader
Write-Host "Deployed only to $game. Local client bridge port: $($config.clientPort)"
