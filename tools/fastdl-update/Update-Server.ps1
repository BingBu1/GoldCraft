param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
$lock=$null
$supported='9b0914872b2a6c282ba5ee42cd545148515fe91263adfd5425b17962ce6650f9'
function Check-Record($Record,[string]$After) {
    if($Record.scope -ne 'remote-fastdl-engine' -or $Record.path -cne 'cs/swds.dll' -or
       $Record.before -cne $supported -or $Record.after -cne $After -or
       $Record.status -notin @('prepared','installed','restored','rolled-back')){throw 'Unrecognized FastDL backup record'}
}
function Check-EngineUnlocked([string]$Path) {
    $stream=[IO.File]::Open((Plain $Path),'Open','ReadWrite','None')
    $stream.Dispose()
}
function Install-Bytes([string]$Path,[byte[]]$Content,[string]$Expected) {
    if((Hash-Bytes $Content) -cne $Expected){throw 'Staging content identity mismatch'}
    Atomic $Path '' $Content
    if((Hash $Path) -cne $Expected){throw 'Installed engine verification failed'}
}
try {
    if($Plan -and $Restore){throw 'Choose -Plan or -Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'TargetDirectory cannot be empty or whitespace'}
    $candidate=$TargetDirectory.Trim().Trim('"').Trim("'")
    if($candidate -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Enter an absolute server folder path'}
    $target=(Plain $candidate).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract the update outside the server installation'}
    foreach($relative in @('Start.cmd','server-settings.json','cs/hlds.exe','cs/swds.dll')){
        if(!(Test-Path -LiteralPath (Under $target $relative) -PathType Leaf)){throw "Select the existing server package root. Missing: $relative"}
    }
    $manifest=Text (Under $package 'update-manifest.json')|ConvertFrom-Json
    $rows=@($manifest.files)
    if($manifest.schema -ne 1 -or $manifest.scope -cne 'remote-fastdl-engine' -or $rows.Count -ne 1 -or
       $manifest.baselineSha256 -cne $supported -or $rows[0].path -cne 'cs/swds.dll' -or
       $rows[0].sha256 -cnotmatch '^[0-9a-f]{64}$' -or $rows[0].sha256 -ceq $supported -or $rows[0].bytes -le 0){throw 'Wrong engine-only update manifest'}
    $row=$rows[0];$destination=Under $target 'cs/swds.dll';$before=Hash $destination
    $state=Under $target '.bingsang-fastdl-update';$journalPath=Under $state 'transaction.json';$saved=Under $state 'previous-swds.dll'
    if($before -cne $supported -and $before -cne $row.sha256){throw 'Installed swds.dll does not match the confirmed baseline or this update; no files changed'}
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Record $journal $row.sha256}
    if($Restore){
        if(!$journal -or $journal.status -notin @('installed','prepared','restored')){throw 'No installed FastDL update is available to restore'}
        if((Hash $saved) -cne $supported){throw 'Rollback engine is missing or changed'}
    } else {
        $source=Under $package 'payload/cs/swds.dll'
        if((Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Hash $source) -cne $row.sha256){throw 'Package engine integrity failure'}
        foreach($file in Get-ChildItem -LiteralPath (Under $package 'payload') -Recurse -Force){
            [void](Plain $file.FullName)
            if(!$file.PSIsContainer -and $file.FullName -cne $source){throw 'Unlisted payload file'}
        }
        if($Plan){[ordered]@{scope=$manifest.scope;release=$manifest.release;path='cs/swds.dll';before=$before;after=$row.sha256;changed=[int]($before -cne $row.sha256);applied=$false}|ConvertTo-Json;exit 0}
    }
    Stopped $target
    Check-EngineUnlocked $destination
    [IO.Directory]::CreateDirectory($state)|Out-Null
    $lock=[IO.File]::Open((Under $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
    # Re-read after obtaining the cross-process lock, never trust the preview.
    $before=Hash $destination
    if($before -cne $supported -and $before -cne $row.sha256){throw 'Target changed while waiting for the update lock'}
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Record $journal $row.sha256}
    if($journal -and $journal.status -eq 'prepared'){
        if((Hash $saved) -cne $supported){throw 'Interrupted transaction has no valid rollback engine'}
        if($before -cne $supported){Install-Bytes $destination ([IO.File]::ReadAllBytes($saved)) $supported}
        $journal.status='rolled-back';Json $journalPath $journal;$before=$supported
    }
    if($Restore){
        if(!$journal -or $journal.status -notin @('installed','restored','rolled-back')){throw 'No installed FastDL update is available to restore'}
        if((Hash $saved) -cne $supported){throw 'Rollback engine changed'}
        if($before -cne $supported){Install-Bytes $destination ([IO.File]::ReadAllBytes($saved)) $supported}
        $journal.status='restored';Json $journalPath $journal
        Write-Host 'Previous swds.dll restored. Start with the original Start.cmd.';exit 0
    }
    if($before -ceq $row.sha256){Write-Host 'FastDL engine is already current. No files copied.';exit 0}
    $content=[IO.File]::ReadAllBytes($source)
    if($content.Length -ne $row.bytes -or (Hash-Bytes $content) -cne $row.sha256){throw 'Package changed before installation'}
    $old=[IO.File]::ReadAllBytes($destination)
    if((Hash-Bytes $old) -cne $supported){throw 'Engine baseline changed before backup'}
    # Atomic staging creates independent files even if the original target or
    # prior backup shares an immutable hard link with another installation.
    Install-Bytes $saved $old $supported
    $journal=[pscustomobject]@{scope='remote-fastdl-engine';release=$manifest.release;path='cs/swds.dll';before=$supported;after=$row.sha256;status='prepared'}
    Json $journalPath $journal
    try {
        Stopped $target;Check-EngineUnlocked $destination
        if((Hash $destination) -cne $supported){throw 'Engine changed before replacement'}
        Install-Bytes $destination $content $row.sha256
        $journal.status='installed';Json $journalPath $journal
    } catch {
        $actual=Hash $destination
        if($actual -ceq $row.sha256){Install-Bytes $destination $old $supported}
        elseif($actual -cne $supported){throw 'Engine changed outside this transaction; rollback retained for inspection'}
        $journal.status='rolled-back';Json $journalPath $journal
        throw
    }
    Write-Host 'Updated cs/swds.dll. One verified rollback copy is retained.'
    Write-Host 'Restart using the original Start.cmd. Server settings and other runtime files were not modified.'
} catch {Write-Error $_ -ErrorAction Continue;exit 1} finally {if($lock){$lock.Dispose()}}
