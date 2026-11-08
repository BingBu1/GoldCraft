param([Parameter(Mandatory)][string]$Bundle,[Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$Output,[Parameter(Mandatory)][string]$PrivateKey,
    [Parameter(Mandatory)][long]$Sequence,[Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$GameAddress,[string]$Python='python')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../scenario-update/Files.ps1')
$root=Plain $Output;$key=Plain $PrivateKey;$cfg=Text (Plain $Bundle)|ConvertFrom-Json
if($key.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Private key must remain outside publication'}
$prepared=& $Python (Join-Path $PSScriptRoot 'managed_release.py') --bundle $Bundle --runtime $Runtime --sequence $Sequence --version $Version --game-address $GameAddress
if($LASTEXITCODE){throw 'Managed release preflight failed'}
$plan=$prepared|ConvertFrom-Json
$rsa=[Security.Cryptography.RSA]::Create();$public=[Security.Cryptography.RSA]::Create();$gate=$null
try{
    $rsa.ImportFromPem([IO.File]::ReadAllText($key));$public.ImportFromPem($cfg.publicKeyPem)
    if($rsa.ExportSubjectPublicKeyInfoPem() -cne $public.ExportSubjectPublicKeyInfoPem()){throw 'Publisher key does not match launcher'}
    $payload=[Text.Encoding]::UTF8.GetBytes(($plan.release|ConvertTo-Json -Depth 20 -Compress))
    $envelope=[ordered]@{payload=[Convert]::ToBase64String($payload);signature=[Convert]::ToBase64String($rsa.SignData($payload,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1))}
    $bytes=[Text.Encoding]::UTF8.GetBytes(($envelope|ConvertTo-Json -Compress))
    if($bytes.Length -gt 1MB){throw 'Signed manifest exceeds the existing HTTP route limit'}
    [IO.Directory]::CreateDirectory($root)|Out-Null
    $gate=[IO.File]::Open((Under $root 'publish.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    $manifest=Under $root 'manifest.json'
    if(Test-Path -LiteralPath $manifest){
        $old=Text $manifest|ConvertFrom-Json;$oldBytes=[Convert]::FromBase64String($old.payload)
        if(!$rsa.VerifyData($oldBytes,[Convert]::FromBase64String($old.signature),[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)){throw 'Existing signature invalid'}
        $previous=[Text.Encoding]::UTF8.GetString($oldBytes)|ConvertFrom-Json
        if($previous.bundleId -cne $cfg.bundleId -or $Sequence -le $previous.sequence){throw 'Release baseline must match and sequence must increase'}
    }
    foreach($row in $plan.auditFiles){if((Hash (Plain $row.source)) -cne $row.sha256){throw 'Input changed after release preparation'}}
    foreach($row in $plan.sources){
        $dest=Under $root ('blobs/'+$row.sha256+'/'+[IO.Path]::GetFileName($row.path))
        if(Test-Path -LiteralPath $dest){if((Hash $dest) -cne $row.sha256){throw 'Immutable blob changed'}}
        else{Atomic $dest $row.source $null}
        if((Hash $dest) -cne $row.sha256){throw 'Core changed during staging'}
    }
    foreach($row in $plan.auditFiles){if((Hash (Plain $row.source)) -cne $row.sha256){throw 'Input changed before release commit'}}
    Atomic $manifest '' $bytes
    Write-Output "Published managed sequence $Sequence with $($plan.sources.Count) DLLs and $($plan.release.resources.Count) signed resources. Existing protocol-2 launchers update without replacing their EXE."
}finally{if($gate){$gate.Dispose()};$public.Dispose();$rsa.Dispose()}
