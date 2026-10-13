. (Join-Path $PSScriptRoot 'Common.ps1')
foreach ($role in @('cs','minecraft')) {
    $record = Get-RoleProcess $role
    if ($record) { Write-Host "$role running: PID $($record.pid)" }
    else { Write-Host "$role stopped" }
}
