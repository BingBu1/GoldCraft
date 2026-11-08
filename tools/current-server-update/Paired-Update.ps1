param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
. (Join-Path $PSScriptRoot 'Paired-Publication.ps1')
$lock=$null;$publicationLock=$null
$scope='paired-managed-delta'
function Check-Row($Row) {
    if($Row.path -cnotmatch '^(cs/(swds\.dll|filesystem_stdio\.dll|cstrike/dlls/mp\.dll)|cs/launcher-public/(manifest\.json|blobs/[0-9a-f]{64}/[A-Za-z0-9_-]+\.dll))$' -or
       $Row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $Row.bytes -le 0){throw 'Invalid paired update path or identity'}
    [void](Under $target $Row.path)
    foreach($digest in $Row.priorSha256){if($digest -cnotmatch '^[0-9a-f]{64}$'){throw 'Invalid baseline identity'}}
}
function Changes {
    foreach($row in $manifest.files){
        $before=Hash (Under $target $row.path)
        if($before -ceq $row.sha256){continue}
        if($before -and $before -cnotin $row.priorSha256){throw "Unknown existing file; preserved: $($row.path)"}
        if(!$before -and !$row.path.StartsWith('cs/launcher-public/blobs/')){throw "Missing existing server file: $($row.path)"}
        [pscustomobject]@{path=$row.path;before=$before;after=$row.sha256;existed=($null -ne $before)}
    }
}
function Unlocked($Rows) {
    foreach($row in $Rows){
        $path=Under $target $row.path
        if(Test-Path -LiteralPath $path -PathType Leaf){$f=[IO.File]::Open($path,'Open','ReadWrite','None');$f.Dispose()}
    }
}
function Check-Journal($Journal) {
    if($Journal.scope -cne $scope -or $Journal.sequence -ne $manifest.sequence -or $Journal.manifestSha256 -cne $manifestHash -or
       $Journal.status -notin @('prepared','installed','restored') -or !@($Journal.files).Count){throw 'Invalid recovery record'}
    $seen=@{}
    foreach($row in $Journal.files){
        if(!$files.ContainsKey($row.path) -or $seen.ContainsKey($row.path) -or $row.after -cne $files[$row.path].sha256){throw 'Invalid recovery path'}
        $seen[$row.path]=$true
        if($row.existed){
            if($row.before -cnotin $files[$row.path].priorSha256 -or (Hash (Under $state ('files/'+$row.path))) -cne $row.before){throw 'Backup missing or changed'}
        }elseif($null -ne $row.before -or !$row.path.StartsWith('cs/launcher-public/blobs/')){throw 'Invalid absent preimage'}
    }
}
try {
    if($Plan -and $Restore){throw 'Choose Plan or Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'Enter the existing server root'}
    $entered=$TargetDirectory.Trim().Trim('"').Trim("'")
    if($entered -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Use an absolute server folder'}
    $target=(Plain $entered).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract the update outside the server folder'}
    foreach($name in @('Start.cmd','cs/hlds.exe','cs/swds.dll','cs/filesystem_stdio.dll','cs/cstrike/dlls/mp.dll')){
        if(!(Test-Path -LiteralPath (Under $target $name) -PathType Leaf)){throw "Missing existing server file: $name"}
    }
    $manifestPath=Under $package 'update-manifest.json';$manifestHash=Hash $manifestPath
    $manifest=Text $manifestPath|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.scope -cne $scope -or $manifest.sequence -le $manifest.baselineSequence -or
       $manifest.sequence -notmatch '^\d{10}$' -or !@($manifest.files).Count){throw 'Wrong paired update manifest'}
    $files=@{};$all=@{}
    foreach($row in @($manifest.files)+@($manifest.requiredBaseline)){
        Check-Row $row
        if($all.ContainsKey($row.path)){throw 'Duplicate update path'}
        $all[$row.path]=$row
    }
    foreach($row in $manifest.files){$files[$row.path]=$row}
    foreach($name in @('cs/swds.dll','cs/filesystem_stdio.dll','cs/cstrike/dlls/mp.dll','cs/launcher-public/manifest.json')){
        if(!$all.ContainsKey($name)){throw 'Missing required paired component'}
    }
    if(!$files.ContainsKey('cs/launcher-public/manifest.json')){throw 'Missing signed publication'}
    $state=Under $target ('.bingsang-paired-update/'+$manifest.sequence);$journalPath=Under $state 'transaction.json'
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal}
    if(!$Plan){
        Stopped $target
        Unlocked @($all.Values|Where-Object {$_.path.EndsWith('.dll')})
    }
    if($Restore){
        if(!$journal){throw 'No original rollback record exists'}
        [IO.Directory]::CreateDirectory((Under $target '.bingsang-launcher-update'))|Out-Null
        $publicationLock=[IO.File]::Open((Under $target '.bingsang-launcher-update/operation.lock'),'OpenOrCreate','ReadWrite','None')
        $lock=[IO.File]::Open((Under $target '.bingsang-paired-update/operation.lock'),'OpenOrCreate','ReadWrite','None')
        $journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal;Unlocked $journal.files
        Restore-Files $target $state $journal
        Write-Host 'Previous engine and signed publication restored. Restart with the original Start.cmd.';exit 0
    }
    Write-Host 'Checking paired engine/client delta...'
    $allowed=@{}
    foreach($row in $manifest.files){
        $source=Under $package ('payload/'+$row.path);$allowed[$source]=$true
        if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Hash $source) -cne $row.sha256){throw "Corrupt payload: $($row.path)"}
    }
    foreach($entry in Get-ChildItem -LiteralPath (Under $package 'payload') -Recurse -Force){
        [void](Plain $entry.FullName)
        if(!$entry.PSIsContainer -and !$allowed.ContainsKey($entry.FullName)){throw 'Unlisted payload file'}
    }
    Check-PairedPublication
    foreach($row in $manifest.requiredBaseline){
        $path=Under $target $row.path
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $row.bytes -or (Hash $path) -cne $row.sha256){throw "Missing/changed prerequisite: $($row.path). Apply the required baseline package first."}
    }
    $rows=@(Changes)
    if($journal -and $journal.status -eq 'installed' -and $rows.Count){throw 'Installed files changed; original backup retained. Restore before reapplying.'}
    if($Plan){[ordered]@{sequence=$manifest.sequence;changed=$rows.Count;baselineVerified=@($manifest.requiredBaseline).Count;applied=$false}|ConvertTo-Json;exit 0}
    if(!$rows.Count -and (!$journal -or $journal.status -ne 'prepared')){Write-Host 'Already current; no files copied.';exit 0}
    Stopped $target;Unlocked $rows
    [IO.Directory]::CreateDirectory($state)|Out-Null
    [IO.Directory]::CreateDirectory((Under $target '.bingsang-launcher-update'))|Out-Null
    $publicationLock=[IO.File]::Open((Under $target '.bingsang-launcher-update/operation.lock'),'OpenOrCreate','ReadWrite','None')
    $lock=[IO.File]::Open((Under $target '.bingsang-paired-update/operation.lock'),'OpenOrCreate','ReadWrite','None')
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal}
    if($journal -and $journal.status -eq 'prepared'){Restore-Files $target $state $journal}
    Check-PairedPublication
    $rows=@(Changes)
    if($journal -and $journal.status -eq 'installed' -and $rows.Count){throw 'Installed transaction changed; restore before reapplying'}
    if(!$rows.Count){Write-Host 'Already current; no files copied.';exit 0}
    foreach($row in $rows){if($row.existed){
        $saved=Under $state ('files/'+$row.path);Atomic $saved (Under $target $row.path) $null
        if((Hash $saved) -cne $row.before){throw 'Destination changed during backup'}
    }}
    $journal=[pscustomobject]@{scope=$scope;sequence=$manifest.sequence;manifestSha256=$manifestHash;status='prepared';files=$rows}
    Json $journalPath $journal
    try {
        Stopped $target;Unlocked $rows
        # All blobs and engine bytes are complete before publishing the new contract.
        foreach($row in @($rows|Sort-Object @{Expression={if($_.path -ceq 'cs/launcher-public/manifest.json'){2}elseif($_.path -ceq 'cs/swds.dll'){1}else{0}}},path)){
            $dest=Under $target $row.path;$source=Under $package ('payload/'+$row.path)
            if((Hash $dest) -cne $row.before -or (Hash $source) -cne $row.after){throw 'File changed before replacement'}
            Atomic $dest $source $null
            if((Hash $dest) -cne $row.after){throw 'Installed identity mismatch'}
        }
        $journal.status='installed';Json $journalPath $journal
    }catch{Restore-Files $target $state $journal;throw}
    Write-Host "Updated $($rows.Count) files in one transaction. All existing settings preserved."
    Write-Host 'Restart the server with the original Start.cmd. Protocol-2 players keep their current Bingsang Launcher EXE.'
}catch{Write-Error ($_.ToString()+"`n"+$_.ScriptStackTrace) -ErrorAction Continue;exit 1}
finally{if($lock){$lock.Dispose()};if($publicationLock){$publicationLock.Dispose()}}
