param(
    [Parameter(Mandatory)][ValidateSet('Baseline','Verify','Copy')][string]$Action,
    [ValidatePattern('^[a-z0-9][a-z0-9-]{0,40}$')][string]$Instance = 'cs-client-a'
)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$manifestPath = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/installation/original-baseline.json')

function Get-Manifest {
    $entries = [Collections.Generic.List[object]]::new()
    $index = 0
    foreach ($file in Get-OriginalFiles) {
        $entries.Add([ordered]@{
            path = $file.FullName.Substring($script:GoldCraftOriginal.Length + 1)
            bytes = $file.Length
            modifiedUtc = $file.LastWriteTimeUtc.ToString('O')
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        })
        $index++
        if ($index % 2000 -eq 0) { Write-Host "Hashed $index original files (read only)." }
    }
    return [ordered]@{
        source = $script:GoldCraftOriginal
        capturedUtc = [DateTime]::UtcNow.ToString('O')
        files = $entries.ToArray()
    }
}

if ($Action -eq 'Baseline') {
    if (Test-Path -LiteralPath $manifestPath) { throw 'Baseline already exists; refusing to replace audit evidence.' }
    $manifest = Get-Manifest
    New-Item -ItemType Directory -Path (Split-Path $manifestPath) -Force | Out-Null
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    Write-Host "Baseline: $($manifest.files.Count) files. $manifestPath"
    return
}

if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'Capture a baseline before copies or tests.' }
# Keep the ISO timestamp as text; PowerShell's automatic DateTime coercion changes comparison semantics.
$baseline = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json -DateKind String
if ($baseline.source -ne $script:GoldCraftOriginal) { throw 'Baseline source mismatch.' }

if ($Action -eq 'Verify') {
    $current = Get-Manifest
    $expected = @{}
    foreach ($entry in $baseline.files) { $expected[$entry.path] = $entry }
    $changes = [Collections.Generic.List[string]]::new()
    foreach ($entry in $current.files) {
        if (-not $expected.ContainsKey($entry.path)) { $changes.Add("added: $($entry.path)"); continue }
        $old = $expected[$entry.path]
        if ($old.bytes -ne $entry.bytes -or $old.sha256 -ne $entry.sha256 -or $old.modifiedUtc -ne $entry.modifiedUtc) {
            $changes.Add("changed: $($entry.path)")
        }
        $expected.Remove($entry.path)
    }
    foreach ($path in $expected.Keys) { $changes.Add("removed: $path") }
    $resultPath = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot ('analysis/installation/verify-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '.json'))
    @{ checkedUtc = $current.capturedUtc; fileCount = $current.files.Count; unchanged = ($changes.Count -eq 0); changes = $changes.ToArray() } |
        ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding utf8
    if ($changes.Count) { throw "Original installation differs from baseline. See $resultPath" }
    Write-Host "Original unchanged: $($current.files.Count) files. $resultPath"
    return
}

$destination = Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/Half-Life")
if (Test-Path -LiteralPath $destination) { throw "Copy already exists; refusing overwrite: $destination" }
New-Item -ItemType Directory -Path $destination -Force | Out-Null
$copied = 0
$copyPolicy=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sandbox-policy.json') -Raw | ConvertFrom-Json
foreach ($entry in $baseline.files) {
    # CS server downloads and unrelated game data can dwarf the actual runtime.
    $topDirectory=($entry.path -split '[\\/]')[0]
    if ($entry.path -match '(?i)\.(i64|idb|id0|id1|id2|nam|til)$' -or
        $topDirectory -in $copyPolicy.excludedCopiedDirectories) { continue }
    $source = [IO.Path]::GetFullPath((Join-Path $script:GoldCraftOriginal $entry.path))
    if (-not $source.StartsWith($script:GoldCraftOriginal + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid baseline relative path.' }
    Assert-NoReparsePath $source
    $target = Assert-SandboxPath (Join-Path $destination $entry.path)
    if (-not $target.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid copy destination.' }
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Copy hash mismatch: $target" }
    $copied++
    if ($copied % 2000 -eq 0) { Write-Host "Copied and verified $copied independent files." }
}
@{ original = $script:GoldCraftOriginal; path = $destination; count = $copied; createdUtc = [DateTime]::UtcNow.ToString('O') } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path (Split-Path $destination) 'copy-manifest.json') -Encoding utf8
Write-Host "Sandbox ready: $destination ($copied independently copied files)."
