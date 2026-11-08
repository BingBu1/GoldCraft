param([Parameter(Mandatory)][string]$Plan,[Parameter(Mandatory)][string]$PrivateKey,[Parameter(Mandatory)][string]$Output,
      [Parameter(Mandatory)][string]$Bundle,[Parameter(Mandatory)][string]$Runtime)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scenario-update/Files.ps1')
. (Join-Path $PSScriptRoot 'StaticContract.ps1')
$planPath=Plain $Plan
$spec=Text $planPath|ConvertFrom-Json
$root=Plain $Output
if(Test-Path -LiteralPath $root){throw 'Choose a fresh publication directory'}
$key=Plain $PrivateKey
if($key.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Private key must remain outside publication'}
$publicPath=Plain (Join-Path $PSScriptRoot '../respawn-release/release-public-key.xml')
if((Hash $publicPath) -cne '0b2d4fb9163980b5a5c4cf3af61539c76a0bf8c8f1153b87b1328fe20118c8fb'){throw 'Unexpected public key'}
$public=[Security.Cryptography.RSA]::Create()
$signer=[Security.Cryptography.RSA]::Create()
try {
    $public.FromXmlString((Text $publicPath))
    $old=Text (Join-Path ([IO.Path]::GetDirectoryName($planPath)) 'baseline-publication.json')|ConvertFrom-Json
    $bytes=[Convert]::FromBase64String($old.payload)
    if(!$public.VerifyData($bytes,[Convert]::FromBase64String($old.signature),[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)){throw 'Invalid previous publication signature'}
    $previous=[Text.Encoding]::UTF8.GetString($bytes)|ConvertFrom-Json
    if($previous.sequence -ne 2026110105 -or $previous.bundleId -cne '24af5cde87712fddd2af140f239cd3bda982e0dbf991e3cbc5cf3ce255483005' -or @($previous.files).Count -ne 19){throw 'Unexpected previous release'}
    Assert-LauncherStaticContract $Bundle $Runtime $previous
    $pins=@{
        'cstrike/metahook/plugins/CSOLSupport.dll'='d585de84ff94d5e1b9ea561ef3586c801c83f6a0e1b7d0a2b6e635525691fe6c'
        'cstrike/metahook/plugins/GoldCraft.dll'='daaf84f876fd57382e54f4eb09f2e4d6982dd43ed8e9b9e3e6d709bb618df16a'
    }
    if(@($spec.clientChanges).Count -ne 2){throw 'Expected two verified core changes'}
    $changed=@{}
    foreach($row in $spec.clientChanges){
        if(!$pins.ContainsKey($row.path) -or $changed.ContainsKey($row.path) -or $row.sha256 -cne $pins[$row.path] -or
           (Hash $row.source) -cne $row.sha256 -or (Get-Item -LiteralPath $row.source).Length -ne $row.bytes){throw 'Core input identity mismatch'}
        $changed[$row.path]=$row
    }
    $signer.ImportFromPem([IO.File]::ReadAllText($key))
    if($signer.ExportSubjectPublicKeyInfoPem() -cne $public.ExportSubjectPublicKeyInfoPem()){throw 'Signing key does not match launcher public key'}
    $files=@(foreach($row in $previous.files){
        if($changed.ContainsKey($row.path)){$row=$changed[$row.path]}
        [ordered]@{path=$row.path;bytes=$row.bytes;sha256=$row.sha256}
    })
    $release=[ordered]@{schema=1;bundleId=$previous.bundleId;sequence=2026110701;version='2026.11.07.1';gameAddress=$previous.gameAddress;files=$files}
    $payload=[Text.Encoding]::UTF8.GetBytes(($release|ConvertTo-Json -Depth 8 -Compress))
    $signature=$signer.SignData($payload,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
    if(!$public.VerifyData($payload,$signature,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)){throw 'Publication verification failed'}
    [IO.Directory]::CreateDirectory($root)|Out-Null
    foreach($row in $spec.clientChanges){
        $dest=Under $root ('blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path))
        Atomic $dest $row.source $null
        if((Hash $dest) -cne $row.sha256){throw 'Published blob mismatch'}
    }
    Json (Under $root 'manifest.json') ([ordered]@{payload=[Convert]::ToBase64String($payload);signature=[Convert]::ToBase64String($signature)})
    Write-Host 'Verified signed sequence 2026110701: two updated cores, seventeen unchanged.'
}finally{$signer.Dispose();$public.Dispose()}
