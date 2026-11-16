# Shared by server patch installers. Uses the existing package's graceful
# stop/start scripts; never kills a game or guesses a process by name alone.
function Get-UpdateProcesses([string]$Root) {
    $prefix=(Plain $Root).TrimEnd('\')+'\'
    @(Get-CimInstance Win32_Process | Where-Object {
        $_.ProcessId -ne $PID -and (
            ($_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) -or
            ($_.Name -match '^(powershell|pwsh|java|javaw)\.exe$' -and $_.CommandLine -and
             $_.CommandLine.IndexOf($prefix,[StringComparison]::OrdinalIgnoreCase) -ge 0))
    })
}
function Get-UpdateRole([string]$Root,[string]$Role) {
    $path=Under $Root ('.state/'+$Role+'.json')
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){return $null}
    $record=Text $path|ConvertFrom-Json
    if($record.role -cne $Role -or $record.runId -cnotmatch '^[a-f0-9]{24}$' -or
       $record.startTicks -notmatch '^\d+$' -or $record.pid -le 0){throw "Invalid $Role process record; no process was stopped."}
    $process=Get-Process -Id $record.pid -ErrorAction SilentlyContinue
    if(!$process){return $null}
    try {
        if($process.StartTime.ToUniversalTime().Ticks.ToString() -cne [string]$record.startTicks){return $null}
        $expected=Under $Root $(if($Role -eq 'cs'){'cs/hlds.exe'}else{'runtime/java/bin/java.exe'})
        if(!$process.Path -or ![string]::Equals((Plain $process.Path),$expected,[StringComparison]::OrdinalIgnoreCase)){
            throw "Unexpected $Role executable; no process was stopped."
        }
        return $record
    }finally{$process.Dispose()}
}
function Invoke-UpdateServerScript([string]$Root,[string]$Name,[int]$TimeoutSeconds,[switch]$Hidden) {
    $path=Under $Root ('scripts/'+$Name)
    if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing original server script: scripts/$Name"}
    $shell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $arguments='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$path+'"'
    if($Hidden){$arguments+=' -Hidden'}
    # File redirection cannot keep the updater waiting on a pipe inherited by a
    # long-lived server supervisor. Two fixed log pairs replace previous runs.
    $stdout=Under $Root ('.bingsang-current-update/'+$Name+'.out.log')
    $stderr=Under $Root ('.bingsang-current-update/'+$Name+'.err.log')
    $process=Start-Process -FilePath $shell -ArgumentList $arguments -WorkingDirectory $Root -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    try {
        # WinPS 5.1 can otherwise release its cached process handle before
        # ExitCode is read for a fast helper; retain it before waiting.
        $null=$process.Handle
        $watch=[Diagnostics.Stopwatch]::StartNew();$nextMessage=10
        while(!$process.WaitForExit(250)){
            if($watch.Elapsed.TotalSeconds -ge $TimeoutSeconds){
                throw "$Name timed out. Update stopped; no server process was forcibly killed. Inspect the original consoles before retrying."
            }
            if($watch.Elapsed.TotalSeconds -ge $nextMessage){Write-Host "Waiting for $Name...";$nextMessage+=10}
        }
        $exitCode=$process.ExitCode
        if($null -eq $exitCode -or $exitCode -ne 0){
            # Original startup errors may contain local addresses. Keep them in
            # the operator console, never in a distributable package receipt.
            $output=Text $stdout;$errorOutput=Text $stderr
            if($output){Write-Host $output.TrimEnd()}
            if($errorOutput){Write-Host $errorOutput.TrimEnd()}
            throw "$Name failed with exit code $exitCode."
        }
    }finally{$process.Dispose()}
}
function Begin-ServerUpdate([string]$Root,[switch]$NoRestart) {
    $context=[pscustomobject]@{Root=$Root;Roles=@();Gate=$null;StartupGate=$null;NoRestart=[bool]$NoRestart}
    try {
        $folder=Under $Root '.bingsang-current-update'
        [IO.Directory]::CreateDirectory($folder)|Out-Null
        $context.Gate=[IO.File]::Open((Under $folder 'automatic-operation.lock'),'OpenOrCreate','ReadWrite','None')
        foreach($role in @('cs','minecraft')){
            $record=Get-UpdateRole $Root $role
            if($record){$context.Roles+=@([pscustomobject]@{Name=$role;Before=$record})}
        }
        $processes=@(Get-UpdateProcesses $Root)
        if(!$context.Roles.Count){
            if($processes.Count){throw 'Unmanaged server or supervisor is running. No process was stopped; use its original shutdown command.'}
            return $context
        }
        foreach($name in @('Stop-Servers.ps1','Start-Servers.ps1')){
            if(!(Test-Path -LiteralPath (Under $Root ('scripts/'+$name)) -PathType Leaf)){throw "Missing original scripts/$name; cannot automatically restart safely."}
        }
        foreach($record in $context.Roles){
            if($record.Before.pid -notin @($processes|ForEach-Object {$_.ProcessId})){throw 'Server identity changed before shutdown'}
        }
        Write-Host 'Preflight passed. Saving and stopping the running servers...'
        Invoke-UpdateServerScript $Root 'Stop-Servers.ps1' 150
        # Stop-Servers waits for game PIDs. Give their supervisors time to close
        # console handles and role locks too, without repeatedly scanning payloads.
        $watch=[Diagnostics.Stopwatch]::StartNew()
        while(@(Get-UpdateProcesses $Root).Count){
            if($watch.Elapsed.TotalSeconds -gt 15){throw 'Server supervisor did not exit; update stopped before replacement.'}
            Start-Sleep -Milliseconds 250
        }
        $context.StartupGate=[IO.File]::Open((Under $Root '.state/startup.lock'),'OpenOrCreate','ReadWrite','None')
        Stopped $Root
        return $context
    }catch{
        if($context.StartupGate){$context.StartupGate.Dispose()}
        if($context.Gate){$context.Gate.Dispose()}
        throw
    }
}
function End-ServerUpdate($Context,[bool]$SafeToStart) {
    if(!$Context){return}
    try {
        if($Context.StartupGate){$Context.StartupGate.Dispose();$Context.StartupGate=$null}
        if(!$Context.Roles.Count){return}
        if(!$SafeToStart){Write-Warning 'Recovery is incomplete. Servers remain stopped; original backups were retained.';return}
        if($Context.NoRestart){Write-Host 'Servers remain stopped as requested (-NoRestart).';return}
        Write-Host 'Verified files are ready. Restarting through the original server startup script...'
        Invoke-UpdateServerScript $Context.Root 'Start-Servers.ps1' 240 -Hidden
        foreach($role in $Context.Roles){
            $now=Get-UpdateRole $Context.Root $role.Name
            if(!$now -or ($now.pid -eq $role.Before.pid -and $now.startTicks -eq $role.Before.startTicks)){
                throw "The original startup script did not restore $($role.Name). Files are retained; inspect its console."
            }
        }
        Write-Host 'The previously running servers have restarted.'
    }finally{if($Context.Gate){$Context.Gate.Dispose()}}
}
