param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
$ErrorActionPreference='Stop'
$helper=Join-Path $PSScriptRoot 'Files.ps1'
if(!(Test-Path -LiteralPath $helper)){$helper=Join-Path $PSScriptRoot '../scenario-update/Files.ps1'}
. $helper
$lock=$null
$newHash='f41fd468156fca0f3b67bf4e351b488024cabfa1f4ae3b52c1a624721e7484df'
$oldHash='f5237c6d3997e13f8e48c0634783667f6519a91ce5dbbb52737a9b51e3cdea8a'
$newBundle='ac689085e776441828892db90bd22f09c9ee21c80e0318630ac3ea096c796406'
function Signed([string]$Path) {
    $envelope=Text $Path|ConvertFrom-Json
    $bytes=[Convert]::FromBase64String($envelope.payload)
    $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp=$false
    try{
        $rsa.FromXmlString((Text (Under $PSScriptRoot 'release-public-key.xml')))
        if(!$rsa.VerifyData($bytes,'SHA256',[Convert]::FromBase64String($envelope.signature))){throw 'Invalid release signature'}
    }finally{$rsa.Dispose()}
    return ([Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json)
}
try {
    if($Plan -and $Restore){throw 'Choose Plan or Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'Enter the server folder containing Start.cmd'}
    $entered=$TargetDirectory.Trim().Trim('"').Trim("'")
    if($entered -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Use an absolute server folder path'}
    $target=(Plain $entered).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract this update outside the server folder'}
    foreach($rel in @('Start.cmd','cs/hlds.exe')){if(!(Test-Path -LiteralPath (Under $target $rel) -PathType Leaf)){throw "Wrong server folder: missing $rel"}}
    if((Hash (Under $package 'release-public-key.xml')) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Wrong release public key'}
    $manifest=Under $package 'payload/cs/launcher-public/manifest.json'
    if((Hash $manifest) -cne $newHash){throw 'Package manifest differs from this release'}
    $release=Signed $manifest
    if($release.schema -ne 1 -or $release.sequence -ne 2026110801 -or $release.bundleId -cne $newBundle -or @($release.files).Count -ne 19){throw 'Wrong launcher release'}
    $seen=@{};$blobs=@()
    foreach($row in $release.files){
        if($row.path -cnotmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or $seen.ContainsKey($row.path) -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0 -or $row.bytes -ge 16MB){throw 'Invalid core DLL record'}
        [void](Under $target $row.path);$seen[$row.path]=$true
        $rel='cs/launcher-public/blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path)
        $src=Under $package ('payload/'+$rel)
        if((Hash $src) -cne $row.sha256 -or (Get-Item -LiteralPath $src).Length -ne $row.bytes){throw "Missing or corrupt packaged core: $($row.path)"}
        $dest=Under $target $rel;$existing=Hash $dest
        if($null -ne $existing -and $existing -cne $row.sha256){throw "Existing immutable blob was modified: $($row.path)"}
        $blobs+=@([pscustomobject]@{source=$src;target=$dest;sha256=$row.sha256;missing=($null -eq $existing)})
    }
    $destManifest=Under $target 'cs/launcher-public/manifest.json'
    $before=Hash $destManifest
    if($null -ne $before -and $before -cne $oldHash -and $before -cne $newHash){throw 'Existing launcher publication is not November 7 or this release; refusing to overwrite'}
    if($null -ne $before){[void](Signed $destManifest)}
    if($Plan){Write-Output "Verified 19 core DLLs. Would add $(@($blobs|Where-Object {$_.missing}).Count) missing blobs and publish launcher 2026.11.08.1. Server configuration is preserved.";exit 0}
    $state=Under $target '.bingsang-launcher-update/20261108'
    [IO.Directory]::CreateDirectory($state)|Out-Null
    $lock=[IO.File]::Open((Under $state 'operation.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    if((Hash $destManifest) -cne $before){throw 'Publication changed during verification; retry'}
    $journalPath=Under $state 'transaction.json';$backup=Under $state 'manifest.before.json'
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){
        $journal=Text $journalPath|ConvertFrom-Json
        if($journal.scope -cne 'launcher-20261108' -or $journal.after -cne $newHash -or $journal.status -notin @('prepared','installed','restored') -or ($null -ne $journal.before -and $journal.before -cne $oldHash)){throw 'Unrecognized recovery record'}
        if($null -ne $journal.before){if((Hash $backup) -cne $oldHash){throw 'Backup integrity failure'};[void](Signed $backup)}
    }
    if($Restore){
        if($null -eq $journal){throw 'No backup from this update exists'}
        if($before -cne $newHash -and $before -cne $journal.before){throw 'Publication changed after this update'}
        if($before -cne $journal.before){
            if($null -eq $journal.before){[IO.File]::Delete($destManifest)}else{Atomic $destManifest $backup $null}
        }
        $journal.status='restored';Json $journalPath $journal
        Write-Output 'Previous launcher manifest restored. Content-addressed core blobs retained.';exit 0
    }
    if($before -ceq $newHash){
        foreach($blob in $blobs){if($blob.missing){Atomic $blob.target $blob.source $null}}
        Write-Output 'Launcher publication already current; all 19 DLLs verified.';exit 0
    }
    if($null -ne $journal -and $before -cne $journal.before){throw 'Publication differs from the recorded original'}
    if($null -eq $journal){
        if($null -ne $before){Atomic $backup $destManifest $null}
        $journal=[pscustomobject]@{scope='launcher-20261108';before=$before;after=$newHash;status='prepared'}
    }else{$journal.status='prepared'}
    Json $journalPath $journal
    # Blobs are immutable; a partial copy cannot affect the old signed manifest.
    # Publish the new manifest only after every required blob is available.
    foreach($blob in $blobs){
        if($blob.missing){Atomic $blob.target $blob.source $null}
        if((Hash $blob.target) -cne $blob.sha256){throw 'Core changed during publication'}
    }
    if((Hash $destManifest) -cne $before){throw 'Publication changed before commit'}
    Atomic $destManifest $manifest $null
    $journal.status='installed';Json $journalPath $journal
    Write-Output 'Updated launcher publication 2026.11.08.1 with all 19 core DLLs. No server restart required. Give players the matching new launcher EXE.'
}catch{Write-Error $_;exit 1}finally{if($null -ne $lock){$lock.Dispose()}}
