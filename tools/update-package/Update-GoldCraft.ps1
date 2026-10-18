param([Parameter(Mandatory)][ValidateSet('server','cs','minecraft')][string]$Role,
      [string]$TargetDirectory,[switch]$Plan)
. (Join-Path $PSScriptRoot 'Common.ps1')

function Read-UpdateTarget {
    $interactive=[string]::IsNullOrWhiteSpace($TargetDirectory)
    $inputPath=$TargetDirectory
    while($true) {
        if($interactive){$inputPath=Read-Host 'Existing install FOLDER (server: folder containing Start.cmd; empty to cancel)'}
        if([string]::IsNullOrWhiteSpace($inputPath)){throw 'Update cancelled: no target folder was entered.'}
        try {
            $inputPath=$inputPath.Trim()
            if($inputPath.Length -ge 2 -and
               (($inputPath[0] -eq '"' -and $inputPath[-1] -eq '"') -or
                ($inputPath[0] -eq "'" -and $inputPath[-1] -eq "'"))) {
                $inputPath=$inputPath.Substring(1,$inputPath.Length-2).Trim()
            }
            if($inputPath -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)') {
                throw 'Enter a full folder path, for example E:\GoldCraft-Servers.'
            }
            if($inputPath.IndexOfAny([IO.Path]::GetInvalidPathChars()) -ge 0 -or $inputPath -match '[*?]') {
                throw 'The folder path contains invalid characters. Paste only the folder path, without a command.'
            }
            $resolved=(Assert-PlainPath $inputPath).TrimEnd('\')
            if(-not(Test-Path -LiteralPath $resolved -PathType Container)) {
                throw 'Folder not found. Select the installed folder containing Start.cmd, not Start.cmd itself or the update ZIP.'
            }
            return $resolved
        } catch {
            if(-not $interactive){throw}
            Write-Host ('Invalid target: '+$_.Exception.Message) -ForegroundColor Red
        }
    }
}

function Add-TextChange([string]$Relative,[string]$Text) {
    $script:changes[$Relative]=[Text.UTF8Encoding]::new($false).GetBytes($Text)
}
function Assert-Stopped {
    Assert-GameStopped $script:target
    foreach($process in Get-CimInstance Win32_Process -ErrorAction Stop) {
        if($process.Name -match '^javaw?\.exe$' -and $process.CommandLine -and
           $process.CommandLine.Replace('/','\').IndexOf($script:target,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Close Minecraft before updating (PID $($process.ProcessId))."
        }
    }
    if($Role -eq 'server') {
        foreach($name in @('cs','minecraft')) {
            $path=Join-CheckedPath $script:target ".state/$name.json"
            if(Test-Path -LiteralPath $path) {
                $record=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
                $process=Get-Process -Id $record.pid -ErrorAction SilentlyContinue
                if($process -and $process.StartTime.ToUniversalTime().Ticks.ToString() -eq $record.startTicks) {
                    throw 'Use Stop.cmd and wait for both servers to exit first.'
                }
            }
        }
    }
}
function Find-OldCore([string]$ModsRelative) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $mods=Join-CheckedPath $script:target $ModsRelative
    foreach($jar in Get-ChildItem -LiteralPath $mods -Filter '*.jar' -File) {
        [void](Assert-PlainPath $jar.FullName)
        $archive=[IO.Compression.ZipFile]::OpenRead($jar.FullName)
        try {
            if($archive.GetEntry('dev/goldcraft/GoldCraft.class')) {
                $name=$ModsRelative+'/'+$jar.Name
                if(-not $script:changes.ContainsKey($name)){$script:changes[$name]=$null}
            }
        } finally {$archive.Dispose()}
    }
}
function Enable-ServerPlugins {
    $relative='cs/cstrike/addons/amxmodx/configs'
    $directory=Join-CheckedPath $script:target $relative
    $seen=@{}
    $listings=@(Get-ChildItem -LiteralPath $directory -File | Where-Object {$_.Name -eq 'plugins.ini' -or $_.Name -like 'plugins-*.ini'} | ForEach-Object {$relative+'/'+$_.Name})
    $listings+=@($script:changes.Keys | Where-Object {$_ -match ('^'+[regex]::Escape($relative)+'/plugins(?:-[^/]+)?\.ini$')})
    foreach($listing in $listings | Sort-Object -Unique) {
        $path=Join-CheckedPath $script:target $listing
        # A shipped enable-list must not replace an owner's edits to that list.
        # Missing required entries are added below after all existing lists merge.
        $original=if(Test-Path -LiteralPath $path){[IO.File]::ReadAllText($path)}else{[Text.Encoding]::UTF8.GetString($script:changes[$listing])}
        $lines=[Collections.Generic.List[string]]::new()
        foreach($line in $original -split '\r?\n') {
            $parts=@(($line -split ';',2)[0].Trim() -split '\s+')
            if($parts[0] -in $manifest.requiredPlugins) {
                if($seen.ContainsKey($parts[0])){continue}
                $seen[$parts[0]]=$true
                $lines.Add($parts[0])
            } else {$lines.Add($line)}
        }
        $text=$lines -join "`r`n"
        if($text -ne $original -or $script:changes.ContainsKey($listing)){Add-TextChange $listing $text}
    }
    $missing=@($manifest.requiredPlugins | Where-Object {-not $seen.ContainsKey($_)})
    if($missing.Count) {
        $path=$relative+'/plugins-goldcraft-update.ini'
        $prior=Read-TextOrEmpty (Join-CheckedPath $script:target $path)
        if($script:changes.ContainsKey($path)){$prior=[Text.Encoding]::UTF8.GetString($script:changes[$path])}
        Add-TextChange $path ($prior.TrimEnd()+"`r`n"+($missing -join "`r`n")+"`r`n")
    }
    $path=$relative+'/modules.ini'
    $prior=Read-TextOrEmpty (Join-CheckedPath $script:target $path)
    if(-not @($prior -split '\r?\n' | Where-Object {($_ -split ';',2)[0].Trim() -match '^goldcraft(?:_amxx(?:\.dll)?)?$'}).Count) {
        Add-TextChange $path ($prior.TrimEnd()+"`r`ngoldcraft`r`n")
    }
    # Apply only the three requested gameplay defaults, preserving all other settings.
    $path='cs/cstrike/server.cfg';$prior=Read-TextOrEmpty (Join-CheckedPath $script:target $path)
    $kept=@($prior -split '\r?\n' | Where-Object {$_ -notmatch '^\s*(mc_map_mining|mc_round_clear_blocks|mc_death_drop_items)\s+'})
    Add-TextChange $path (($kept -join "`r`n").TrimEnd()+"`r`nmc_map_mining 2`r`nmc_round_clear_blocks 1`r`nmc_death_drop_items 0`r`n")
}

