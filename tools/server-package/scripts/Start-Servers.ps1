param([switch]$AcceptEula, [switch]$CheckOnly, [switch]$Hidden, [switch]$Diagnostics,
      [ValidateRange(30,180)][int]$StartupTimeoutSeconds=180)
. (Join-Path $PSScriptRoot 'Common.ps1')
function Get-MapHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}
$startupLock = $null
$launched = @()
try {
    New-Item -ItemType Directory -Path $State -Force | Out-Null
    $startupLock = [IO.File]::Open((Join-Path $State 'startup.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    $settings = Read-Json (Join-Path $PackageRoot 'server-settings.json')
    $csPublicPort = Get-CsPublicPort $settings
    $http = Get-HttpDownloadSettings $settings
    if ($csPublicPort) {
        $engine = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $PackageRoot 'cs/swds.dll')))
        if (-not $engine.Contains('GoldCraft -publicport requires an integer in 1..65535')) {
            throw 'ReHLDS lacks public-port support. Update cs/swds.dll before setting csPublicPort.'
        }
    }
    foreach ($key in @('csPort','steamPort','minecraftPort','bridgePort')) {
        if ($settings.$key -lt 1024 -or $settings.$key -gt 65535) { throw "Invalid $key" }
    }
    if (@($settings.csPort,$settings.steamPort,$settings.minecraftPort,$settings.bridgePort | Select-Object -Unique).Count -ne 4) { throw 'Use four different ports.' }
    if ($settings.minecraftMemoryGB -lt 2 -or $settings.minecraftMemoryGB -gt 128) { throw 'Minecraft memory must be 2-128 GB.' }
    if ($settings.botTotalPlayers -lt 0 -or $settings.botTotalPlayers -gt 31) { throw 'Bot population must be 0-31.' }
    $address = $null
    if (-not [Net.IPAddress]::TryParse($settings.bindAddress, [ref]$address) -or $address.AddressFamily -ne 'InterNetwork') { throw 'Use an IPv4 bindAddress.' }
    if ($settings.map -notmatch '^[a-zA-Z0-9_]+$' -or -not (Test-Path -LiteralPath (Join-Path $PackageRoot "cs/cstrike/maps/$($settings.map).bsp"))) { throw 'Map is missing or invalid.' }
    $maps = Read-Json (Join-Path $PackageRoot 'maps.json')
    foreach ($map in $maps) {
        if ($map.map -notmatch '^[a-zA-Z0-9_]+$' -or $map.dimension -notmatch '^goldcraft:[a-z0-9_]+_[a-f0-9]{8}$') { throw 'Invalid packaged map identity.' }
        $bsp = Join-Path $PackageRoot "cs/cstrike/maps/$($map.map).bsp"
        $dimension = $map.dimension.Substring('goldcraft:'.Length)
        $dimensionFile = Join-Path $PackageRoot "minecraft/goldcraft-worlds/datapacks/goldcraft_host_maps/data/goldcraft/dimension/$dimension.json"
        if (-not (Test-Path -LiteralPath $bsp) -or -not (Test-Path -LiteralPath $dimensionFile) -or
            (Get-MapHash $bsp) -ne $map.bspSha256) {
            throw "Map $($map.map) differs from its Minecraft dimension. Rebuild the server package after changing maps."
        }
    }
    if ($settings.map -notin @($maps | ForEach-Object { $_.map })) { throw 'Selected map is not in maps.json. Rebuild the package.' }
    foreach ($role in @('cs','minecraft')) { if (Get-RoleProcess $role) { throw "$role is already running; stop it first." } }
    $tcpPorts = @($settings.minecraftPort,$settings.bridgePort)
    if ($http.enabled) {
        if ($http.port -in $tcpPorts) { throw 'httpPort must differ from Minecraft and bridge TCP ports.' }
        $tcpPorts += $http.port
        $engineHttp = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $PackageRoot 'cs/swds.dll')))
        if (-not $engineHttp.Contains('GoldCraft built-in HTTP listening')) { throw 'Update cs/swds.dll for the built-in HTTP downloader.' }
    }
    foreach ($port in $tcpPorts) {
        $listener = New-Object Net.Sockets.TcpListener([Net.IPAddress]::Any, [int]$port)
        $listener.Server.ExclusiveAddressUse = $true
        try { $listener.Start() } finally { $listener.Stop() }
    }
    foreach ($port in @($settings.csPort,$settings.steamPort)) {
        $udp = New-Object Net.Sockets.UdpClient
        try { $udp.ExclusiveAddressUse=$true; $udp.Client.Bind((New-Object Net.IPEndPoint([Net.IPAddress]::Any,[int]$port))) }
        finally { $udp.Dispose() }
    }
    $properties = Join-Path $PackageRoot 'minecraft/server.properties'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $core = [IO.Compression.ZipFile]::OpenRead((Join-Path $PackageRoot 'minecraft/mods/goldcraft-neoforge-0.1.0-dev.jar'))
    try {
        if (-not $core.GetEntry('dev/goldcraft/net/CsAdmission.class')) { throw 'GoldCraft JAR is missing automatic CS admission. Update both client and server.' }
        $entry = $core.GetEntry('META-INF/goldcraft-capabilities.json')
        if (-not $entry) { throw 'GoldCraft JAR lacks offline CS admission support. Update the core.' }
        $reader = New-Object IO.StreamReader($entry.Open())
        try { $capabilities = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
        if ($capabilities.csAdmissionVersion -ne 1 -or $capabilities.offlineCsAdmission -isnot [bool] -or
            -not $capabilities.offlineCsAdmission) { throw 'GoldCraft JAR lacks offline CS admission support. Update the core.' }
    } finally { $core.Dispose() }
    $propertyLines = @(Get-Content -LiteralPath $properties -Encoding UTF8)
    $onlineModes = @($propertyLines | Where-Object { $_ -match '^online-mode=' })
    if ($onlineModes.Count -ne 1 -or $onlineModes[0] -notmatch '^online-mode=(true|false)$') { throw 'Set online-mode=true or online-mode=false exactly once.' }
    if ($onlineModes[0] -eq 'online-mode=false' -and $propertyLines -notcontains 'enforce-secure-profile=false') {
        throw 'Offline Minecraft requires enforce-secure-profile=false.'
    }
    foreach ($pair in @(@('white-list','false'),@('enforce-whitelist','false'),@('enable-rcon','false'),@('max-players','32'))) {
        if ($propertyLines -notcontains "$($pair[0])=$($pair[1])") {
            throw "Required production property: $($pair[0])=$($pair[1])"
        }
    }
    if ($CheckOnly) { Write-Host 'Configuration and port checks passed; no server was launched.'; return }
    $eula = Join-Path $PackageRoot 'minecraft/eula.txt'
    if ((Get-Content -LiteralPath $eula) -notcontains 'eula=true') {
        if (-not $AcceptEula) {
            Write-Host 'Read https://aka.ms/MinecraftEULA before running Minecraft.'
            if ((Read-Host 'Do you accept the Minecraft EULA? Type YES') -cne 'YES') { throw 'Minecraft EULA was not accepted.' }
        }
        Write-Text $eula "# https://aka.ms/MinecraftEULA`neula=true`n"
    }
    $secretsPath = Join-Path $State 'secrets.json'
    if (-not (Test-Path -LiteralPath $secretsPath)) { Write-Json $secretsPath @{session=(Random-Hex 16);token=(Random-Hex 16)} }
    Set-Property $properties 'server-port' ([string]$settings.minecraftPort)
    Set-Property $properties 'server-ip' $(if ($settings.bindAddress -eq '0.0.0.0') { '' } else { $settings.bindAddress })
    $botConfig = Join-Path $PackageRoot 'cs/cstrike/addons/sypb/sypb.cfg'
    $botLines = @(Get-Content -LiteralPath $botConfig -Encoding UTF8 | Where-Object { $_ -notmatch '^\s*sypb_(auto_players|quota)\s' })
    $botLines += "sypb_auto_players $($settings.botTotalPlayers)"
    $botLines += "sypb_quota $([Math]::Max(0,[int]$settings.botTotalPlayers-1))"
    Write-Text $botConfig ($botLines -join "`n")
    Write-Text (Join-Path $PackageRoot 'cs/cstrike/goldcraft-runtime.cfg') "sv_lan 0`nrcon_password `"`"`n"
    foreach ($name in @('goldcraft-server-status.json','minecraft-server-status.json')) {
        $path = Join-Path $State $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    # Explicitly visible consoles are part of the server operator interface.
    foreach ($role in @('cs','minecraft')) {
        $scriptPath = Join-Path $PSScriptRoot 'Run-Server.ps1'
        $shell = Join-Path $PSHOME 'powershell.exe'
        if (-not (Test-Path -LiteralPath $shell)) { $shell = Join-Path $PSHOME 'pwsh.exe' }
        $style = if ($Hidden -or $role -eq 'cs') { 'Hidden' } else { 'Normal' }
        $extra = if ($Hidden) { ' -Hidden' } else { '' }
        if ($Diagnostics) { $extra += ' -Diagnostics' }
        Start-Process -FilePath $shell -ArgumentList "-NoLogo -NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`" -Role $role$extra" -WorkingDirectory $PackageRoot -WindowStyle $style | Out-Null
        $launched += $role
        $limit=[DateTime]::UtcNow.AddSeconds(20)
        while (-not (Get-RoleProcess $role)) {
            if ([DateTime]::UtcNow -gt $limit) { throw "$role did not start; see its console." }
            Start-Sleep -Milliseconds 200
        }
    }
    $deadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($role in $launched) { if (-not (Get-RoleProcess $role)) { throw "$role exited during startup." } }
        $native = Join-Path $State 'goldcraft-server-status.json'
        $java = Join-Path $State 'minecraft-server-status.json'
        if ((Test-Path -LiteralPath $native) -and (Test-Path -LiteralPath $java)) {
            try {
                $n=Read-Json $native; $j=Read-Json $java
                if ($n.world -and $n.world -ne '0' -and $n.world -eq $j.world -and $j.map -eq $settings.map) {
                    Write-Host "Both servers ready: CS UDP $($settings.csPort); Minecraft TCP $($settings.minecraftPort); 32 slots each."
                    if ($csPublicPort) { Write-Host "CS advertises public game port $csPublicPort; forward this UDP port to local $($settings.csPort)." }
                    if ($http.enabled) {
                        Write-Host "Built-in HTTP download: local TCP $($http.port)."
                        if ($http.url) { Write-Host "Players download from $($http.url); map its TCP endpoint to local $($http.port)." }
                        else { Write-Host 'Set httpPublicUrl to a player-reachable HTTP tunnel URL ending /goldcraft/. Until then, clients use the existing download channel.' }
                    }
                    Write-Host 'Minecraft requires a valid CS connection. Pairing is automatic; see README.md.'
                    return
                }
            } catch { }
        }
        Start-Sleep -Milliseconds 500
    }
    throw 'Timed out waiting for the authenticated GoldCraft bridge; inspect both consoles.'
} catch {
    Write-Host "Startup failed: $($_.Exception.Message)" -ForegroundColor Red
    foreach ($role in @('minecraft','cs')) {
        if ($launched -contains $role -and (Get-RoleProcess $role)) {
            try { Send-RoleCommand $role $(if ($role -eq 'cs') {'quit'} else {'stop'}) } catch { Write-Warning $_ }
        }
    }
    exit 1
} finally { if ($startupLock) { $startupLock.Dispose() } }
