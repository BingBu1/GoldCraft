param(
    [Parameter(Mandatory)][string]$Bundle,
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][string]$PrivateKey,
    [Parameter(Mandatory)][long]$Sequence,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$GameAddress
)
$ErrorActionPreference = 'Stop'
function Plain([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    for($p=$full;$p;$p=[IO.Path]::GetDirectoryName($p)) {
        if((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Reparse point in publication path' }
    }
    return $full
}
function Atomic([string]$Path,[byte[]]$Bytes) {
    $temp=$Path+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
    [IO.File]::WriteAllBytes($temp,$Bytes)
    try { if([IO.File]::Exists($Path)) { [IO.File]::Replace($temp,$Path,[NullString]::Value) } else { [IO.File]::Move($temp,$Path) } }
    finally { if([IO.File]::Exists($temp)) { [IO.File]::Delete($temp) } }
}
$root=Plain $Output
$runtimeRoot=Plain $Runtime
$keyFile=Plain $PrivateKey
if($keyFile.StartsWith($root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Private key must remain outside the served directory' }
if($GameAddress -notmatch '^[A-Za-z0-9.-]+:[0-9]{1,5}$' -or [int]($GameAddress.Split(':')[-1]) -notin 1..65535) { throw 'Invalid game address' }
if($Sequence -lt 1 -or !$Version -or $Version.Length -gt 80) { throw 'Invalid release identity' }
$cfg=Get-Content -LiteralPath (Plain $Bundle) -Raw -Encoding UTF8 | ConvertFrom-Json
foreach($fixed in $cfg.files) {
    if($fixed.path -in $cfg.corePaths -or $fixed.path.StartsWith('goldcraft-licenses/')) { continue }
    $file=Plain (Join-Path $runtimeRoot $fixed.path)
    if(!(Test-Path -LiteralPath $file -PathType Leaf) -or
       (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fixed.sha256) {
        throw "Static resource changed: $($fixed.path). Build a new launcher bundle instead of publishing an incompatible DLL update."
    }
}
foreach($entry in @(@('hw.dll',$cfg.engineSha256),@('cstrike/cl_dlls/client.dll',$cfg.clientSha256))) {
    if((Get-FileHash -LiteralPath (Plain (Join-Path $runtimeRoot $entry[0])) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry[1]) { throw 'Unsupported base game version' }
}
$rsa=[Security.Cryptography.RSA]::Create()
$rsa.ImportFromPem([IO.File]::ReadAllText($keyFile))
$public=[Security.Cryptography.RSA]::Create();$public.ImportFromPem($cfg.publicKeyPem)
if($rsa.ExportSubjectPublicKeyInfoPem() -ne $public.ExportSubjectPublicKeyInfoPem()) { throw 'Publisher key does not match the embedded launcher key' }
[IO.Directory]::CreateDirectory($root) | Out-Null
$gate=[IO.File]::Open((Join-Path $root 'publish.lock'),[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
try {
    $manifest=Plain (Join-Path $root 'manifest.json')
    if(Test-Path -LiteralPath $manifest) {
        $old=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8 | ConvertFrom-Json
        $oldBytes=[Convert]::FromBase64String($old.payload)
        if(!$rsa.VerifyData($oldBytes,[Convert]::FromBase64String($old.signature),[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)) { throw 'Existing manifest signature invalid' }
        $previous=[Text.Encoding]::UTF8.GetString($oldBytes)|ConvertFrom-Json
        if($Sequence -le $previous.sequence) { throw 'Sequence must increase' }
    }
    $files=@(foreach($name in $cfg.corePaths) {
        if($name -notmatch '^(MetaHook\.dll|cstrike/metahook/(plugins|dlls)/[A-Za-z0-9_/-]+\.dll)$' -or $name.Contains('..')) { throw 'Non-core DLL in allowlist' }
        $source=Plain (Join-Path $runtimeRoot $name)
        $item=Get-Item -LiteralPath $source
        if($item.Length -le 0 -or $item.Length -ge 16MB) { throw 'Core DLL exceeds HTTP limit' }
        $binary=[IO.BinaryReader]::new([IO.File]::OpenRead($source))
        try {
            if($binary.ReadUInt16() -ne 0x5A4D) { throw 'Core DLL is not a PE binary' }
            $binary.BaseStream.Position=0x3C;$offset=$binary.ReadInt32()
            if($offset -lt 64 -or $offset+6 -gt $item.Length) { throw 'Invalid PE header' }
            $binary.BaseStream.Position=$offset
            if($binary.ReadUInt32() -ne 0x4550 -or $binary.ReadUInt16() -ne 0x14C) { throw 'Core DLL must be x86' }
        } finally { $binary.Dispose() }
        $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        $folder=Plain (Join-Path $root ('blobs/'+$hash))
        [IO.Directory]::CreateDirectory($folder)|Out-Null
        $dest=Plain (Join-Path $folder ([IO.Path]::GetFileName($name)))
        if(Test-Path -LiteralPath $dest) {
            if((Get-FileHash -LiteralPath $dest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'Immutable blob changed' }
        } else {
            $temp=$dest+'.'+[Guid]::NewGuid().ToString('N')+'.tmp'
            [IO.File]::Copy($source,$temp,$false)
            try {
                if((Get-FileHash -LiteralPath $temp -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hash) { throw 'Source changed during publication' }
                [IO.File]::Move($temp,$dest)
            } finally {if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
        }
        [ordered]@{path=$name;bytes=$item.Length;sha256=$hash}
    })
    $release=[ordered]@{schema=1;bundleId=$cfg.bundleId;sequence=$Sequence;version=$Version;gameAddress=$GameAddress;files=$files}
    $payload=[Text.Encoding]::UTF8.GetBytes(($release|ConvertTo-Json -Depth 8 -Compress))
    $signature=$rsa.SignData($payload,[Security.Cryptography.HashAlgorithmName]::SHA256,[Security.Cryptography.RSASignaturePadding]::Pkcs1)
    $envelope=[ordered]@{payload=[Convert]::ToBase64String($payload);signature=[Convert]::ToBase64String($signature)}
    Atomic $manifest ([Text.Encoding]::UTF8.GetBytes(($envelope|ConvertTo-Json -Compress)))
    Write-Output "Published $($files.Count) core DLLs; sequence $Sequence. Copy launcher-public beside cstrike on the server."
} finally { $gate.Dispose();$rsa.Dispose();$public.Dispose() }
