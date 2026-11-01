param([string]$TargetDirectory,[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
. (Join-Path $PSScriptRoot 'Configure.ps1')
$lock=$null
try {
    if($Plan -and $Restore){throw 'Choose plan or restore'}
    if(!$TargetDirectory){$TargetDirectory=Read-Host 'Server folder containing the original Start.cmd'}
    $target=Plain ($TargetDirectory.Trim().Trim('"'));$package=Plain $PSScriptRoot
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract the update outside the server installation'}
    foreach($rel in @('Start.cmd','server-settings.json','scripts/Start-Servers.ps1','scripts/Common.ps1','scripts/Run-Server.ps1','cs/hlds.exe','cs/cstrike/server.cfg','cs/cstrike/addons/amxmodx/configs/plugins.ini')){
        if(!(Test-Path -LiteralPath (Under $target $rel) -PathType Leaf)){throw "Select the existing server package root. Missing: $rel"}
    }
    $state=Under $target '.bingsang-updates';$latest=Under $state 'latest.json'
    if(!$Plan){
        Stopped $target;[IO.Directory]::CreateDirectory($state)|Out-Null
        $lock=[IO.File]::Open((Under $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
        $backups=Under $state 'backups'
        if(Test-Path -LiteralPath $backups){foreach($dir in Get-ChildItem -LiteralPath $backups -Directory){
            $journalPath=Under $dir.FullName 'transaction.json'
            if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;if($journal.status -eq 'prepared'){Restore-Files $target $dir.FullName $journal}}
        }}
    }
    if($Restore){
        $record=Text $latest|ConvertFrom-Json;$backup=Under $state $record.backup;$journal=Text (Under $backup 'transaction.json')|ConvertFrom-Json
        if($journal.status -ne 'installed'){throw 'Latest update is not installed'}
        Restore-Files $target $backup $journal;Write-Host 'Restored the previous server version and mode.';exit 0
    }
    $manifest=Text (Under $package 'update-manifest.json')|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.scope -ne 'remote-scenario-delta'){throw 'Wrong update package'}
    Write-Host 'Checking changed files and required baseline resources...'
    $sources=@{};$hashes=@{};$allowed=@{};$count=0
    foreach($row in $manifest.files){
        $dest=Under $target $row.path;$src=Under $package ('payload/'+$row.path)
        if($sources.ContainsKey($row.path)){throw 'Duplicate payload destination'}
        if((Get-Item -LiteralPath $src).Length -ne $row.bytes -or (Hash $src) -ne $row.sha256){throw "Package integrity failure: $($row.path)"}
        $sources[$row.path]=$src;$hashes[$row.path]=$row.sha256;$allowed[$src]=$true;$count++
        if(($count%50) -eq 0){Write-Progress -Activity 'Checking update payload' -Status "$count / $($manifest.files.Count)" -PercentComplete ($count*100/$manifest.files.Count)}
    }
    Write-Progress -Activity 'Checking update payload' -Completed
    foreach($file in Get-ChildItem -LiteralPath (Under $package 'payload') -Recurse -Force){
        [void](Plain $file.FullName);if(!$file.PSIsContainer -and !$allowed.ContainsKey($file.FullName)){throw 'Unlisted payload file'}
    }
    $missing=@(foreach($row in $manifest.requiredBaseline){
        $file=Under $target $row.path
        if(!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -ne $row.bytes -or (Hash $file) -ne $row.sha256){$row.path}
    })
    if($missing.Count){throw ('Baseline mismatch; nothing installed. Restore these files from the confirmed previous update: '+($missing -join ', '))}
    $configs=Configure-Scenario $target $manifest;$bytes=@{}
    foreach($rel in $configs.Keys){$bytes[$rel]=[Text.UTF8Encoding]::new($false).GetBytes($configs[$rel]);$hashes[$rel]=Hash-Bytes $bytes[$rel];$sources.Remove($rel)}
    # Existing, byte-identical files are hashed once and never backed up or copied.
    $rows=@(foreach($rel in @($hashes.Keys|Sort-Object)){
        $before=Hash (Under $target $rel)
        if($before -ne $hashes[$rel]){[pscustomobject]@{path=$rel;existed=($null -ne $before);before=$before;after=$hashes[$rel]}}
    })
    $skipped=$hashes.Count-$rows.Count
    if($Plan){[ordered]@{release=$manifest.release;changed=$rows.Count;unchanged=$skipped;baselineVerified=$manifest.requiredBaseline.Count;mode=2;map='zs_deadend';applied=$false;paths=@($rows|ForEach-Object {$_.path})}|ConvertTo-Json -Depth 6;exit 0}
    if(!$rows.Count){Write-Host 'Already current. No files copied.';exit 0}
    Stopped $target
    $id=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $backup=Under $state ('backups/'+$id);[IO.Directory]::CreateDirectory($backup)|Out-Null
    foreach($row in $rows){if($row.existed){
        $saved=Under $backup ('files/'+$row.path);[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($saved))|Out-Null
        [IO.File]::Copy((Under $target $row.path),$saved,$false);if((Hash $saved) -ne $row.before){throw 'Backup source changed'}
    }}
    $journal=[pscustomobject]@{status='prepared';release=$manifest.release;files=$rows};Json (Under $backup 'transaction.json') $journal
    try {
        $done=0
        # Publish the signed manifest last, after all core blobs and configuration.
        foreach($row in @($rows|Sort-Object @{Expression={if($_.path -eq 'cs/launcher-public/manifest.json'){1}else{0}}},path)){
            $dest=Under $target $row.path
            if((Hash $dest) -ne $row.before){throw "Destination changed: $($row.path)"}
            if($sources.ContainsKey($row.path)){Atomic $dest $sources[$row.path] $null}else{Atomic $dest '' $bytes[$row.path]}
            if((Hash $dest) -ne $row.after){throw 'Installed hash mismatch'}
            $done++;Write-Progress -Activity 'Installing scenario update' -Status "$done / $($rows.Count)" -PercentComplete ($done*100/$rows.Count)
        }
        $journal.status='installed';Json (Under $backup 'transaction.json') $journal;Json $latest ([ordered]@{backup='backups/'+$id})
    }catch{Restore-Files $target $backup $journal;throw}
    Write-Progress -Activity 'Installing scenario update' -Completed
    Write-Host "Updated $($rows.Count) files; skipped $skipped identical files. Backup: $backup"
    Write-Host 'Use the original Start.cmd. First map: zs_deadend; scenario mode 2; SyPB quota 0.'
    Write-Host 'Existing public addresses, ports, credentials and admins were preserved.'
}catch{Write-Error $_ -ErrorAction Continue;exit 1}finally{if($lock){$lock.Dispose()}}
