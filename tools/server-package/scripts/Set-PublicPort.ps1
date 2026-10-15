param(
    [Parameter(Mandatory)][ValidateRange(0,65535)][int]$PublicPort,
    [string]$PackageRoot,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Split-Path -Path $PSScriptRoot -Parent
}
$root = (Get-Item -LiteralPath $PackageRoot).FullName
$settingsPath = Join-Path $root 'server-settings.json'
$enginePath = Join-Path $root 'cs/swds.dll'
$runnerPath = Join-Path $root 'scripts/Run-Server.ps1'
$statePath = Join-Path $root '.state'
foreach ($path in @($settingsPath,$enginePath,$runnerPath,$statePath)) {
    $entry = $path
    while ($entry) {
        if (Test-Path -LiteralPath $entry) {
            if ((Get-Item -LiteralPath $entry -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw 'Refusing a redirected package path.'
            }
        }
        $entry = Split-Path $entry -Parent
    }
}
$original = [IO.File]::ReadAllBytes($settingsPath)
$settings = [Text.Encoding]::UTF8.GetString($original).TrimStart([char]0xfeff) | ConvertFrom-Json
if (-not $settings.PSObject.Properties['csPort']) { throw 'Not a GoldCraft server package: csPort is missing.' }
if ($PublicPort) {
    $engine = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($enginePath))
    $runner = [IO.File]::ReadAllText($runnerPath)
    if (-not $engine.Contains('GoldCraft -publicport requires an integer in 1..65535') -or
        -not $runner.Contains('-publicport $csPublicPort')) {
        throw 'Apply the public-port engine/scripts hotfix before setting this port.'
    }
}
$previous = if ($settings.PSObject.Properties['csPublicPort']) { $settings.csPublicPort } else { 0 }
if ($CheckOnly) {
    Write-Output "Public-port support verified. Current=$previous; requested=$PublicPort. No files changed."
    exit 0
}
if (($previous -is [int] -or $previous -is [long]) -and $previous -eq $PublicPort) {
    Write-Output "Public port is already $PublicPort. No files changed. Restart CS if its current process predates this setting."
    exit 0
}
New-Item -ItemType Directory -Path $statePath -Force | Out-Null
$tag = [Guid]::NewGuid().ToString('N')
$backup = Join-Path $statePath "server-settings-before-public-port-$tag.json"
$temporary = Join-Path $root "server-settings-public-port-$tag.tmp"
$settings | Add-Member -NotePropertyName csPublicPort -NotePropertyValue $PublicPort -Force
$utf8 = New-Object Text.UTF8Encoding($false)
try {
    [IO.File]::WriteAllText($temporary, ($settings | ConvertTo-Json -Depth 100), $utf8)
    $readback = [IO.File]::ReadAllText($temporary) | ConvertFrom-Json
    if ($readback.csPublicPort -ne $PublicPort) { throw 'Public-port setting readback failed.' }
    if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($settingsPath)) -ne [Convert]::ToBase64String($original)) {
        throw 'Server settings changed during this operation; no update was applied.'
    }
    [IO.File]::Replace($temporary, $settingsPath, $backup)
} finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}
Write-Output "Public port set to $PublicPort; local CS listener remains $($settings.csPort)."
Write-Output "Original settings backup: $backup"
Write-Output 'Restart CS using the package launcher to apply it; a map change is insufficient.'
