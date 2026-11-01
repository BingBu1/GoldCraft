Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
function Plain([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    for($p=$full;$p;$p=[IO.Path]::GetDirectoryName($p)) {
        try {$attributes=[IO.File]::GetAttributes($p)}
        catch [IO.FileNotFoundException] {continue}
        catch [IO.DirectoryNotFoundException] {continue}
        if($attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked path rejected: $p"}
    }
    return $full
}
function Under([string]$Root,[string]$Name) {
    if(!$Name -or $Name -match '(^/|\\|:|//|(^|/)\.\.?(/|$))' -or
       @($Name.Split('/')|Where-Object {$_ -match '[. ]$|^(?i:con|prn|aux|nul|com\d|lpt\d)(\.|$)' -or $_.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0}).Count){throw 'Invalid package path'}
    $base=(Plain $Root).TrimEnd('\')+'\';$result=Plain ([IO.Path]::Combine($base,$Name))
    if(!$result.StartsWith($base,[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes root'}
    return $result
}
function Hash([string]$Path) {
    if(!(Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
    $stream=[IO.File]::OpenRead((Plain $Path));$sha=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
    finally{$sha.Dispose();$stream.Dispose()}
}
function Hash-Bytes([byte[]]$Bytes) {
    $sha=[Security.Cryptography.SHA256]::Create()
    try{return [BitConverter]::ToString($sha.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
}
function Atomic([string]$Target,[string]$Source,[byte[]]$Bytes) {
    [void](Plain $Target);[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Target))|Out-Null
    $temp=$Target+'.bingsang-'+[Guid]::NewGuid().ToString('N')+'.tmp'
    try {
        if($Source){[IO.File]::Copy((Plain $Source),$temp,$false);$expected=Hash $Source}
        else{[IO.File]::WriteAllBytes($temp,$Bytes);$expected=Hash-Bytes $Bytes}
        if((Hash $temp) -ne $expected){throw 'Atomic staging hash mismatch'}
        if([IO.File]::Exists($Target)){
            $original=Hash $Target
            for($attempt=0;;$attempt++){
                try{[IO.File]::Replace($temp,$Target,[NullString]::Value);break}
                catch{
                    $code=$_.Exception.GetBaseException().HResult -band 0xffff
                    # Windows can retain a recently released sharing lock briefly.
                    # Retry only while both paths still have their original identities.
                    if($attempt -ge 4 -or $code -notin @(32,33,1175) -or
                       (Hash $Target) -cne $original -or (Hash $temp) -cne $expected){throw}
                    Start-Sleep -Milliseconds (100 * [Math]::Pow(2,$attempt))
                }
            }
        }else{[IO.File]::Move($temp,$Target)}
    }finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Json([string]$Path,$Value){Atomic $Path '' ([Text.UTF8Encoding]::new($false).GetBytes(($Value|ConvertTo-Json -Depth 30)+"`r`n"))}
function Text([string]$Path){if([IO.File]::Exists($Path)){return [IO.File]::ReadAllText((Plain $Path))};return ''}
function Stopped([string]$Root) {
    $prefix=(Plain $Root).TrimEnd('\')+'\'
    foreach($p in Get-CimInstance Win32_Process){
        if($p.ExecutablePath -and $p.ExecutablePath.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Stop both servers with the original Stop.cmd before applying or restoring.'}
        if($p.Name -match '^(powershell|pwsh|java|javaw)\.exe$' -and $p.CommandLine -and $p.CommandLine.IndexOf($prefix,[StringComparison]::OrdinalIgnoreCase) -ge 0){throw 'A server supervisor is still running; use the original Stop.cmd first.'}
    }
}
function Restore-Files([string]$Root,[string]$Backup,$Journal) {
    foreach($row in $Journal.files){
        $target=Under $Root $row.path;$now=Hash $target
        if($now -ne $row.before -and $now -ne $row.after){throw "File changed outside update; backup retained: $($row.path)"}
        if($row.existed -and (Hash (Under $Backup ('files/'+$row.path))) -ne $row.before){throw 'Backup integrity failure'}
    }
    foreach($row in $Journal.files){
        $target=Under $Root $row.path
        if((Hash $target) -eq $row.before){continue}
        if($row.existed){Atomic $target (Under $Backup ('files/'+$row.path)) $null}
        elseif([IO.File]::Exists($target)){[IO.File]::Delete($target)}
    }
    $Journal.status='restored';Json (Under $Backup 'transaction.json') $Journal
}
