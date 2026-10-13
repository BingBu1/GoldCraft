param([ValidateRange(10,90)][int]$TimeoutSeconds=90)
. (Join-Path $PSScriptRoot 'Common.ps1')
$stopErrors = @()
foreach ($role in @('minecraft','cs')) {
    if (Get-RoleProcess $role) {
        try { Send-RoleCommand $role $(if ($role -eq 'cs') {'quit'} else {'stop'}) }
        catch { $stopErrors += "$role : $($_.Exception.Message)" }
    }
}
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
while ((Get-RoleProcess 'cs') -or (Get-RoleProcess 'minecraft')) {
    if ([DateTime]::UtcNow -gt $deadline) { throw 'Graceful shutdown timed out. Check the consoles; processes were not forcibly killed.' }
    Start-Sleep -Milliseconds 250
}
if ($stopErrors.Count) { throw ($stopErrors -join '; ') }
Write-Host 'Both servers stopped.'
