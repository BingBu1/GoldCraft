# Protocol-2 publication checks for the paired engine/client update transaction.
function Read-SignedPublication([string]$Path) {
    if((Get-Item -LiteralPath $Path).Length -gt 1MB){throw 'Publication exceeds the HTTP limit'}
    $envelope=Text $Path|ConvertFrom-Json
    $bytes=[Convert]::FromBase64String($envelope.payload)
    $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp=$false
    try {
        $rsa.FromXmlString((Text (Under $package 'release-public-key.xml')))
        if(!$rsa.VerifyData($bytes,'SHA256',[Convert]::FromBase64String($envelope.signature))){throw 'Invalid publication signature'}
    }finally{$rsa.Dispose()}
    $release=[Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json
    if($release.schema -ne 2 -or $release.bundleId -cne 'ac689085e776441828892db90bd22f09c9ee21c80e0318630ac3ea096c796406' -or
       @($release.files).Count -lt 19 -or @($release.files).Count -gt 128){throw 'Wrong managed publication channel'}
    return $release
}
function Check-PairedPublication {
    if((Hash (Under $package 'release-public-key.xml')) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Wrong public key'}
    $name='cs/launcher-public/manifest.json'
    $next=Read-SignedPublication (Under $package ('payload/'+$name))
    $baseline=Under $package 'baseline-manifest.json';$previous=Read-SignedPublication $baseline
    if($next.sequence -ne $manifest.sequence -or $previous.sequence -ne $manifest.baselineSequence -or
       $previous.sequence -ge $next.sequence){throw 'Wrong paired publication sequence'}
    $oldHash=Hash $baseline;$newHash=$files[$name].sha256
    if(@($files[$name].priorSha256).Count -ne 1 -or $files[$name].priorSha256[0] -cne $oldHash){throw 'Wrong publication preimage'}
    $installed=Hash (Under $target $name)
    if($installed -cne $oldHash -and $installed -cne $newHash){throw "Install launcher server package $($previous.sequence) first; this delta does not match the installed publication."}
    $oldFiles=@{};$seen=@{};$expected=@{}
    foreach($row in $previous.files){
        if($oldFiles.ContainsKey($row.path)){throw 'Duplicate baseline core'}
        $oldFiles[$row.path]=$row
    }
    foreach($row in $next.files){
        if($row.path -cnotmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or
           $seen.ContainsKey($row.path) -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0 -or $row.bytes -ge 16MB){throw 'Invalid signed core'}
        $seen[$row.path]=$true
        $blob='cs/launcher-public/blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path)
        $expected[$blob]=$true
        if(!$all.ContainsKey($blob) -or $all[$blob].sha256 -cne $row.sha256 -or $all[$blob].bytes -ne $row.bytes){throw 'Signed DLL missing from delta or baseline'}
        if(!$files.ContainsKey($blob)){
            $old=$oldFiles[$row.path]
            if($null -eq $old -or $old.sha256 -cne $row.sha256 -or $old.bytes -ne $row.bytes){throw 'Omitted DLL is not part of the signed baseline'}
        }
    }
    foreach($name in $all.Keys){if($name.StartsWith('cs/launcher-public/blobs/') -and !$expected.ContainsKey($name)){throw 'Unreferenced DLL blob in package'}}
}
