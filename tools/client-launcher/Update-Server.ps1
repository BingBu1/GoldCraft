param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'Files.ps1'
if(!(Test-Path -LiteralPath $helper)){$helper=Join-Path $PSScriptRoot '../scenario-update/Files.ps1'}
. $helper
$lock=$null
function Signed([string]$Path){
    $envelope=Text $Path|ConvertFrom-Json;$bytes=[Convert]::FromBase64String($envelope.payload)
    $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider;$rsa.PersistKeyInCsp=$false
    try{
        $rsa.FromXmlString((Text (Under $PSScriptRoot 'release-public-key.xml')))
        if(!$rsa.VerifyData($bytes,'SHA256',[Convert]::FromBase64String($envelope.signature))){throw 'Invalid release signature'}
    }finally{$rsa.Dispose()}
    return ([Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json)
}
try{
    if($Plan -and $Restore){throw 'Choose Plan or Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'Enter the existing server root'}
    $entered=$TargetDirectory.Trim().Trim('"').Trim("'")
    if($entered -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Use an absolute server folder'}
    $target=(Plain $entered).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract the update outside the server folder'}
    foreach($rel in @('Start.cmd','cs/hlds.exe')){if(!(Test-Path -LiteralPath (Under $target $rel) -PathType Leaf)){throw "Wrong server folder: missing $rel"}}
    if((Hash (Under $package 'release-public-key.xml')) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Wrong public key'}
    $source=Under $package 'payload/cs/launcher-public/manifest.json';$after=Hash $source
    if((Get-Item -LiteralPath $source).Length -gt 1MB){throw 'Manifest too large for HTTP server'}
    $release=Signed $source
    if($release.schema -ne 2 -or $release.sequence -lt 1 -or $release.bundleId -cne 'ac689085e776441828892db90bd22f09c9ee21c80e0318630ac3ea096c796406' -or @($release.files).Count -lt 19 -or @($release.files).Count -gt 128){throw 'Wrong managed release channel'}
    $baselinePath=Under $package 'baseline-manifest.json';$baseline=$null;$baselineHash=$null;$baselineFiles=@{}
    if(Test-Path -LiteralPath $baselinePath){
        $baseline=Signed $baselinePath;$baselineHash=Hash $baselinePath
        if($baseline.schema -ne 2 -or $baseline.bundleId -cne $release.bundleId -or $baseline.sequence -ge $release.sequence){throw 'Invalid incremental baseline'}
        foreach($row in $baseline.files){if($baselineFiles.ContainsKey($row.path)){throw 'Duplicate baseline core'};$baselineFiles[$row.path]=$row}
    }
    $dest=Under $target 'cs/launcher-public/manifest.json';$before=Hash $dest
    if($null -ne $baseline -and $before -cne $baselineHash -and $before -cne $after){throw "This incremental update requires installed launcher publication $($baseline.sequence). Install that server package first."}
    if($null -ne $before){
        $previous=Signed $dest
        if($previous.bundleId -cne $release.bundleId -and $previous.bundleId -cne '24af5cde87712fddd2af140f239cd3bda982e0dbf991e3cbc5cf3ce255483005'){throw 'Incompatible installed publication'}
        if($previous.sequence -gt $release.sequence -or ($previous.sequence -eq $release.sequence -and $before -cne $after)){throw 'Refusing an older release or a changed same-sequence manifest'}
    }
    $seen=@{};$blobs=@()
    foreach($row in $release.files){
        if($row.path -cnotmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or $seen.ContainsKey($row.path) -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0 -or $row.bytes -ge 16MB){throw 'Invalid DLL record'}
        [void](Under $target $row.path);$seen[$row.path]=$true
        $rel='cs/launcher-public/blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path)
        $src=Under $package ('payload/'+$rel);$file=Under $target $rel;$existing=Hash $file
        if(Test-Path -LiteralPath $src -PathType Leaf){
            if((Hash $src) -cne $row.sha256 -or (Get-Item -LiteralPath $src).Length -ne $row.bytes){throw 'Packaged DLL is corrupt'}
        }else{
            $old=$baselineFiles[$row.path]
            if($null -eq $old -or $old.sha256 -cne $row.sha256 -or $old.bytes -ne $row.bytes){throw 'Packaged DLL is missing and is not part of the verified incremental baseline'}
            if($existing -cne $row.sha256 -or (Get-Item -LiteralPath $file).Length -ne $row.bytes){throw "Incremental baseline DLL is missing or corrupt: $($row.path). Reinstall the baseline server package first."}
        }
        if($null -ne $existing -and $existing -cne $row.sha256){throw 'Existing immutable blob changed'}
        $blobs+=@([pscustomobject]@{source=$src;target=$file;sha256=$row.sha256;missing=($null -eq $existing)})
    }
    if($Plan){Write-Output "Verified sequence $($release.sequence): $($blobs.Count) DLLs and $($release.resources.Count) signed resources. No files changed.";exit 0}
    $state=Under $target ('.bingsang-launcher-update/'+$release.sequence)
    [IO.Directory]::CreateDirectory($state)|Out-Null
    $lock=[IO.File]::Open((Under $target '.bingsang-launcher-update/operation.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    if((Hash $dest) -cne $before){throw 'Publication changed during verification'}
    $record=Under $state 'transaction.json';$backup=Under $state 'manifest.before.json';$journal=$null
    if(Test-Path -LiteralPath $record){
        $journal=Text $record|ConvertFrom-Json
        if($journal.scope -cne 'managed-launcher-v2' -or $journal.after -cne $after -or $journal.status -notin @('prepared','installed','restored')){throw 'Invalid recovery record'}
        if($null -ne $journal.before){
            if($journal.before -cnotmatch '^[0-9a-f]{64}$' -or (Hash $backup) -cne $journal.before){throw 'Backup integrity failure'}
            $saved=Signed $backup
            if($saved.sequence -ge $release.sequence){throw 'Backup is not an earlier publication'}
        }
    }
    if($Restore){
        if($null -eq $journal){throw 'No original backup exists'}
        if($before -cne $after -and $before -cne $journal.before){throw 'Publication changed after this update'}
        if($before -cne $journal.before){if($null -eq $journal.before){[IO.File]::Delete($dest)}else{Atomic $dest $backup $null}}
        $journal.status='restored';Json $record $journal
        Write-Output 'Previous signed manifest restored. Immutable DLL blobs retained.';exit 0
    }
    if($before -ceq $after){
        foreach($blob in $blobs){if($blob.missing){Atomic $blob.target $blob.source $null}}
        Write-Output 'Launcher publication already current; all DLLs verified.';exit 0
    }
    if($null -ne $journal -and $journal.before -cne $before){throw 'Publication differs from the recovery record'}
    if($null -eq $journal){
        if($null -ne $before){Atomic $backup $dest $null}
        $journal=[pscustomobject]@{scope='managed-launcher-v2';before=$before;after=$after;status='prepared'}
    }else{$journal.status='prepared'}
    Json $record $journal
    foreach($blob in $blobs){if($blob.missing){Atomic $blob.target $blob.source $null};if((Hash $blob.target) -cne $blob.sha256){throw 'Blob changed during staging'}}
    if((Hash $source) -cne $after -or (Hash $dest) -cne $before){throw 'Publication changed before commit'}
    Atomic $dest $source $null
    $journal.status='installed';Json $record $journal
    Write-Output "Published $($release.version): $($blobs.Count) DLLs and $($release.resources.Count) paired resources. No server restart needed. Protocol-2 clients need no new launcher EXE."
}catch{Write-Error $_;exit 1}finally{if($null -ne $lock){$lock.Dispose()}}