try {
    $manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'update-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    if($manifest.schema -ne 1 -or $Role -notin $manifest.roles){throw 'Wrong update package for this role.'}
    $target=Read-UpdateTarget
    $patchRoot=(Assert-PlainPath $PSScriptRoot).TrimEnd('\')
    if($target -eq $patchRoot -or $target.StartsWith($patchRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $patchRoot.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract this patch outside the installed game/server and select its installed folder.'}
    $changes=@{};$expected=@{}
    foreach($row in $manifest.files) {
        $source=Join-CheckedPath $PSScriptRoot $row.path
        if($expected.ContainsKey($row.path)){throw 'Duplicate manifest path.'}
        $expected[$row.path]=$true
        if($row.role -notin $manifest.roles -or $row.path -cne ('payload/'+$row.role+'/'+$row.destination)){throw 'Invalid manifest role/destination.'}
        if((Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Get-Sha256 $source) -ne $row.sha256){throw "Patch integrity failure: $($row.path)"}
        if($row.role -eq $Role) {
            [void](Join-CheckedPath $target $row.destination)
            if($changes.ContainsKey($row.destination)){throw 'Duplicate target path.'}
            $changes[$row.destination]=[IO.File]::ReadAllBytes($source)
        }
    }
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'payload') -Recurse -File) {
        [void](Assert-PlainPath $file.FullName)
        $relative=$file.FullName.Substring($PSScriptRoot.Length+1).Replace('\','/')
        if(-not $expected.ContainsKey($relative)){throw "Unlisted payload file: $relative"}
    }
    if($Role -eq 'server') {
        foreach($name in @('Start.cmd','server-settings.json','cs/hlds.exe','cs/cstrike/server.cfg','minecraft/mods')) {
            if(-not(Test-Path -LiteralPath (Join-CheckedPath $target $name))){throw "Not an existing GoldCraft server package: $name"}
        }
        Find-OldCore 'minecraft/mods'
        if(Test-Path -LiteralPath (Join-CheckedPath $target 'client-update/mods') -PathType Container){Find-OldCore 'client-update/mods'}
        Enable-ServerPlugins
    } elseif($Role -eq 'cs') {
        if((Get-Sha256 (Join-CheckedPath $target 'hw.dll')) -ne $manifest.supportedGame.engineSha256 -or
           (Get-Sha256 (Join-CheckedPath $target 'cstrike/cl_dlls/client.dll')) -ne $manifest.supportedGame.clientSha256){throw 'CS engine/client build does not match this patch.'}
        $path='cstrike/metahook/configs/plugins.lst'
        Add-TextChange $path (Merge-Plugins (Read-TextOrEmpty (Join-CheckedPath $target $path)) $manifest.plugins)
        $path='cstrike/metahook/configs/dllpaths.lst'
        Add-TextChange $path (Merge-DllPaths (Read-TextOrEmpty (Join-CheckedPath $target $path)) @('FreeImage'))
        foreach($name in Get-ManagedPluginCandidates $manifest.plugins) {
            if(-not $changes.ContainsKey($name) -and (Test-Path -LiteralPath (Join-CheckedPath $target $name))){$changes[$name]=$null}
        }
        foreach($name in @('plugins_goldsrc.lst','plugins_svencoop.lst')) {
            $relative='cstrike/metahook/configs/'+$name
            if(Test-Path -LiteralPath (Join-CheckedPath $target $relative)){$changes[$relative]=$null}
        }
    } else {
        if(-not(Test-Path -LiteralPath (Join-CheckedPath $target 'mods') -PathType Container)){throw 'Select the Minecraft 1.21.1 / NeoForge 21.1.256 instance folder containing mods.'}
        Find-OldCore 'mods'
    }
    $rows=@(foreach($name in @($changes.Keys | Sort-Object)) {
        $path=Join-CheckedPath $target $name
        $before=if(Test-Path -LiteralPath $path -PathType Leaf){Get-Sha256 $path}else{$null}
        $after=$null
        if($null -ne $changes[$name]) {
            $hash=[Security.Cryptography.SHA256]::Create()
            try {$after=[BitConverter]::ToString($hash.ComputeHash([byte[]]$changes[$name])).Replace('-','').ToLowerInvariant()}finally{$hash.Dispose()}
        }
        if($before -ne $after){[pscustomobject]@{path=$name;existed=($null -ne $before);beforeSha256=$before;afterSha256=$after}}
    })
    if($Plan){[ordered]@{role=$Role;files=$rows.Count;changes=$rows;applied=$false}|ConvertTo-Json -Depth 5;exit 0}
    Assert-Stopped
    if(-not $rows.Count){Write-Host 'Already current; no files changed.';exit 0}
    $backup=Join-CheckedPath $target ('.goldcraft-updates/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    [IO.Directory]::CreateDirectory($backup)|Out-Null
    foreach($row in $rows) {
        if($row.existed) {
            $saved=Join-CheckedPath $backup ('files/'+$row.path)
            [IO.Directory]::CreateDirectory((Split-Path -Parent $saved))|Out-Null
            [IO.File]::Copy((Join-CheckedPath $target $row.path),$saved,$false)
            if((Get-Sha256 $saved) -ne $row.beforeSha256){throw 'Backup verification failed.'}
        }
    }
    $transaction=[pscustomobject]@{status='prepared';role=$Role;release=$manifest.release;files=$rows}
    Write-Json (Join-Path $backup 'transaction.json') $transaction
    $completed=[Collections.Generic.List[object]]::new()
    try {
        Assert-Stopped
        foreach($row in $rows) {
            $path=Join-CheckedPath $target $row.path
            $before=if(Test-Path -LiteralPath $path -PathType Leaf){Get-Sha256 $path}else{$null}
            if($before -ne $row.beforeSha256){throw "Target changed: $($row.path)"}
            [IO.Directory]::CreateDirectory((Split-Path -Parent $path))|Out-Null
            if($null -eq $changes[$row.path]){[IO.File]::Delete($path)}else{
                $temporary=$path+'.goldcraft-'+[Guid]::NewGuid().ToString('N')+'.tmp'
                try {
                    [IO.File]::WriteAllBytes($temporary,[byte[]]$changes[$row.path])
                    if($row.existed){[IO.File]::Replace($temporary,$path,[NullString]::Value)}else{[IO.File]::Move($temporary,$path)}
                } finally {if(Test-Path -LiteralPath $temporary){[IO.File]::Delete($temporary)}}
            }
            $completed.Add($row)
            if($row.afterSha256 -and (Get-Sha256 $path) -ne $row.afterSha256){throw 'Installed hash mismatch.'}
        }
        $transaction.status='installed';Write-Json (Join-Path $backup 'transaction.json') $transaction
    } catch {
        Restore-Transaction $target $backup ([pscustomobject]@{files=@($completed)})
        $transaction.status='rolled-back';Write-Json (Join-Path $backup 'transaction.json') $transaction
        throw
    }
    Write-Host "Updated $Role ($($rows.Count) files). Backup: $backup"
    Write-Host 'Restart the game/server to load this version.'
} catch {Write-Error $_;exit 1}
