$ErrorActionPreference = 'Stop'
$PackageRoot = Split-Path $PSScriptRoot -Parent
$State = Join-Path $PackageRoot '.state'
$Utf8 = New-Object System.Text.UTF8Encoding($false)
function Write-Text([string]$Path, [string]$Value) {
    [IO.File]::WriteAllText($Path, $Value, $Utf8)
}
function Read-Json([string]$Path) {
    Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}
function Get-CsPublicPort($Settings) {
    # Missing field supports existing packages; 0 preserves direct hosting.
    if (-not $Settings.PSObject.Properties['csPublicPort']) { return 0 }
    $value = $Settings.csPublicPort
    if (($value -isnot [int] -and $value -isnot [long]) -or $value -lt 0 -or $value -gt 65535) {
        throw 'csPublicPort must be 0 (use csPort) or an integer in 1-65535.'
    }
    return [int]$value
}
function Write-Json([string]$Path, $Value) {
    Write-Text "$Path.tmp" ($Value | ConvertTo-Json -Depth 12)
    Move-Item -LiteralPath "$Path.tmp" -Destination $Path -Force
}
function Random-Hex([int]$Count) {
    $bytes = New-Object byte[] $Count
    $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
    try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
    -join ($bytes | ForEach-Object { $_.ToString('x2') })
}
function Set-Property([string]$Path, [string]$Name, [string]$Value) {
    $lines = @(Get-Content -LiteralPath $Path -Encoding UTF8 | Where-Object { -not $_.StartsWith("$Name=") })
    Write-Text $Path (($lines + "$Name=$Value") -join "`n")
}
function Get-RoleProcess([string]$Role) {
    $path = Join-Path $State "$Role.json"
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $record = Read-Json $path
    $proc = Get-Process -Id $record.pid -ErrorAction SilentlyContinue
    if ($proc -and $proc.StartTime.ToUniversalTime().Ticks.ToString() -eq $record.startTicks) {
        return $record
    }
    return $null
}
function Send-RoleCommand([string]$Role, [string]$Command) {
    $record = Get-RoleProcess $Role
    if (-not $record) { throw "$Role is not running." }
    if ($Command.Length -gt 512 -or $Command -match '[\r\n\x00]') { throw 'One console command is required.' }
    $path = Join-Path $State "$($record.runId).command"
    if (Test-Path -LiteralPath $path) { throw "$Role already has a pending console command." }
    if (Test-Path -LiteralPath "$path.error") { Remove-Item -LiteralPath "$path.error" }
    $temporary = "$path.$([Guid]::NewGuid().ToString('N')).tmp"
    Write-Text $temporary $Command
    Move-Item -LiteralPath $temporary -Destination $path
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ((Test-Path -LiteralPath $path) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
    if (Test-Path -LiteralPath $path) { throw "$Role console did not acknowledge the command." }
    if (Test-Path -LiteralPath "$path.error") { throw ([IO.File]::ReadAllText("$path.error", $Utf8)) }
}
