param([switch]$Hidden,[switch]$Diagnostics,[switch]$CheckOnly)
. (Join-Path $PSScriptRoot 'Common.ps1')
$settings=Read-Json (Join-Path $PackageRoot 'server-settings.json')
$http=Get-HttpDownloadSettings $settings
$null=Get-CsPublicPort $settings
if([int]$settings.csolGameMode -ne 2 -or $settings.map -ne 'zs_deadend'){throw 'Native scenario requires mode 2 and zs_deadend.'}
foreach($name in @('csPort','steamPort','bridgePort')){if($settings.$name -lt 1024 -or $settings.$name -gt 65535){throw "Invalid $name"}}
if($settings.csPort -eq $settings.steamPort){throw 'CS and Steam ports must differ.'}
if($http.enabled -and $http.port -eq $settings.bridgePort){throw 'HTTP and bridge TCP ports must differ.'}
$address=$null
if(![Net.IPAddress]::TryParse($settings.bindAddress,[ref]$address) -or $address.AddressFamily -ne 'InterNetwork'){throw 'Use an IPv4 bind address.'}
$root=Join-Path $PackageRoot 'cs'
foreach($path in @('hlds.exe','swds.dll','cstrike/dlls/mp.dll','cstrike/maps/zs_deadend.bsp','cstrike/addons/amxmodx/plugins/goldcraft_game_mode.amxx')){
    if(!(Test-Path -LiteralPath (Join-Path $root $path) -PathType Leaf)){throw "Missing scenario file: $path"}
}
if($CheckOnly){Write-Host 'Native scenario configuration valid; no Minecraft process or dimension is required.';return}
New-Item -ItemType Directory -Path $State -Force|Out-Null
$gate=[IO.File]::Open((Join-Path $State 'startup.lock'),'OpenOrCreate','ReadWrite','None')
try {
    if((Get-RoleProcess 'cs') -or (Get-RoleProcess 'minecraft')){throw 'Use the original Stop.cmd before starting scenario mode.'}
    foreach($port in @($settings.csPort,$settings.steamPort)){
        $probe=New-Object Net.Sockets.UdpClient
        try{$probe.ExclusiveAddressUse=$true;$probe.Client.Bind((New-Object Net.IPEndPoint($address,[int]$port)))}finally{$probe.Dispose()}
    }
    $tcpPorts=@([int]$settings.bridgePort);if($http.enabled){$tcpPorts+=@([int]$http.port)}
    foreach($port in $tcpPorts){$probe=New-Object Net.Sockets.TcpListener($address,$port);try{$probe.Server.ExclusiveAddressUse=$true;$probe.Start()}finally{$probe.Stop()}}
    $secrets=Join-Path $State 'secrets.json'
    if(!(Test-Path -LiteralPath $secrets)){Write-Json $secrets @{session=(Random-Hex 16);token=(Random-Hex 16)}}
    # Do not regenerate runtime.cfg or reset its operator's RCON configuration.
    $shell=Join-Path $PSHOME 'powershell.exe';if(!(Test-Path -LiteralPath $shell)){$shell=Join-Path $PSHOME 'pwsh.exe'}
    $script=Join-Path $PSScriptRoot 'Run-Server.ps1'
    $scenarioArguments='-NoLogo -NoProfile -ExecutionPolicy Bypass -File "'+$script+'" -Role cs'
    if($Hidden){$scenarioArguments+=' -Hidden'}
    if($Diagnostics){$scenarioArguments+=' -Diagnostics'}
    $supervisor=Start-Process -FilePath $shell -ArgumentList $scenarioArguments -WorkingDirectory $PackageRoot -WindowStyle Hidden -PassThru
    $deadline=[DateTime]::UtcNow.AddSeconds(30)
    while([DateTime]::UtcNow -lt $deadline){
        if(Get-RoleProcess 'cs'){Write-Host 'Native CS scenario started. Minecraft was not launched. Use original Stop.cmd to stop.';return}
        if($supervisor.HasExited){throw 'Scenario supervisor exited; inspect its console.'}
        Start-Sleep -Milliseconds 200
    }
    throw 'Scenario startup timed out. Use Stop.cmd before retrying; processes were not forcibly killed.'
}finally{$gate.Dispose()}
