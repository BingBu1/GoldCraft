param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
. (Join-Path $PSScriptRoot 'Publication.ps1')
$lock=$null
$oldMp='a3e32ab1fe182525c01129adbf455f2a423bde44d6a5e24bf9e53862f0258ec5'
$newMp='cfd2e0968a7e8f303bd1ee8d037a2ba58fd8cbd7f0fed381c682fba200c827b5'
$engineSha='6839933a4dfe5dc8b36185406eddaa15e4957b57008120844886de6ba00a3e3f'
$mpPath='cs/cstrike/dlls/mp.dll'
$manifestPath='cs/launcher-public/manifest.json'
$clientSha='6881e8b9ce604b3e529253eb5730fe6983e4dde4058960a91880ed5da167e187'
$blobPath='cs/launcher-public/blobs/'+$clientSha+'/CSOLSupport.dll'
$oldManifest='a553c6f82cdbd477c9d22f985584cc9250c329bfa9dc0642107c89e3e6f2295c'
$beforeHashes=@{$mpPath=$oldMp;$manifestPath=$oldManifest}
function Checked-Hash($Row) {
    if($Row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $Row.bytes -le 0){throw 'Invalid file identity in manifest'}
}
function Check-Journal($Journal,$Files,[string]$Backup) {
    if($Journal.scope -cne 'remote-respawn-signed' -or $Journal.release -cne $manifest.release -or
       $Journal.status -notin @('prepared','installed','restored')){throw 'Unrecognized rollback record'}
    $seen=@{}
    foreach($row in $Journal.files){
        if(!$Files.ContainsKey($row.path) -or $seen.ContainsKey($row.path) -or
           $row.after -cne $Files[$row.path].sha256){throw 'Rollback record does not match this package'}
        [void](Under $target $row.path);$seen[$row.path]=$true
        if($row.existed){
            if(!$beforeHashes.ContainsKey($row.path) -or $row.before -cne $beforeHashes[$row.path]){throw 'Unexpected original file in rollback record'}
            if((Hash (Under $Backup ('files/'+$row.path))) -cne $row.before){throw 'Verified previous file is missing or changed'}
        }elseif($null -ne $row.before){throw 'Invalid absent original file'}
    }
    if(!$seen.Count -or $seen.Count -gt 80){throw 'Incomplete rollback record'}
}
function Check-Engine {
    if((Hash (Under $target 'cs/swds.dll')) -cne $engineSha){
        throw 'Install the preceding 20261101-FastDL server fix first. This update preserves that exact swds.dll.'
    }
}
function Changes($Files) {
    $result=@(foreach($name in @($Files.Keys|Sort-Object)){
        $before=Hash (Under $target $name);$after=$Files[$name].sha256
        if($beforeHashes.ContainsKey($name)){
            if($before -cne $beforeHashes[$name] -and $before -cne $after){throw 'Installed game DLL or signed manifest is not the confirmed old version or this exact update'}
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
    if($manifest.schema -ne 2 -or $manifest.scope -cne 'remote-respawn-signed' -or
       $manifest.baselineMpSha256 -cne $oldMp -or $manifest.engineSha256 -cne $engineSha -or
       @($manifest.files).Count -ne 80 -or @($manifest.requiredBaseline).Count -lt 1){throw 'Wrong Scenario revival update manifest'}
    $files=@{};$allowed=@{};$baselineNames=@{}
    foreach($row in $manifest.files){
        Checked-Hash $row
        if($files.ContainsKey($row.path)){throw 'Duplicate payload path'}
        if($row.path -ceq $mpPath){
            if($row.sha256 -cne $newMp){throw 'Wrong candidate game DLL'}
        }elseif($row.path -ceq $blobPath){
            if($row.sha256 -cne $clientSha){throw 'Wrong client DLL'}
        }elseif($row.path -ceq $manifestPath){
            # The signed envelope is checked against the embedded release public key below.
        }elseif($row.path -cnotmatch '^cs/cstrike/(resource/.+\.tga|sound/.+\.wav|sprites/zbs_level\.spr)$'){
            throw 'Payload contains a path outside this update scope'
        }
        [void](Under $target $row.path);$source=Under $package ('payload/'+$row.path)
        if(!(Test-Path -LiteralPath $source -PathType Leaf) -or
           (Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Hash $source) -cne $row.sha256){throw "Package integrity failure: $($row.path)"}
        $files[$row.path]=$row;$allowed[$source]=$true
    }
    if(!$files.ContainsKey($mpPath) -or !$files.ContainsKey($blobPath) -or !$files.ContainsKey($manifestPath)){throw 'Incomplete paired release'}
    Check-Publication $package $target $manifestPath $blobPath $clientSha
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
    $state=Under $target '.bingsang-respawn-release'
    $journalPath=Under $state 'transaction.json'
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal $files $state}
    if($Plan){
        [ordered]@{scope=$manifest.scope;release=$manifest.release;changed=$rows.Count;unchanged=(80-$rows.Count);
            baselineVerified=$baselineNames.Count;pendingRecovery=($journal -and $journal.status -eq 'prepared');
            applied=$false;paths=@($rows|ForEach-Object {$_.path})}|ConvertTo-Json -Depth 6
        exit 0
    }
    if($Restore -and !$journal){throw 'No verified rollback record is available'}
    if(!$Restore -and !$rows.Count -and (!$journal -or $journal.status -ne 'prepared')){
        Write-Host 'Already current. No runtime or rollback files changed.';exit 0
    }
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
        Write-Host 'Previous game DLL, signed publication and resources restored. Start using the original Start.cmd.';exit 0
    }
    $rows=@(Changes $files)
    if(!$rows.Count){Write-Host 'Already current. No files copied.';exit 0}
    foreach($row in $rows){
        if($row.existed){
            $saved=Under $state ('files/'+$row.path)
            Atomic $saved (Under $target $row.path) $null
            if((Hash $saved) -cne $row.before){throw 'Original file changed before backup completed'}
        }
    }
    $journal=[pscustomobject]@{scope=$manifest.scope;release=$manifest.release;status='prepared';files=$rows}
    Json $journalPath $journal
    try {
        Stopped $target
        # Resources and the immutable client blob precede the GameDLL; publish its signed manifest last.
        foreach($row in @($rows|Sort-Object @{Expression={if($_.path -ceq $manifestPath){2}elseif($_.path -ceq $mpPath){1}else{0}}},path)){
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
    Write-Host 'Restart with the original Start.cmd. Players receive the matching client DLL when starting with Bingsang Launcher.'
}catch{Write-Error ($_.ToString()+"`n"+$_.ScriptStackTrace) -ErrorAction Continue;exit 1}finally{if($lock){$lock.Dispose()}}
