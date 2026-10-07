param([ValidatePattern('^[a-z0-9][a-z0-9-]{0,40}$')][string]$Instance='cs-server')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/Half-Life")
& (Join-Path $PSScriptRoot 'Initialize-SandboxInstance.ps1') -Instance $Instance
$clusterPath=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cluster.json')
$cluster=Get-Content -LiteralPath $clusterPath -Raw | ConvertFrom-Json
if(-not $cluster.PSObject.Properties['csRconToken']) {
    $cluster | Add-Member -NotePropertyName csRconToken -NotePropertyValue ([Convert]::ToHexString([Security.Cryptography.RandomNumberGenerator]::GetBytes(32)))
    $cluster | ConvertTo-Json | Set-Content -LiteralPath $clusterPath -Encoding utf8
}
('rcon_password "'+$cluster.csRconToken+'"') | Add-Content -LiteralPath (Join-Path $game 'cstrike/server.cfg') -Encoding ascii
foreach($name in @('swds.dll','swds.pdb','hlds.exe','hlds.pdb','filesystem_stdio.dll','steam_api.dll')) {
    Copy-Item -LiteralPath (Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "dist/rehlds/$name")) -Destination (Join-Path $game $name) -Force
}
$files=@('hlds.exe','swds.dll','filesystem_stdio.dll','steam_api.dll','cstrike/dlls/mp.dll') | ForEach-Object {
    $file=Get-Item -LiteralPath (Join-Path $game $_)
    @{name=$_;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash;bytes=$file.Length}
}
@{engine='ReHLDS';revision=(& git -C (Join-Path $script:GoldCraftRoot 'external/ReHLDS') rev-parse HEAD);files=$files} |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path (Split-Path $game) 'rehlds-deployment.json') -Encoding utf8
Write-Output "ReHLDS dedicated engine deployed only to $game"
