param([switch]$Restart,[switch]$Start,[switch]$ValidateOnly,[switch]$UpdateClientRuntime,
      [ValidateSet('cs-client-a','cs-client-b')][string[]]$Clients=@('cs-client-b'),
      [ValidatePattern('^[A-Za-z0-9_]{1,64}$')][string]$Map='cs_assault',
      [ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$env:GOLDCRAFT_MOD_LOADER=$Loader
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Use PowerShell 7, or the generated sandbox/modpack/Sync-and-Start.cmd shortcut.'}

# Validate all three environments before interrupting a running game.
& python (Join-Path $PSScriptRoot 'Modpack.py') plan --refresh-core
if($LASTEXITCODE){throw 'The modpack has a conflict. No running process was stopped and no runtime mods were changed.'}
if($Loader -eq 'neoforge'){
    & python (Join-Path $PSScriptRoot 'Prepare-NeoForgeRuntime.py') --verify
    if($LASTEXITCODE){throw 'Prepare the production NeoForge runtime before restarting. Running instances have not been stopped.'}
}
if($UpdateClientRuntime){& (Join-Path $PSScriptRoot 'Deploy-ClientRuntime.ps1') -ValidateOnly -Loader $Loader}
if($ValidateOnly){return}

$roles=@(
    @{Role='CsServer';Instance='cs-server'},
    @{Role='MinecraftServer';Instance='cs-server'},
    @{Role='CsClient';Instance='cs-client-a'},
    @{Role='CsClient';Instance='cs-client-b'},
    @{Role='MinecraftClient';Instance='cs-client-a'},
    @{Role='MinecraftClient';Instance='cs-client-b'}
)
function Get-ManagedRuntime($item) {
    $recordPath=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$($item.Instance)/process-$($item.Role).json")
    if(-not(Test-Path -LiteralPath $recordPath)){return $null}
    $record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json -DateKind String
    $live=Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
    if(-not $live){return $null}
    $expected=if($item.Role -eq 'CsClient'){Join-Path $script:GoldCraftRoot "sandbox/$($item.Instance)/Half-Life/MetaHook.exe"}
        elseif($item.Role -eq 'CsServer'){Join-Path $script:GoldCraftRoot 'sandbox/cs-server/Half-Life/hlds.exe'}
        else{Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1/bin/java.exe'}
    $expected=Assert-WorkspacePath $expected
    # An old record can point to an unrelated process after Windows reuses its
    # PID. Both image and creation time must differ before treating it as gone.
    if($live.ExecutablePath -and $live.ExecutablePath -ne $expected -and
        [Math]::Abs(($live.CreationDate.ToUniversalTime()-[DateTime]::Parse($record.startedUtc).ToUniversalTime()).TotalSeconds) -gt 1) {
        return $null
    }
    if($live.ExecutablePath -ne $expected -or $record.executable -ne $expected -or
        [Math]::Abs(($live.CreationDate.ToUniversalTime()-[DateTime]::Parse($record.startedUtc).ToUniversalTime()).TotalSeconds) -gt 1) {
        throw "Stale or mismatched process identity for $($item.Instance) $($item.Role); no process was stopped."
    }
    return $live
}
$running=@($roles | Where-Object { $null -ne (Get-ManagedRuntime $_) })
if(($Start -or ($Restart -and @($running | Where-Object Role -eq 'CsClient').Count)) -and -not(Get-Process -Name steam -ErrorAction SilentlyContinue)) {
    throw 'Start Steam and sign in to the account that owns CS 1.6 before starting the sandbox. No authentication workaround is applied.'
}
if($Restart) {
    # Stop clients before saving the MC server, and stop the native authority last.
    # A new ReHLDS map epoch applies the user's building-reset policy on restart.
    foreach($roleName in @('MinecraftClient','CsClient','MinecraftServer','CsServer')) {
        foreach($item in $running | Where-Object Role -eq $roleName) {
            & (Join-Path $PSScriptRoot 'Stop-Sandbox.ps1') -Role $item.Role -Instance $item.Instance
        }
    }
}
if($UpdateClientRuntime){& (Join-Path $PSScriptRoot 'Deploy-ClientRuntime.ps1') -Loader $Loader}
& python (Join-Path $PSScriptRoot 'Modpack.py') sync --refresh-core
if($LASTEXITCODE){throw 'Mod synchronization did not complete. Runtime startup was cancelled.'}

$selectedRoles=@($roles | Where-Object { $_.Instance -eq 'cs-server' -or $_.Instance -in $Clients })
$requested=if($Start){$selectedRoles}elseif($Restart){@($running | Where-Object { $_.Instance -eq 'cs-server' -or $_.Instance -in $Clients })}else{@()}
foreach($item in $requested) {
    if($null -ne (Get-ManagedRuntime $item)){continue}
    $launchScript=Assert-WorkspacePath (Join-Path $PSScriptRoot 'Start-Sandbox.ps1')
    $logDirectory=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$($item.Instance)/logs")
    New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
    $stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
    $arguments=@('-NoLogo','-NoProfile','-File',('"'+$launchScript+'"'),'-Role',$item.Role,'-Instance',$item.Instance,'-Map',$Map,'-Loader',$Loader,'-Wait')
    if($item.Role -eq 'CsClient'){$arguments+='-Capture'}
    # A hidden real console gives ReHLDS its required console input handle.
    $launch=@{FilePath=(Join-Path $PSHOME 'pwsh.exe');ArgumentList=$arguments;WindowStyle='Hidden';PassThru=$true;WorkingDirectory=$script:GoldCraftRoot}
    if($item.Role -ne 'CsServer') {
        $launch.RedirectStandardOutput=Join-Path $logDirectory "launch-$($item.Role)-$stamp.log"
        $launch.RedirectStandardError=Join-Path $logDirectory "launch-$($item.Role)-$stamp.error.log"
    }
    $helper=Start-Process @launch
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 200
        $runtime=Get-ManagedRuntime $item
        if($helper.HasExited -and -not $runtime){throw "Startup failed for $($item.Role); see $logDirectory/launch-$($item.Role)-$stamp.error.log"}
    } while(-not $runtime -and [DateTime]::UtcNow -lt $deadline)
    if(-not $runtime){throw "Timed out starting $($item.Instance) $($item.Role). Inspect the launcher log before retrying."}
    Write-Output "$($item.Instance) $($item.Role) running as PID $($runtime.ProcessId)"
}
if($Start) {
    Write-Output "Waiting for selected CS/Minecraft pairs: $($Clients -join ', ')..."
    $deadline=[DateTime]::UtcNow.AddSeconds(120)
    do {
        $ready=$true
        foreach($role in $selectedRoles){
            if(-not(Get-ManagedRuntime $role)){throw "$($role.Instance) $($role.Role) exited during startup; inspect its sandbox log before retrying."}
        }
        foreach($instance in $Clients) {
            $item=@{Role='CsClient';Instance=$instance}
            $runtime=Get-ManagedRuntime $item
            if(-not $runtime){throw "$instance exited during startup; inspect its sandbox logs."}
            try {
                $path=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$instance/logs/goldcraft-client-status.json")
                $file=Get-Item -LiteralPath $path
                $status=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
                if($file.LastWriteTimeUtc -lt $runtime.CreationDate.ToUniversalTime() -or -not $status.connected -or $status.world -eq '0'){$ready=$false}
            }catch{$ready=$false}
        }
        if(-not $ready){Start-Sleep -Milliseconds 500}
    } while(-not $ready -and [DateTime]::UtcNow -lt $deadline)
    if(-not $ready){throw 'Mod files are synchronized, but CS/Minecraft pairing did not become ready. Check the actual CS dialogs and sandbox logs; process creation alone is not a successful launch.'}
    Write-Output 'Selected CS/Minecraft pairs are connected with the synchronized Mod set.'
}else{Write-Output 'Mod files are synchronized.'}
