# WinPS 5.1's SHA256.Create() selects the managed implementation. Use Windows
# CNG through IncrementalHash, with one bounded buffer for this serial installer.
$script:nightfallHashBuffer=New-Object byte[] 1048576
function Hash([string]$Path) {
    if(!(Test-Path -LiteralPath $Path -PathType Leaf)){return $null}
    $stream=[IO.File]::OpenRead((Plain $Path))
    $hash=[Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
    try{
        while(($count=$stream.Read($script:nightfallHashBuffer,0,$script:nightfallHashBuffer.Length)) -gt 0){
            $hash.AppendData($script:nightfallHashBuffer,0,$count)
        }
        return [BitConverter]::ToString($hash.GetHashAndReset()).Replace('-','').ToLowerInvariant()
    }finally{$hash.Dispose();$stream.Dispose()}
}
