param([string]$GameDirectory, [switch]$Plan, [switch]$Restore)
. (Join-Path $PSScriptRoot 'Common.ps1')

try {
    if ($Plan -and $Restore) { throw 'Choose either -Plan or -Restore.' }
    $packageRoot = Assert-PlainPath $PSScriptRoot
    $manifest = Get-Content -LiteralPath (Join-Path $packageRoot 'package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($manifest.schema -ne 1 -or $manifest.appId -ne 10 -or $manifest.modDirectory -ne 'cstrike') { throw 'Unsupported package manifest.' }
    $expected = @{}
    foreach ($row in $manifest.files) {
        $path = Join-CheckedPath $packageRoot $row.path
        if ($expected.ContainsKey($row.path)) { throw 'Duplicate package path.' }
        $expected[$row.path] = $true
        if ((Get-Sha256 $path) -ne $row.sha256) { throw "Package integrity failure: $($row.path)" }
    }
    $payload = Join-Path $packageRoot 'install/output'
    foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File) {
        $relative = $file.FullName.Substring($packageRoot.Length + 1).Replace('\', '/')
        [void](Assert-PlainPath $file.FullName)
        if (!$expected.ContainsKey($relative)) { throw "Unlisted payload file: $relative" }
    }
    $cli = Join-Path $packageRoot 'MetahookInstallerCLI.exe'
    if ((Get-Sha256 $cli) -ne $manifest.officialInstaller.cliSha256) { throw 'Official installer identity mismatch.' }
    $query = @('-appid', '10', '-moddir', 'cstrike', '-describe-target')
    if ($GameDirectory) { $query += @('-gamedir', (Assert-PlainPath $GameDirectory)) }
    # Steam discovery, validation and launcher selection belong to upstream.
    $description = & $cli @query
    if ($LASTEXITCODE -ne 0) { throw 'Official game discovery failed. Pass -GameDirectory with the CS game root.' }
    $target = ($description -join "`n") | ConvertFrom-Json
    $game = (Assert-PlainPath $target.GameDirectory).TrimEnd('\')
    if ($target.ModDirectory -ne 'cstrike' -or [IO.Path]::GetFileName($target.LauncherPath) -ne 'MetaHook.exe') { throw 'This package requires normal GoldSrc Counter-Strike 1.6.' }
    if ($game -eq $packageRoot -or $game.StartsWith($packageRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or $packageRoot.StartsWith($game + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Extract this package outside the game directory.' }
    $stateRoot = Join-CheckedPath $game '.goldcraft-installer'
    $latest = Join-CheckedPath $stateRoot 'latest.json'
    if ($Restore) {
        Assert-GameStopped $game
        $last = Get-Content -LiteralPath $latest -Raw | ConvertFrom-Json
        $backup = Join-CheckedPath $stateRoot $last.backup
        $transaction = Get-Content -LiteralPath (Join-Path $backup 'transaction.json') -Raw | ConvertFrom-Json
        if ($transaction.status -ne 'installed') { throw 'The last transaction is not an installed version.' }
        Restore-Transaction $game $backup $transaction -CheckInstalled
        $transaction.status = 'restored'
        Write-Json (Join-Path $backup 'transaction.json') $transaction
        Write-Host 'Previous files restored. Game configuration and key bindings were preserved.'
        exit 0
    }
    if ((Get-Sha256 (Join-CheckedPath $game 'hw.dll')) -ne $manifest.supportedGame.engineSha256 -or
        (Get-Sha256 (Join-CheckedPath $game 'cstrike/cl_dlls/client.dll')) -ne $manifest.supportedGame.clientSha256) {
        throw 'This engine/client version has not been matched to the packaged GoldCraft hooks. No game files were changed.'
    }
    $destinations = @{}
    foreach ($row in $manifest.files) {
        if (!$row.path.StartsWith('install/output/')) { continue }
        $relative = $row.path.Substring('install/output/'.Length)
        if ($relative.StartsWith('svencoop/')) { $relative = 'cstrike/' + $relative.Substring('svencoop/'.Length) }
        elseif ($relative -notin @('MetaHook.exe', 'SDL2.dll', 'SDL3.dll')) { throw "Unsupported official payload mapping: $relative" }
        [void](Join-CheckedPath $game $relative)
        $destinations[$relative] = $row.sha256
    }
    $pluginsPath = Join-CheckedPath $game 'cstrike/metahook/configs/plugins.lst'
    $pathsPath = Join-CheckedPath $game 'cstrike/metahook/configs/dllpaths.lst'
    $pluginText = Merge-Plugins (Read-TextOrEmpty $pluginsPath) $manifest.plugins
    $dllText = Merge-DllPaths (Read-TextOrEmpty $pathsPath) $manifest.dllPaths
    $obsoleteAliases = @()
    foreach ($alias in Get-ManagedPluginCandidates $manifest.plugins) {
        if (!$destinations.ContainsKey($alias)) {
            $obsoleteAliases += $alias
            $destinations[$alias] = $null
            [void](Join-CheckedPath $game $alias)
        }
    }
    foreach ($name in @('cstrike/metahook/configs/plugins.lst', 'cstrike/metahook/configs/dllpaths.lst',
                        'cstrike/metahook/configs/plugins_goldsrc.lst', 'cstrike/metahook/configs/plugins_svencoop.lst',
                        'MetaHook for Counter-Strike.lnk')) { $destinations[$name] = $null; [void](Join-CheckedPath $game $name) }
    if ($Plan) {
        [ordered]@{ gameDirectory=$game; installer='Official MetahookInstallerCLI'; files=$destinations.Count;
                    plugins=$manifest.plugins; mergedPluginList=$pluginText; changesApplied=$false } | ConvertTo-Json -Depth 5
        exit 0
    }
    Assert-GameStopped $game
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $backup = Join-CheckedPath $stateRoot ('backups/' + $id)
    [IO.Directory]::CreateDirectory($backup) | Out-Null
    $rows = foreach ($relative in ($destinations.Keys | Sort-Object)) {
        $path = Join-CheckedPath $game $relative
        $exists = Test-Path -LiteralPath $path -PathType Leaf
        $hash = $null
        if ($exists) {
            $hash = Get-Sha256 $path
            $saved = Join-CheckedPath $backup ('files/' + $relative)
            [IO.Directory]::CreateDirectory((Split-Path -Parent $saved)) | Out-Null
            [IO.File]::Copy($path, $saved, $false)
            if ((Get-Sha256 $saved) -ne $hash) { throw 'Backup verification failed.' }
        }
        [pscustomobject]@{ path=$relative; existed=$exists; beforeSha256=$hash; afterSha256=$null }
    }
    $transaction = [pscustomobject]@{ status='prepared'; files=@($rows); packageSha256=(Get-Sha256 (Join-Path $packageRoot 'package.json')) }
    Write-Json (Join-Path $backup 'transaction.json') $transaction
    try {
        Push-Location $game
        try { & $cli -appid 10 -gamedir $game -moddir cstrike; if ($LASTEXITCODE -ne 0) { throw 'Official installation failed.' } }
        finally { Pop-Location }
        [IO.Directory]::CreateDirectory((Split-Path -Parent $pluginsPath)) | Out-Null
        [IO.File]::WriteAllText($pluginsPath, $pluginText, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText($pathsPath, $dllText, [Text.UTF8Encoding]::new($false))
        foreach ($alias in $obsoleteAliases) {
            $path = Join-CheckedPath $game $alias
            if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
        }
        foreach ($relative in $destinations.Keys) {
            $hash = $destinations[$relative]
            # Upstream conditionally installs SDL only when hw.dll imports SDL2.
            if ($relative -in @('SDL2.dll', 'SDL3.dll')) {
                $path = Join-CheckedPath $game $relative
                $actual = if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Sha256 $path } else { $null }
                $prior = @($transaction.files | Where-Object { $_.path -eq $relative })[0].beforeSha256
                if ($actual -ne $hash -and $actual -ne $prior) { throw "Unexpected conditional SDL result: $relative" }
                continue
            }
            if ($hash -and (Get-Sha256 (Join-CheckedPath $game $relative)) -ne $hash) { throw "Installed file verification failed: $relative" }
        }
        foreach ($row in $transaction.files) {
            $path = Join-CheckedPath $game $row.path
            $row.afterSha256 = if (Test-Path -LiteralPath $path -PathType Leaf) { Get-Sha256 $path } else { $null }
        }
        $transaction.status = 'installed'
        Write-Json (Join-Path $backup 'transaction.json') $transaction
        Write-Json $latest ([ordered]@{ backup='backups/' + $id })
    } catch {
        Restore-Transaction $game $backup $transaction
        $transaction.status = 'rolled-back'
        Write-Json (Join-Path $backup 'transaction.json') $transaction
        throw
    }
    Write-Host "Installed the GoldCraft plugin set into $game"
    Write-Host 'Use the generated MetaHook for Counter-Strike shortcut. Minecraft pairing still uses your existing GoldCraft launcher.'
    Write-Host 'To restore the previous files, run this script with -Restore and the same -GameDirectory.'
    exit 0
} catch { Write-Error $_ -ErrorAction Continue; exit 1 }
