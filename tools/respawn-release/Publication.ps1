function Check-Publication([string]$Package,[string]$Target,[string]$ManifestPath,[string]$BlobPath,[string]$ClientSha) {
    if((Hash (Under $Package 'release-public-key.xml')) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Wrong release public key'}
    $envelope=Text (Under $Package ('payload/'+$ManifestPath))|ConvertFrom-Json
    $payload=[Convert]::FromBase64String($envelope.payload)
    $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp=$false
    try {
        $rsa.FromXmlString((Text (Under $Package 'release-public-key.xml')))
        if(!$rsa.VerifyData($payload,'SHA256',[Convert]::FromBase64String($envelope.signature))){throw 'Invalid client release signature'}
    }finally{$rsa.Dispose()}
    $release=[Text.Encoding]::UTF8.GetString($payload)|ConvertFrom-Json
    if($release.schema -ne 1 -or $release.sequence -ne 2026110105 -or
       $release.bundleId -cne '24af5cde87712fddd2af140f239cd3bda982e0dbf991e3cbc5cf3ce255483005' -or
       @($release.files).Count -ne 19){throw 'Wrong signed client release'}
    $seen=@{};$hasClient=$false
    foreach($row in $release.files){
        if($row.path -cnotmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or
           $seen.ContainsKey($row.path)){throw 'Invalid client core path'}
        [void](Under $Target $row.path);$seen[$row.path]=$true
        Checked-Hash $row
        $blob='cs/launcher-public/blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path)
        if($row.path -ceq 'cstrike/metahook/plugins/CSOLSupport.dll'){
            if($row.sha256 -cne $ClientSha -or $blob -cne $BlobPath){throw 'Wrong paired client'}
            $path=Under $Package ('payload/'+$blob);$hasClient=$true
        }else{$path=Under $Target $blob}
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or
           (Get-Item -LiteralPath $path).Length -ne $row.bytes -or (Hash $path) -cne $row.sha256){
            throw 'A required launcher core blob is missing or changed. Install the previous Scenario package first.'
        }
    }
    if(!$hasClient){throw 'Paired client missing from signed publication'}
}
