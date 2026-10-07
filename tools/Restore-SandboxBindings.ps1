param([ValidateSet('cs-client-a','cs-client-b')][string[]]$Instances=@('cs-client-a','cs-client-b'))
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$source=Join-Path $script:GoldCraftOriginal 'cstrike/config.cfg'
Assert-NoReparsePath $source
$bindings=@(Get-Content -LiteralPath $source | Where-Object {$_ -match '^\s*bind\s+"'})
if($bindings.Count -lt 10){throw 'Original binding configuration is missing or unexpectedly small.'}
$evidence=@()
foreach($instance in $Instances){
    $game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$instance/Half-Life")
    if(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq (Join-Path $game 'MetaHook.exe')}){throw "Stop $instance before restoring its saved bindings."}
    $target=Assert-SandboxPath (Join-Path $game 'cstrike/config.cfg')
    $backup=Assert-SandboxPath ($target+'.before-bindings13')
    if(-not(Test-Path -LiteralPath $backup)){Copy-Item -LiteralPath $target -Destination $backup}
    $lines=@(Get-Content -LiteralPath $target)
    $before=@($lines | Where-Object {$_ -match '^\s*bind\s+'}).Count
    $other=@($lines | Where-Object {$_ -notmatch '^\s*(bind\s+|unbindall\b|unbind\s+)'})
    @('unbindall')+$bindings+$other | Set-Content -LiteralPath $target -Encoding ascii
    $actual=@(Get-Content -LiteralPath $target | Where-Object {$_ -match '^\s*bind\s+"'})
    if(Compare-Object $bindings $actual){throw 'Restored binding comparison failed'}
    $evidence+=@{instance=$instance;previousBindings=$before;restoredBindings=$actual.Count;matchesOriginal=$true;sha256=(Get-FileHash -LiteralPath $target).Hash}
}
@{sourceReadOnly=$source;instances=$evidence} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/goldcraft-tests/bindings13-restored.json')) -Encoding utf8
Write-Output "Restored $($bindings.Count) original bindings in each selected sandbox; other sandbox settings preserved."
