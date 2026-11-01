param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers', [switch]$Plan, [switch]$Restore)
. (Join-Path $PSScriptRoot 'Common.ps1')
. (Join-Path $PSScriptRoot 'Retired-Plugins.ps1')

function Add-Text([string]$Relative, [string]$Text) {
    $script:textChanges[$Relative] = $Text
    $script:sources.Remove($Relative)
}
function Set-Cvar([string]$Text, [string]$Cvar, [string]$Value) {
    $kept = @($Text -split '\r?\n' | Where-Object { $_ -notmatch ('^\s*' + [regex]::Escape($Cvar) + '(?=\s|$)') })
    return (($kept -join "`r`n").TrimEnd() + "`r`n" + $Cvar + ' ' + $Value + "`r`n")
}

try {
    if ($Plan -and $Restore) { throw 'Choose -Plan or -Restore.' }
    $manifestPath = Join-Path $PSScriptRoot 'update-manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.schema -ne 2 -or $manifest.scope -ne 'cs-only') { throw 'Wrong server package.' }
    if ([string]::IsNullOrWhiteSpace($TargetDirectory)) { throw 'No server folder selected.' }
    $candidate = $TargetDirectory.Trim().Trim('"').Trim("'")
    if ($candidate -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)') { throw 'Enter an absolute folder path.' }
    $target = (Assert-PlainPath $candidate).TrimEnd('\')
    if (Test-Path -LiteralPath (Join-Path $target 'cs/hlds.exe')) { $target = Join-CheckedPath $target 'cs' }
    foreach ($relative in @('hlds.exe','cstrike/server.cfg','cstrike/liblist.gam',
                            'cstrike/addons/amxmodx/configs/zombieplague.ini',
                            'cstrike/addons/metamod/plugins.ini')) {
        if (!(Test-Path -LiteralPath (Join-CheckedPath $target $relative) -PathType Leaf)) {
            throw "Select the existing GoldCraft/ZP dedicated server folder. Missing: $relative"
        }
    }
    $packageRoot = (Assert-PlainPath $PSScriptRoot).TrimEnd('\')
    if ($target -eq $packageRoot -or $target.StartsWith($packageRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or
        $packageRoot.StartsWith($target + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Extract the update outside the installed server.' }
    $state = Join-CheckedPath $target '.goldcraft-cs-updates'
    $latest = Join-CheckedPath $state 'latest.json'
    if ($Restore) {
        Assert-GameStopped $target
        $record = Get-Content -LiteralPath $latest -Raw | ConvertFrom-Json
        $backup = Join-CheckedPath $state $record.backup
        $transaction = Get-Content -LiteralPath (Join-Path $backup 'transaction.json') -Raw | ConvertFrom-Json
        if ($transaction.status -ne 'installed') { throw 'Latest transaction is not an installed update.' }
        Restore-Transaction $target $backup $transaction -CheckInstalled
        $transaction.status = 'restored'
        Write-Json (Join-Path $backup 'transaction.json') $transaction
        Write-Host 'Previous server files restored. Start with your existing launcher.'
        exit 0
    }
    $sources = @{}; $textChanges = @{}; $expected = @{}
    foreach ($row in $manifest.files) {
        $source = Join-CheckedPath $packageRoot $row.path
        if ($expected.ContainsKey($row.path)) { throw 'Duplicate package path.' }
        $expected[$row.path] = $true
        if ((Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Get-Sha256 $source) -ne $row.sha256) {
            throw "Package integrity failure: $($row.path)"
        }
        if ($row.path.StartsWith('payload/cs/')) {
            $relative = $row.path.Substring('payload/cs/'.Length)
            $destination = Join-CheckedPath $target $relative
            if ($sources.ContainsKey($relative)) { throw 'Duplicate destination.' }
            if ($relative -in $manifest.preserveIfExists -and (Test-Path -LiteralPath $destination -PathType Leaf)) { continue }
            $sources[$relative] = $source
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $packageRoot 'payload') -Recurse -File) {
        [void](Assert-PlainPath $file.FullName)
        $relative = $file.FullName.Substring($packageRoot.Length + 1).Replace('\','/')
        if (!$expected.ContainsKey($relative)) { throw "Unlisted payload file: $relative" }
    }

    # Keep unrelated entries and debug flags. Move managed plugins to their
    # canonical dependency order and block retired aliases across every list.
    $owners = @{}
    foreach ($group in $manifest.pluginGroups) {
        foreach ($plugin in $group.plugins) {
            if ($owners.ContainsKey($plugin)) { throw 'Duplicate managed plugin.' }
            $owners[$plugin] = $group.path
        }
    }
    $config = Join-CheckedPath $target 'cstrike/addons/amxmodx/configs'
    $listings = @('cstrike/addons/amxmodx/configs/plugins.ini') + @($manifest.pluginGroups | ForEach-Object { $_.path })
    $listings += @(Get-ChildItem -LiteralPath $config -Filter 'plugins-*.ini' -File |
        ForEach-Object { 'cstrike/addons/amxmodx/configs/' + $_.Name })
    foreach ($relative in $listings | Sort-Object -Unique) {
        $original = Read-TextOrEmpty (Join-CheckedPath $target $relative)
        $flags = @{}; $lines = [Collections.Generic.List[string]]::new(); $seen = @{}
        foreach ($line in $original -split '\r?\n') {
            $parts = @(($line -split ';',2)[0].Trim() -split '\s+')
            $plugin = $parts[0]
            if ($plugin -in $manifest.retiredPlugins) { $lines.Add((Disable-RetiredPluginLines $line @($plugin))); continue }
            if ($owners.ContainsKey($plugin)) {
                if ($owners[$plugin] -ne $relative -or $seen.ContainsKey($plugin)) { continue }
                $seen[$plugin] = $true
                if ($parts.Count -gt 1 -and $parts[1] -eq 'debug') { $flags[$plugin] = ' debug' }
                $lines.Add($plugin)
            } else { $lines.Add($line) }
        }
        $text = $lines -join "`r`n"
        $group = @($manifest.pluginGroups | Where-Object { $_.path -eq $relative })
        if ($group.Count) {
            $text = Merge-Plugins $text $group[0].plugins
            foreach ($plugin in $flags.Keys) {
                $text = [regex]::Replace($text, '(?m)^' + [regex]::Escape($plugin) + '\r?$', $plugin + $flags[$plugin])
            }
        }
        if ($text -cne $original) { Add-Text $relative $text }
    }
    $retiredNames = @($manifest.retiredPlugins) + @('addon_floating_damage.amxx')
    $retireChanges = Get-RetiredPluginChanges $target 'cstrike/addons/amxmodx/configs' $textChanges ($retiredNames | Sort-Object -Unique)
    foreach ($key in $retireChanges.Keys) { Add-Text $key $retireChanges[$key] }
    $modulesPath = 'cstrike/addons/amxmodx/configs/modules.ini'
    $modules = Read-TextOrEmpty (Join-CheckedPath $target $modulesPath)
    foreach ($module in @('reapi','goldcraft','sypb')) {
        if (!@($modules -split '\r?\n' | Where-Object { ($_ -split ';',2)[0].Trim() -match ('^' + $module + '(?:_amxx(?:\.dll)?)?$') }).Count) {
            $modules = $modules.TrimEnd() + "`r`n" + $module + "`r`n"
        }
    }
    Add-Text $modulesPath $modules
    # game_init runs before map precache; server.cfg alone is too late.
    foreach ($relative in @('cstrike/game_init.cfg','cstrike/server.cfg')) {
        $original = Read-TextOrEmpty (Join-CheckedPath $target $relative)
        Add-Text $relative (Set-Cvar $original 'csol_weapons' '1')
    }
    # Migrate only the six renamed independent ZP rules, preserving values.
    $cfgs = @('cstrike/server.cfg','cstrike/autoexec.cfg') + @(Get-ChildItem -LiteralPath $config -Filter '*.cfg' -Recurse -File |
        ForEach-Object { $_.FullName.Substring($target.Length + 1).Replace('\','/') })
    foreach ($relative in $cfgs | Sort-Object -Unique) {
        $original = if ($textChanges.ContainsKey($relative)) { $textChanges[$relative] } else { Read-TextOrEmpty (Join-CheckedPath $target $relative) }
        $updated = [regex]::Replace($original, '(?mi)^([ \t]*)mc_(zombie_armor|zombie_respawn_delay|human_respawn_delay|zombie_headshot_extra_knockback|zombie_blood|zombie_gib_damage)(?=[ \t]|$)', '${1}zp_${2}')
        if ($updated -cne $original) { Add-Text $relative $updated }
    }
    $rows = @(foreach ($relative in @(@($sources.Keys) + @($textChanges.Keys) | Sort-Object -Unique)) {
        $destination = Join-CheckedPath $target $relative
        $before = if (Test-Path -LiteralPath $destination -PathType Leaf) { Get-Sha256 $destination } else { $null }
        if ($sources.ContainsKey($relative)) { $after = Get-Sha256 $sources[$relative] }
        else {
            $hash = [Security.Cryptography.SHA256]::Create()
            try { $after = [BitConverter]::ToString($hash.ComputeHash([Text.UTF8Encoding]::new($false).GetBytes($textChanges[$relative]))).Replace('-','').ToLowerInvariant() }
            finally { $hash.Dispose() }
        }
        if ($before -ne $after) { [pscustomobject]@{path=$relative;existed=($null -ne $before);beforeSha256=$before;afterSha256=$after} }
    })
    if ($Plan) { [ordered]@{release=$manifest.release;files=$rows.Count;changes=$rows;applied=$false} | ConvertTo-Json -Depth 5; exit 0 }
    Assert-GameStopped $target
    if (!$rows.Count) { Write-Host 'Already current; no files changed.'; exit 0 }
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $backup = Join-CheckedPath $state ('backups/' + $id)
    [IO.Directory]::CreateDirectory($backup) | Out-Null
    foreach ($row in $rows) {
        if ($row.existed) {
            $saved = Join-CheckedPath $backup ('files/' + $row.path)
            [IO.Directory]::CreateDirectory((Split-Path -Parent $saved)) | Out-Null
            [IO.File]::Copy((Join-CheckedPath $target $row.path), $saved, $false)
            if ((Get-Sha256 $saved) -ne $row.beforeSha256) { throw 'Backup verification failed.' }
        }
    }
    $transaction = [pscustomobject]@{status='prepared';release=$manifest.release;files=$rows}
    Write-Json (Join-Path $backup 'transaction.json') $transaction
    $completed = [Collections.Generic.List[object]]::new()
    try {
        Assert-GameStopped $target
        foreach ($row in $rows) {
            $path = Join-CheckedPath $target $row.path
            $actual = if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Sha256 $path } else { $null }
            if ($actual -ne $row.beforeSha256) { throw "Target changed: $($row.path)" }
            [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
            $temporary = $path + '.goldcraft-' + [Guid]::NewGuid().ToString('N') + '.tmp'
            try {
                if ($sources.ContainsKey($row.path)) { [IO.File]::Copy($sources[$row.path], $temporary, $false) }
                else { [IO.File]::WriteAllText($temporary, $textChanges[$row.path], [Text.UTF8Encoding]::new($false)) }
                if ((Get-Sha256 $temporary) -ne $row.afterSha256) { throw 'Source changed while installing.' }
                if ($row.existed) { [IO.File]::Replace($temporary,$path,[NullString]::Value) }
                else { [IO.File]::Move($temporary,$path) }
            } finally { if (Test-Path -LiteralPath $temporary) { [IO.File]::Delete($temporary) } }
            $completed.Add($row)
            if ((Get-Sha256 $path) -ne $row.afterSha256) { throw 'Installed hash mismatch.' }
        }
        $transaction.status = 'installed'
        Write-Json (Join-Path $backup 'transaction.json') $transaction
        Write-Json $latest ([ordered]@{backup='backups/' + $id})
    } catch {
        Restore-Transaction $target $backup ([pscustomobject]@{files=@($completed)})
        $transaction.status = 'rolled-back'
        Write-Json (Join-Path $backup 'transaction.json') $transaction
        throw
    }
    Write-Host "Updated $($rows.Count) CS server files. Backup: $backup"
    Write-Host 'Start using your original Start.cmd. Minecraft, ports, passwords and admins were preserved.'
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }
