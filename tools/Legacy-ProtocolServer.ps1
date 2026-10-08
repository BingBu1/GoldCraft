# Reuse the stopped combat fixture's assets; no second client or asset copy.
param([Parameter(Mandatory)][ValidateSet('Prepare','Run','Stop','Restore')][string]$Action,[switch]$StartupLog)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$root=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/headless-combat')
$game=Assert-SandboxPath (Join-Path $root 'Half-Life')
$statePath=Assert-SandboxPath (Join-Path $root 'legacy-protocol.json')
$processPath=Assert-SandboxPath (Join-Path $root 'legacy-protocol-process.json')
$owned=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)})
if($Action -eq 'Stop'){
    if(-not(Test-Path -LiteralPath $processPath)){throw 'No recorded legacy server'}
    $record=Get-Content -LiteralPath $processPath -Raw | ConvertFrom-Json -DateKind String
    $live=Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
    if($live){
        if($live.ExecutablePath -ne (Join-Path $game 'hlds.exe') -or
           [Math]::Abs(($live.CreationDate.ToUniversalTime()-[DateTime]::Parse($record.startedUtc).ToUniversalTime()).TotalSeconds) -gt 1){throw 'Legacy PID identity changed'}
        Stop-Process -Id $record.pid
    }
    Write-Output 'Legacy protocol server stopped.';return
}
if($owned){throw 'Stop the owned fixture processes before preparing, running or restoring it.'}
if($Action -eq 'Prepare'){
    if(-not(Test-Path -LiteralPath (Join-Path $root 'deployment.json'))){throw 'Prepare the independent headless-combat fixture first.'}
    if(Test-Path -LiteralPath $statePath){throw 'Restore the previous legacy test before preparing another.'}
    $baseline=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'analysis/installation/original-baseline.json') -Raw | ConvertFrom-Json
    $source=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-client-b/Half-Life')
    $replacements=@{}
    foreach($relative in @('hlds.exe','swds.dll','filesystem_stdio.dll','steam_api.dll','mss32.dll','SDL2.dll','SDL3.dll','cstrike/dlls/mp.dll')){
        $path=Assert-SandboxPath (Join-Path $source $relative)
        $entry=$baseline.files | Where-Object {$_.path.Replace('\','/') -eq $relative}
        if(-not $entry){throw "Stock binary absent from protected baseline: $relative"}
        if((Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256){
            # A workspace game DLL may already be modified. Read the verified
            # original only as a copy source; never execute/write in that tree.
            $path=Join-Path $script:GoldCraftOriginal $relative
            Assert-NoReparsePath $path
            if((Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256){throw "Original binary changed: $relative"}
        }
        $replacements[$relative]=[IO.File]::ReadAllBytes($path)
    }
    $config=Get-Content -LiteralPath (Join-Path $root 'cluster.json') -Raw | ConvertFrom-Json
    $replacements['cstrike/server.cfg']=[Text.Encoding]::ASCII.GetBytes((@('hostname "GoldCraft stock protocol fixture"','sv_lan 1','log off','mp_timelimit 0','mp_autokick 0',('rcon_password "'+$config.csRconToken+'"')) -join "`n")+"`n")
    $replacements['cstrike/autoexec.cfg']=[Text.Encoding]::ASCII.GetBytes("// Independent stock protocol fixture.`n")
    $replacements['cstrike/valve.rc']=[Text.Encoding]::ASCII.GetBytes("ip 127.0.0.1`nexec server.cfg`nstuffcmds`n")
    $liblist=[IO.File]::ReadAllText((Join-Path $game 'cstrike/liblist.gam'))
    $replacements['cstrike/liblist.gam']=[Text.Encoding]::ASCII.GetBytes([Regex]::Replace($liblist,'(?m)^\s*gamedll\s+"[^"]*"','gamedll "dlls/mp.dll"'))
    $backup=Assert-SandboxPath (Join-Path $root 'legacy-protocol-backup')
    $files=@()
    foreach($relative in $replacements.Keys){
        $target=Assert-SandboxPath (Join-Path $game $relative)
        $saved=Assert-SandboxPath (Join-Path $backup $relative)
        New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
        $hadFile=Test-Path -LiteralPath $target -PathType Leaf
        $before=$null
        if($hadFile){Copy-Item -LiteralPath $target -Destination $saved -Force;$before=(Get-FileHash -LiteralPath $saved).Hash}
        $files+=@{path=$relative;hadFile=$hadFile;before=$before;test=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($replacements[$relative]))}
    }
    @{files=$files;engine='unchanged stock engine/game DLL matched to original baseline';port=$config.csPort} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $statePath -Encoding utf8
    foreach($relative in $replacements.Keys){[IO.File]::WriteAllBytes((Assert-SandboxPath (Join-Path $game $relative)),$replacements[$relative])}
    Write-Output "Prepared stock protocol server on loopback port $($config.csPort), reusing existing fixture assets.";return
}
$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
foreach($entry in $state.files){
    if((Get-FileHash -LiteralPath (Assert-SandboxPath (Join-Path $game $entry.path))).Hash -ne $entry.test){throw "Preserving changed test file: $($entry.path)"}
}
if($Action -eq 'Restore'){
    foreach($entry in $state.files){
        if(-not $entry.hadFile){continue}
        $saved=Assert-SandboxPath (Join-Path $root ('legacy-protocol-backup/'+$entry.path))
        if((Get-FileHash -LiteralPath $saved).Hash -ne $entry.before){throw 'Legacy backup hash changed'}
    }
    foreach($entry in $state.files){
        if(-not $entry.hadFile){Remove-Item -LiteralPath (Assert-SandboxPath (Join-Path $game $entry.path));continue}
        $saved=Assert-SandboxPath (Join-Path $root ('legacy-protocol-backup/'+$entry.path))
        Copy-Item -LiteralPath $saved -Destination (Assert-SandboxPath (Join-Path $game $entry.path)) -Force
        Remove-Item -LiteralPath $saved
    }
    Remove-Item -LiteralPath $statePath
    Write-Output 'Restored all fixture binaries/configs from verified backups.';return
}
if([Console]::IsInputRedirected){throw 'Run this dedicated server from a PTY.'}
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=Join-Path $game 'hlds.exe';$start.WorkingDirectory=$game
$start.UseShellExecute=$false;$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
foreach($name in @('GOLDCRAFT_SERVER_PORT','GOLDCRAFT_SERVER_SESSION','GOLDCRAFT_SERVER_TOKEN')){$start.Environment.Remove($name)|Out-Null}
foreach($arg in @('-console','-insecure','-nomaster','-game','cstrike','-port',[string]$state.port,'+ip','127.0.0.1','+sv_lan','1','+maxplayers','4','+map','cs_assault')){$start.ArgumentList.Add($arg)}
if($StartupLog){$start.ArgumentList.Add('-condebug')}
$process=[Diagnostics.Process]::Start($start)
@{pid=$process.Id;startedUtc=$process.StartTime.ToUniversalTime().ToString('O')} | ConvertTo-Json | Set-Content -LiteralPath $processPath -Encoding utf8
Write-Output "Stock protocol server PID $($process.Id), loopback port $($state.port)."
$process.WaitForExit();exit $process.ExitCode
