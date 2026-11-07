function Check-Publication([string]$Package,[string]$Target,$Manifest,$Files) {
    if((Hash (Under $Package 'release-public-key.xml')) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Wrong release public key'}
    $envelope=Text (Under $Package 'payload/cs/launcher-public/manifest.json')|ConvertFrom-Json
    $payload=[Convert]::FromBase64String($envelope.payload)
    $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp=$false
    try {
        $rsa.FromXmlString((Text (Under $Package 'release-public-key.xml')))
        if(!$rsa.VerifyData($payload,'SHA256',[Convert]::FromBase64String($envelope.signature))){throw 'Invalid client release signature'}
    }finally{$rsa.Dispose()}
    $release=[Text.Encoding]::UTF8.GetString($payload)|ConvertFrom-Json
    if($release.schema -ne 1 -or $release.sequence -ne 2026110701 -or
       $release.bundleId -cne '24af5cde87712fddd2af140f239cd3bda982e0dbf991e3cbc5cf3ce255483005' -or @($release.files).Count -ne 19){throw 'Unexpected paired client release'}
    $seen=@{}
    foreach($row in $release.files){
        if($row.path -cnotmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or $seen.ContainsKey($row.path)){throw 'Invalid core path'}
        $seen[$row.path]=$true
        if($row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0 -or $row.bytes -ge 16MB){throw 'Invalid core identity'}
        $blob='cs/launcher-public/blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path)
        if(!$Files.ContainsKey($blob) -or $Files[$blob].sha256 -cne $row.sha256 -or $Files[$blob].bytes -ne $row.bytes){throw 'Core missing from update or verified baseline'}
    }
    $oldPath=Under $Target 'cs/launcher-public/manifest.json'
    if(Test-Path -LiteralPath $oldPath){
        $old=Text $oldPath|ConvertFrom-Json
        $oldBytes=[Convert]::FromBase64String($old.payload)
        $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider;$rsa.PersistKeyInCsp=$false
        try{$rsa.FromXmlString((Text (Under $Package 'release-public-key.xml')))
            if(!$rsa.VerifyData($oldBytes,'SHA256',[Convert]::FromBase64String($old.signature))){throw 'Existing launcher signature invalid'}
        }finally{$rsa.Dispose()}
        $previous=[Text.Encoding]::UTF8.GetString($oldBytes)|ConvertFrom-Json
        if($previous.bundleId -cne $release.bundleId -or $previous.sequence -gt $release.sequence){throw 'Incompatible or newer installed launcher publication; downgrade refused'}
    }
}
