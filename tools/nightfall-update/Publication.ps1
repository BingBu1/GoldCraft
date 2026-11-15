function Read-NightfallPublication([string]$Path) {
    if((Get-Item -LiteralPath $Path).Length -gt 1MB){throw 'Publication exceeds the HTTP limit'}
    $envelope=Text $Path|ConvertFrom-Json
    $bytes=[Convert]::FromBase64String($envelope.payload)
    $rsa=New-Object Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp=$false
    try{
        $rsa.FromXmlString((Text (Under $package 'release-public-key.xml')))
        if(!$rsa.VerifyData($bytes,'SHA256',[Convert]::FromBase64String($envelope.signature))){throw 'Invalid client release signature'}
    }finally{$rsa.Dispose()}
    return [Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json
}
function Check-Publication([string]$Package,[string]$Target,$Manifest,$Files) {
    if((Hash (Under $Package 'release-public-key.xml')) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Wrong release public key'}
    $release=Read-NightfallPublication (Under $Package 'payload/cs/launcher-public/manifest.json')
    if($release.schema -ne 2 -or $release.sequence -ne $Manifest.managedSequence -or $release.bundleId -cne $Manifest.bundleId -or @($release.files).Count -lt 19){throw 'Unexpected managed client release'}
    $seen=@{}
    foreach($row in $release.files){
        if($row.path -cnotmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or
           $seen.ContainsKey($row.path) -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0 -or $row.bytes -ge 16MB){throw 'Invalid signed core'}
        $seen[$row.path]=$true
        $blob='cs/launcher-public/blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path)
        if(!$Files.ContainsKey($blob) -or $Files[$blob].sha256 -cne $row.sha256 -or $Files[$blob].bytes -ne $row.bytes){throw 'Signed client DLL missing from package or verified baseline'}
    }
    $seen=@{}
    foreach($row in $release.resources){
        if($row.path -cnotmatch '^cstrike/(metahook/gamedata/[A-Za-z0-9_./-]+\.json|(metahook/shaders|renderer/shader)/[A-Za-z0-9_./-]+\.(glsl|fsh|vsh|h))$' -or
           $seen.ContainsKey($row.path) -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $row.bytes -le 0 -or $row.bytes -gt 4MB){throw 'Invalid signed catalog or shader'}
        [void](Under $Target $row.path)
        $seen[$row.path]=$true
        # Publication preparation checks the embedded resource bytes and index
        # closure; the launcher repeats that check before installing them.
        # The server installs this signed document, without expanding client data.
    }
    $old=Under $Target 'cs/launcher-public/manifest.json'
    if(Test-Path -LiteralPath $old){
        $previous=Read-NightfallPublication $old
        if($previous.schema -ne 2 -or $previous.bundleId -cne $release.bundleId -or $previous.sequence -gt $release.sequence){throw 'Incompatible or newer launcher publication; downgrade refused'}
    }
}
