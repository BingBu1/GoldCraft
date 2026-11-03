param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
$lock=$null
$scope='remote-engine-filesystem'
$engineVersions=@(
    '9b0914872b2a6c282ba5ee42cd545148515fe91263adfd5425b17962ce6650f9',
    '6839933a4dfe5dc8b36185406eddaa15e4957b57008120844886de6ba00a3e3f',
    'e4c69d7eeb70a75c201f63c0d7cde9faaa94596de4b292c24bad58d693123f8d')
$gameVersions=@(
    'a3e32ab1fe182525c01129adbf455f2a423bde44d6a5e24bf9e53862f0258ec5',
    'cfd2e0968a7e8f303bd1ee8d037a2ba58fd8cbd7f0fed381c682fba200c827b5',
    'cbb609e0f768b3d313e5694bc7fd233f179f430fe46eec366449e9a4680ab69a')
$paths=@('cs/filesystem_stdio.dll','cs/swds.dll')
function Check-PE([string]$Path) {
    $b=[IO.File]::ReadAllBytes((Plain $Path))
    if($b.Length -lt 64 -or $b[0] -ne 77 -or $b[1] -ne 90){throw 'Expected an existing x86 DLL'}
    $offset=[BitConverter]::ToInt32($b,60)
    if($offset -lt 64 -or $offset -gt $b.Length-24 -or
       [BitConverter]::ToUInt32($b,$offset) -ne 17744 -or
       [BitConverter]::ToUInt16($b,$offset+4) -ne 332 -or
       !([BitConverter]::ToUInt16($b,$offset+22) -band 8192)){throw 'Expected an existing x86 DLL'}
}
function Changes {
    $engine=Hash (Under $target 'cs/swds.dll')
    if($engine -cnotin $engineVersions -and $engine -cne $files['cs/swds.dll'].sha256){throw 'Unsupported engine baseline; no update applied'}
    if((Hash (Under $target 'cs/cstrike/dlls/mp.dll')) -cnotin $gameVersions){throw 'Unsupported scenario GameDLL; no update applied'}
    foreach($name in $paths){
        $dest=Under $target $name;Check-PE $dest
        $before=Hash $dest;$after=$files[$name].sha256
        if($before -cne $after){[pscustomobject]@{path=$name;existed=$true;before=$before;after=$after}}
    }
}
function Check-Journal($Journal) {
    if($Journal.scope -cne $scope -or $Journal.release -cne $manifest.release -or
       $Journal.status -notin @('prepared','installed','restored') -or
       @($Journal.files).Count -notin @(1,2)){throw 'Invalid engine rollback record'}
    $seen=@{}
    foreach($row in $Journal.files){
        if(!$files.ContainsKey($row.path) -or $seen.ContainsKey($row.path) -or !$row.existed -or
           $row.before -cnotmatch '^[0-9a-f]{64}$' -or $row.after -cne $files[$row.path].sha256){throw 'Invalid rollback identity'}
        $seen[$row.path]=$true
        if($row.path -ceq 'cs/swds.dll' -and $row.before -cnotin $engineVersions){throw 'Invalid engine rollback baseline'}
        if((Hash (Under $state ('files/'+$row.path))) -cne $row.before){throw 'Rollback file missing or changed'}
    }
}
function Unlocked {
    foreach($name in $paths){
        $f=[IO.File]::Open((Under $target $name),'Open','ReadWrite','None');$f.Dispose()
    }
}
try {
    if($Plan -and $Restore){throw 'Choose -Plan or -Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'TargetDirectory cannot be empty'}
    $entered=$TargetDirectory.Trim().Trim('"').Trim("'")
    if($entered -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Enter an absolute server root'}
    $target=(Plain $entered).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract this update outside the server folder'}
    foreach($name in @('Start.cmd','server-settings.json','cs/hlds.exe','cs/cstrike/dlls/mp.dll')+$paths){
        if(!(Test-Path -LiteralPath (Under $target $name) -PathType Leaf)){throw "Missing server file: $name"}
    }
    $manifest=Text (Under $package 'update-manifest.json')|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.scope -cne $scope -or @($manifest.files).Count -ne 2){throw 'Wrong engine/provider manifest'}
    $files=@{};$allowed=@{}
    foreach($row in $manifest.files){
        if($row.path -cnotin $paths -or $files.ContainsKey($row.path) -or
           $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0){throw 'Invalid engine/provider payload'}
        $files[$row.path]=$row;$source=Under $package ('payload/'+$row.path);$allowed[$source]=$true
        if(!$Restore){
            if((Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Hash $source) -cne $row.sha256){throw 'Payload integrity failure'}
            Check-PE $source
        }
    }
    if(!$Restore){foreach($entry in Get-ChildItem -LiteralPath (Under $package 'payload') -Recurse -Force){
        [void](Plain $entry.FullName)
        if(!$entry.PSIsContainer -and !$allowed.ContainsKey($entry.FullName)){throw 'Unlisted payload file'}
    }}
    $rows=@(Changes);$state=Under $target '.bingsang-engine-update';$journalPath=Under $state 'transaction.json'
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal}
    if($Plan){[ordered]@{release=$manifest.release;changed=$rows.Count;unchanged=(2-$rows.Count);applied=$false;
        pendingRecovery=($journal -and $journal.status -eq 'prepared');paths=@($rows|ForEach-Object {$_.path})}|ConvertTo-Json -Depth 5;exit 0}
    if(!$Restore -and !$rows.Count -and (!$journal -or $journal.status -ne 'prepared')){Write-Host 'Already current. No files copied.';exit 0}
    if($Restore -and !$journal){throw 'No rollback record available'}
    Stopped $target;Unlocked
    [IO.Directory]::CreateDirectory($state)|Out-Null
    $lock=[IO.File]::Open((Under $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal}
    $rows=@(Changes)
    if($journal -and $journal.status -eq 'prepared'){Restore-Files $target $state $journal}
    if($Restore){
        if(!$journal){throw 'Rollback record disappeared'}
        Restore-Files $target $state $journal
        Write-Host 'Previous engine and filesystem restored. Use the original Start.cmd.';exit 0
    }
    $rows=@(Changes)
    if(!$rows.Count){Write-Host 'Already current. No files copied.';exit 0}
    foreach($row in $rows){
        $saved=Under $state ('files/'+$row.path);Atomic $saved (Under $target $row.path) $null
        if((Hash $saved) -cne $row.before){throw 'Original changed during backup'}
    }
    $journal=[pscustomobject]@{scope=$scope;release=$manifest.release;status='prepared';files=$rows}
    Json $journalPath $journal
    try {
        Stopped $target;Unlocked
        foreach($row in $rows){
            $destination=Under $target $row.path;$source=Under $package ('payload/'+$row.path)
            if((Hash $destination) -cne $row.before -or (Hash $source) -cne $row.after){throw 'File changed before replacement'}
            Atomic $destination $source $null
            if((Hash $destination) -cne $row.after){throw 'Installed DLL verification failed'}
        }
        $journal.status='installed';Json $journalPath $journal
    }catch{Restore-Files $target $state $journal;throw}
    Write-Host "Updated $($rows.Count) DLLs; skipped $(2-$rows.Count) identical files."
    Write-Host 'Settings and resources preserved. Restart with the original Start.cmd.'
}catch{Write-Error ($_.ToString()+"`n"+$_.ScriptStackTrace) -ErrorAction Continue;exit 1}finally{if($lock){$lock.Dispose()}}
