param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
$lock=$null
$oldMp='a3e32ab1fe182525c01129adbf455f2a423bde44d6a5e24bf9e53862f0258ec5'
$newMp='cfd2e0968a7e8f303bd1ee8d037a2ba58fd8cbd7f0fed381c682fba200c827b5'
$engineSha='6839933a4dfe5dc8b36185406eddaa15e4957b57008120844886de6ba00a3e3f'
$mpPath='cs/cstrike/dlls/mp.dll'
function Checked-Hash($Row) {
    if($Row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $Row.bytes -le 0){throw 'Invalid file identity in manifest'}
}
function Check-Journal($Journal,$Files,[string]$Backup) {
    if($Journal.scope -cne 'remote-scenario-respawn' -or $Journal.release -cne $manifest.release -or
       $Journal.status -notin @('prepared','installed','restored')){throw 'Unrecognized rollback record'}
    $seen=@{};$hasMp=$false
    foreach($row in $Journal.files){
        if(!$Files.ContainsKey($row.path) -or $seen.ContainsKey($row.path) -or
           $row.after -cne $Files[$row.path].sha256){throw 'Rollback record does not match this package'}
        [void](Under $target $row.path);$seen[$row.path]=$true
        if($row.path -ceq $mpPath){
            if(!$row.existed -or $row.before -cne $oldMp){throw 'Wrong original game DLL in rollback record'}
            $hasMp=$true
        }elseif($row.existed -or $null -ne $row.before){throw 'Unexpected preexisting resource in rollback record'}
    }
    if(!$hasMp -or $seen.Count -gt 78){throw 'Incomplete rollback record'}
    if((Hash (Under $Backup ('files/'+$mpPath))) -cne $oldMp){throw 'Verified previous game DLL is missing or changed'}
}
function Check-Engine {
    if((Hash (Under $target 'cs/swds.dll')) -cne $engineSha){
        throw 'Install the preceding 20261101-FastDL server fix first. This update preserves that exact swds.dll.'
    }
}
function Changes($Files) {
    $result=@(foreach($name in @($Files.Keys|Sort-Object)){
        $before=Hash (Under $target $name);$after=$Files[$name].sha256
        if($name -ceq $mpPath){
            if($before -cne $oldMp -and $before -cne $newMp){throw 'Installed mp.dll is not the confirmed old version or this exact update'}
        }elseif($null -ne $before -and $before -cne $after){
            throw "Existing resource differs from the confirmed update; nothing will overwrite it: $name"
        }
        if($before -cne $after){[pscustomobject]@{path=$name;existed=($null -ne $before);before=$before;after=$after}}
    })
    return $result
}
try {
    if($Plan -and $Restore){throw 'Choose -Plan or -Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'TargetDirectory cannot be empty or whitespace. Omit -TargetDirectory to use E:\cs_MineCraft\GoldCraft-Servers.'}
    $entered=$TargetDirectory.Trim().Trim('"').Trim("'")
    if([string]::IsNullOrWhiteSpace($entered)){throw 'TargetDirectory cannot be empty or whitespace. Omit -TargetDirectory to use E:\cs_MineCraft\GoldCraft-Servers.'}
    if($entered -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Enter an absolute server folder path'}
    $target=(Plain $entered).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract this update outside the server installation'}
    foreach($name in @('Start.cmd','server-settings.json','cs/hlds.exe','cs/swds.dll',$mpPath)){
        if(!(Test-Path -LiteralPath (Under $target $name) -PathType Leaf)){throw "Select the existing server package root. Missing: $name"}
    }
    $manifest=Text (Under $package 'update-manifest.json')|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.scope -cne 'remote-scenario-respawn' -or
       $manifest.baselineMpSha256 -cne $oldMp -or $manifest.engineSha256 -cne $engineSha -or
       @($manifest.files).Count -ne 78 -or @($manifest.requiredBaseline).Count -lt 1){throw 'Wrong Scenario revival update manifest'}
    $files=@{};$allowed=@{};$baselineNames=@{}
    foreach($row in $manifest.files){
        Checked-Hash $row
        if($files.ContainsKey($row.path)){throw 'Duplicate payload path'}
        if($row.path -ceq $mpPath){
            if($row.sha256 -cne $newMp){throw 'Wrong candidate game DLL'}
        }elseif($row.path -cnotmatch '^cs/cstrike/(resource/.+\.tga|sound/.+\.wav|sprites/zbs_level\.spr)$'){
            throw 'Payload contains a path outside this update scope'
        }
        [void](Under $target $row.path);$source=Under $package ('payload/'+$row.path)
        if(!(Test-Path -LiteralPath $source -PathType Leaf) -or
           (Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Hash $source) -cne $row.sha256){throw "Package integrity failure: $($row.path)"}
        $files[$row.path]=$row;$allowed[$source]=$true
    }
    if(!$files.ContainsKey($mpPath)){throw 'Game DLL missing from payload'}
    foreach($entry in Get-ChildItem -LiteralPath (Under $package 'payload') -Recurse -Force){
        [void](Plain $entry.FullName)
        if(!$entry.PSIsContainer -and !$allowed.ContainsKey($entry.FullName)){throw 'Unlisted payload file'}
    }
    Check-Engine
    foreach($row in $manifest.requiredBaseline){
        Checked-Hash $row
        if($row.path -notmatch '^cs/cstrike/' -or $row.path -match '(?i)\.(dll|exe|amxx|pdb)$' -or
           $files.ContainsKey($row.path) -or $baselineNames.ContainsKey($row.path)){throw 'Invalid omitted baseline resource'}
        $baselineNames[$row.path]=$true;$path=Under $target $row.path
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or
           (Get-Item -LiteralPath $path).Length -ne $row.bytes -or (Hash $path) -cne $row.sha256){
            throw "Missing or changed baseline resource. Install the Scenario and Resources-v3 packages first: $($row.path)"
        }
    }
    $rows=@(Changes $files)
    $state=Under $target '.bingsang-scenario-respawn-update'
    $journalPath=Under $state 'transaction.json'
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal $files $state}
    if($Plan){
        [ordered]@{scope=$manifest.scope;release=$manifest.release;changed=$rows.Count;unchanged=(78-$rows.Count);
            baselineVerified=$baselineNames.Count;pendingRecovery=($journal -and $journal.status -eq 'prepared');
            applied=$false;paths=@($rows|ForEach-Object {$_.path})}|ConvertTo-Json -Depth 6
        exit 0
    }
    if($Restore -and !$journal){throw 'No verified rollback record is available'}
    if(!$Restore -and !$rows.Count -and (!$journal -or $journal.status -ne 'prepared')){
        Write-Host 'Already current. No runtime or rollback files changed.';exit 0
    }
    if(!$Restore -and $rows.Count -and (Hash (Under $target $mpPath)) -ceq $newMp -and
       (!$journal -or $journal.status -ne 'prepared')){throw 'Partial installation has no prepared recovery record; restore the confirmed baseline first'}
    Stopped $target
    [IO.Directory]::CreateDirectory($state)|Out-Null
    $lock=[IO.File]::Open((Under $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
    Check-Engine
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal $files $state}
    if($journal -and $journal.status -eq 'prepared'){
        Restore-Files $target $state $journal
    }
    if($Restore){
        if(!$journal){throw 'Rollback record disappeared'}
        if($journal.status -ne 'restored'){Restore-Files $target $state $journal}
        Write-Host 'Previous game DLL and resource state restored. The FastDL engine and settings are unchanged.';exit 0
    }
    $rows=@(Changes $files)
    if(!$rows.Count){Write-Host 'Already current. No files copied.';exit 0}
    if((Hash (Under $target $mpPath)) -cne $oldMp){throw 'Game DLL baseline changed while waiting for the lock'}
    $saved=Under $state ('files/'+$mpPath)
    Atomic $saved (Under $target $mpPath) $null
    if((Hash $saved) -cne $oldMp){throw 'Game DLL changed before backup completed'}
    $journal=[pscustomobject]@{scope=$manifest.scope;release=$manifest.release;status='prepared';files=$rows}
    Json $journalPath $journal
    try {
        Stopped $target
        # All assets are present before the new GameDLL becomes visible.
        foreach($row in @($rows|Sort-Object @{Expression={if($_.path -ceq $mpPath){1}else{0}}},path)){
            $destination=Under $target $row.path
            if((Hash $destination) -cne $row.before){throw "Destination changed during update: $($row.path)"}
            $source=Under $package ('payload/'+$row.path)
            if((Hash $source) -cne $row.after){throw "Package changed during update: $($row.path)"}
            Atomic $destination $source $null
            if((Hash $destination) -cne $row.after){throw 'Installed file verification failed'}
        }
        $journal.status='installed';Json $journalPath $journal
    }catch{Restore-Files $target $state $journal;throw}
    Write-Host "Updated $($rows.Count) files. One verified rollback record is retained."
    Write-Host 'Restart with the original Start.cmd. Client HUD requires the separately released matching client plugin.'
}catch{Write-Error $_ -ErrorAction Continue;exit 1}finally{if($lock){$lock.Dispose()}}
