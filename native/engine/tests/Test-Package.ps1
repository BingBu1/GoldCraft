param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$sourceRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$work=Join-Path $OutputDirectory ('package-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
. (Join-Path $sourceRoot 'tools/server-package/scripts/Common.ps1')
$script:checks=0
function Check($condition,[string]$message) { $script:checks++; if(-not $condition){throw $message} }
function Reject($settings) {
    $rejected=$false
    try { $null=Get-HttpDownloadSettings $settings } catch { $rejected=$true }
    Check $rejected 'Invalid HTTP settings were accepted.'
}
$default=Get-HttpDownloadSettings ('{}'|ConvertFrom-Json)
Check ($default.enabled -and $default.port -eq 27080 -and $default.url -eq '') 'Old package defaults differ.'
$disabled=Get-HttpDownloadSettings ('{"httpDownload":false}'|ConvertFrom-Json)
Check (-not $disabled.enabled) 'Disable setting ignored.'
$valid=Get-HttpDownloadSettings ('{"httpPublicUrl":"http://example.invalid:28080/goldcraft/","httpPort":28080}'|ConvertFrom-Json)
Check ($valid.url -eq 'http://example.invalid:28080/goldcraft/' -and $valid.port -eq 28080) 'Public/local settings differ.'
foreach($json in @('{"httpDownload":1}','{"httpPort":0}','{"httpPort":65536}','{"httpPort":1.5}',
                   '{"httpPort":"27080"}','{"httpPublicUrl":null}','{"httpPublicUrl":"https://x/goldcraft/"}',
                   '{"httpPublicUrl":"http://x:0/goldcraft/"}','{"httpPublicUrl":"http://x:65536/goldcraft/"}',
                   '{"httpPublicUrl":"http://user@x/goldcraft/"}','{"httpPublicUrl":"http://x/other/"}',
                   '{"httpPublicUrl":"http://x/goldcraft/;quit"}',
                   '{"httpPublicUrl":"HTTP://x/goldcraft/"}','{"httpPublicUrl":"http://x/GOLDCRAFT/"}')) { Reject ($json|ConvertFrom-Json) }
# Actual startup script, with tiny declared test payloads and no game runtime.
# -CheckOnly must perform validation and return before any launch/write stage.
$package=Join-Path $work 'package'
foreach($dir in @('scripts','cs/cstrike/maps','minecraft/mods','minecraft/goldcraft-worlds/datapacks/goldcraft_host_maps/data/goldcraft/dimension')) {
    New-Item -ItemType Directory -Path (Join-Path $package $dir) -Force | Out-Null
}
foreach($name in @('Common.ps1','Start-Servers.ps1')) { Copy-Item -LiteralPath (Join-Path $sourceRoot "tools/server-package/scripts/$name") -Destination (Join-Path $package "scripts/$name") }
$fixtureSettings=Read-Json (Join-Path $sourceRoot 'tools/server-package/server-settings.json')
# Match Start-Servers: the CS/Steam listeners are UDP and the remaining three
# are TCP, all bound to Any with exclusive use. A free TCP port can be reserved
# or unavailable for UDP on Windows, so TCP-only allocation is not sufficient.
$heldTcp=@(); $heldUdp=@(); $ports=@()
try {
    while($ports.Count -lt 5) {
        if($ports.Count -lt 2) {
            $listener=New-Object Net.Sockets.UdpClient
            $heldUdp+=,$listener
            $listener.ExclusiveAddressUse=$true
            $listener.Client.Bind((New-Object Net.IPEndPoint([Net.IPAddress]::Any,0)))
            $port=$listener.Client.LocalEndPoint.Port
        } else {
            $listener=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any,0)
            $heldTcp+=,$listener
            $listener.Server.ExclusiveAddressUse=$true
            $listener.Start()
            $port=$listener.LocalEndpoint.Port
        }
        if($port -notin $ports){$ports+=,$port}
    }
} finally {
    foreach($listener in $heldTcp){$listener.Stop()}
    foreach($listener in $heldUdp){$listener.Dispose()}
}
$fixtureSettings.csPort=$ports[0];$fixtureSettings.steamPort=$ports[1];$fixtureSettings.minecraftPort=$ports[2]
$fixtureSettings.bridgePort=$ports[3];$fixtureSettings.httpPort=$ports[4];$fixtureSettings.bindAddress='127.0.0.1'
$bsp=Join-Path $package 'cs/cstrike/maps/cs_assault.bsp'
[IO.File]::WriteAllBytes($bsp,[byte[]](1,2,3,4))
$hashStream=[IO.File]::OpenRead($bsp)
$hashProvider=[Security.Cryptography.SHA256]::Create()
try { $hash=[BitConverter]::ToString($hashProvider.ComputeHash($hashStream)).Replace('-','') }
finally { $hashProvider.Dispose(); $hashStream.Dispose() }
$map=@(@{map='cs_assault';dimension='goldcraft:cs_assault_12345678';bspSha256=$hash})
Write-Text (Join-Path $package 'maps.json') (ConvertTo-Json -InputObject $map)
Write-Text (Join-Path $package 'minecraft/goldcraft-worlds/datapacks/goldcraft_host_maps/data/goldcraft/dimension/cs_assault_12345678.json') '{}'
Write-Text (Join-Path $package 'minecraft/server.properties') "online-mode=false`nenforce-secure-profile=false`nwhite-list=false`nenforce-whitelist=false`nenable-rcon=false`nmax-players=32`n"
Write-Text (Join-Path $package 'cs/swds.dll') 'TEST PAYLOAD: GoldCraft built-in HTTP listening'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$jarPath=Join-Path $package 'minecraft/mods/goldcraft-neoforge-0.1.0-dev.jar'
$jar=[IO.Compression.ZipFile]::Open($jarPath,'Create')
try {
    $null=$jar.CreateEntry('dev/goldcraft/net/CsAdmission.class')
    $entry=$jar.CreateEntry('META-INF/goldcraft-capabilities.json')
    $writer=New-Object IO.StreamWriter($entry.Open())
    try{$writer.Write('{"csAdmissionVersion":1,"offlineCsAdmission":true}')}finally{$writer.Dispose()}
}finally{$jar.Dispose()}
$shell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
function StartupCheck([bool]$expectSuccess,[string]$label) {
    Write-Json (Join-Path $package 'server-settings.json') $fixtureSettings
    $messages=& $shell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $package 'scripts/Start-Servers.ps1') -CheckOnly 2>&1
    $exitCode=$LASTEXITCODE
    Write-Text (Join-Path $work ($label+'.log')) ($messages -join "`n")
    Check (($exitCode -eq 0) -eq $expectSuccess) "Startup check $label unexpected exit $exitCode."
    Check (-not(Test-Path -LiteralPath (Join-Path $package '.state/secrets.json'))) 'CheckOnly generated secrets.'
    Check (-not(Test-Path -LiteralPath (Join-Path $package 'cs/cstrike/goldcraft-runtime.cfg'))) 'CheckOnly changed runtime config.'
}
StartupCheck $true 'valid'
$fixtureSettings.httpPort=$fixtureSettings.bridgePort
StartupCheck $false 'collision'
$fixtureSettings.httpPort=$ports[4]
Write-Text (Join-Path $package 'cs/swds.dll') 'TEST PAYLOAD: old engine without HTTP'
StartupCheck $false 'missing-engine-support'
$fixtureSettings.httpDownload=$false
StartupCheck $true 'disabled-old-engine'
Write-Text (Join-Path $work 'result.json') (@{passed=$true;checks=$checks;powerShell=$PSVersionTable.PSVersion.ToString();launchedGames=$false}|ConvertTo-Json)
Write-Host "HTTP package checks=$checks passed; evidence $work"
