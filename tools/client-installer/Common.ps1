Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Assert-PlainPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $part = $full
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse points are not supported: $part"
            }
        }
        $parent = [IO.Path]::GetDirectoryName($part)
        if ($parent -eq $part) { break }
        $part = $parent
    }
    return $full
}

function Join-CheckedPath([string]$Root, [string]$Relative) {
    if (!$Relative -or $Relative -match '(^/|\\|:|(^|/)\.\.?(/|$)|//)') { throw "Invalid relative path: $Relative" }
    $rootPath = (Assert-PlainPath $Root).TrimEnd('\')
    $full = Assert-PlainPath (Join-Path $rootPath $Relative)
    if (!$full.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes its root' }
    return $full
}

function Get-ActiveEntry([string]$Line) {
    $entry = ($Line -split '//', 2)[0].Trim()
    if (!$entry -or $entry.StartsWith('#') -or $entry.StartsWith(';')) { return '' }
    return $entry
}

function Merge-Plugins([string]$Text, [string[]]$Required) {
    # Keep unrelated plugins/comments in their original relative order. Only
    # normalize owned entries and the required Renderer/GoldCraft adjacency.
    $managed = @{}
    foreach ($name in $Required) { $managed[($name -replace '_(AVX2|AVX|SSE2|SSE)(?=\.dll$)', '').ToLowerInvariant()] = $name }
    $seen = @{}
    $lines = [Collections.Generic.List[string]]::new()
    foreach ($line in $Text -split '\r?\n') {
        $entry = Get-ActiveEntry $line
        $key = ($entry -replace '_(AVX2|AVX|SSE2|SSE)(?=\.dll$)', '').ToLowerInvariant()
        if ($managed.ContainsKey($key)) {
            if (!$seen.ContainsKey($key)) { $lines.Add($managed[$key]); $seen[$key] = $true }
        } else { $lines.Add($line) }
    }
    while ($lines.Count -gt 0 -and !$lines[$lines.Count - 1]) { $lines.RemoveAt($lines.Count - 1) }
    $previous = -1
    for ($n = 0; $n -lt $Required.Count; $n++) {
        $name = $Required[$n]
        $index = $lines.IndexOf($name)
        if ($index -ge 0 -and ($index -le $previous -or $name -eq 'GoldCraft.dll')) {
            $lines.RemoveAt($index)
            if ($index -le $previous) { $previous-- }
            $index = -1
        }
        if ($index -lt 0) {
            $insert = $lines.Count
            for ($k = $n + 1; $k -lt $Required.Count; $k++) {
                $next = $lines.IndexOf($Required[$k])
                if ($next -gt $previous -and $next -lt $insert) { $insert = $next }
            }
            if ($name -eq 'GoldCraft.dll') { $insert = $previous + 1 }
            $lines.Insert($insert, $name); $index = $insert
        }
        $previous = $index
    }
    $caption = $lines.IndexOf('CaptionMod.dll')
    for ($n = $lines.Count - 1; $n -gt $caption -and $caption -ge 0; $n--) {
        if ((Get-ActiveEntry $lines[$n]) -match '^MetaAudio(?:_AVX2|_AVX|_SSE2|_SSE)?\.dll$') {
            $audio = $lines[$n]; $lines.RemoveAt($n); $lines.Insert($caption, $audio); $caption++
        }
    }
    return ($lines.ToArray() -join "`r`n") + "`r`n"
}

function Get-ManagedPluginCandidates([string[]]$Required) {
    $candidates = @{}
    foreach ($name in $Required) {
        $stem = $name -replace '\.dll$', ''
        $base = $stem -replace '_(AVX2|AVX|SSE2|SSE)$', ''
        # The loader appends CPU suffixes even when the listed name already has
        # one (e.g. Renderer_AVX2_AVX2.dll), and tries plain first under a debugger.
        foreach ($prefix in @($base, $stem)) {
            foreach ($suffix in @('_AVX2.dll','_AVX.dll','_SSE2.dll','_SSE.dll','.dll')) {
                $candidates['cstrike/metahook/plugins/' + $prefix + $suffix] = $true
            }
        }
    }
    return @($candidates.Keys | Sort-Object)
}

function Merge-DllPaths([string]$Text, [string[]]$Required) {
    $seen = @{}
    foreach ($line in $Text -split '\r?\n') { $entry = Get-ActiveEntry $line; if ($entry) { $seen[$entry] = $true } }
    $result = $Text.TrimEnd("`r", "`n")
    foreach ($entry in $Required) { if (!$seen.ContainsKey($entry)) { if ($result) { $result += "`r`n" }; $result += $entry; $seen[$entry] = $true } }
    return $result + "`r`n"
}

function Read-TextOrEmpty([string]$Path) {
    if (Test-Path -LiteralPath $Path -PathType Leaf) { return [IO.File]::ReadAllText($Path) }
    return ''
}

function Assert-GameStopped([string]$Game) {
    if (!(Get-Command Get-CimInstance -ErrorAction SilentlyContinue)) {
        Import-Module (Join-Path $PSHOME 'Modules/CimCmdlets/CimCmdlets.psd1') -ErrorAction Stop
    }
    foreach ($process in Get-CimInstance Win32_Process -ErrorAction Stop) {
        if ($process.ExecutablePath -and $process.ExecutablePath.StartsWith($Game.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Close the game before installation (PID $($process.ProcessId))."
        }
    }
}

function Write-Json([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12) + "`r`n", [Text.UTF8Encoding]::new($false))
}

function Restore-Transaction([string]$Game, [string]$Backup, $Transaction, [switch]$CheckInstalled) {
    foreach ($row in $Transaction.files) {
        $target = Join-CheckedPath $Game $row.path
        if ($row.existed) {
            $saved = Join-CheckedPath $Backup ('files/' + $row.path)
            if ((Get-Sha256 $saved) -ne $row.beforeSha256) { throw "Backup mismatch: $($row.path)" }
        }
        if ($CheckInstalled) {
            $actual = if (Test-Path -LiteralPath $target -PathType Leaf) { Get-Sha256 $target } else { $null }
            if ($actual -ne $row.afterSha256) { throw "File changed since installation; restore stopped: $($row.path)" }
        }
    }
    foreach ($row in $Transaction.files) {
        $target = Join-CheckedPath $Game $row.path
        if ($row.existed) {
            if ((Test-Path -LiteralPath $target -PathType Leaf) -and (Get-Sha256 $target) -eq $row.beforeSha256) { continue }
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
            [IO.File]::Copy((Join-CheckedPath $Backup ('files/' + $row.path)), $target, $true)
        } elseif (Test-Path -LiteralPath $target -PathType Leaf) {
            # Only exact files recorded by this installation, never recursive removal.
            Remove-Item -LiteralPath $target -Force
        }
    }
}
