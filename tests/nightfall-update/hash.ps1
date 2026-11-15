param([string]$Files,[string]$FastHash,[string]$Paths,[string]$Output)
. $Files
$originalHash=${function:Hash}
$inputs=Get-Content -LiteralPath $Paths -Raw -Encoding UTF8|ConvertFrom-Json
. $FastHash
$results=@()
foreach($row in $inputs){
    $watch=[Diagnostics.Stopwatch]::StartNew();$old=& $originalHash $row.source;$watch.Stop();$oldMs=$watch.Elapsed.TotalMilliseconds
    $watch.Restart();$new=Hash $row.source;$watch.Stop()
    if($old -cne $row.sha256 -or $new -cne $old){throw 'Hash implementation mismatch'}
    $results+=@([ordered]@{bytes=$row.bytes;originalMs=$oldMs;cngMs=$watch.Elapsed.TotalMilliseconds;equal=$true})
}
$results|ConvertTo-Json|Set-Content -LiteralPath $Output -Encoding UTF8
