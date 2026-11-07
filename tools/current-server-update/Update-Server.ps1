param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers',[switch]$Plan,[switch]$Restore)
. (Join-Path $PSScriptRoot 'Files.ps1')
. (Join-Path $PSScriptRoot 'Scenario.ps1')
. (Join-Path $PSScriptRoot 'Configuration.ps1')
. (Join-Path $PSScriptRoot 'Publication.ps1')
$lock=$null
$scope='remote-deployed-delta'
$pins=@{
    'cs/swds.dll'='5f869363a5e116ea01eaee924bd332f4521fba725e742569b7ce70c03c339549'
    'cs/filesystem_stdio.dll'='14bedc219a27811576faacddc316ae522e566c802f9c3852be1896277a7ac553'
    'cs/cstrike/dlls/mp.dll'='481b52d78644e9a89ec52b343abff70b0fb292f82f2e28f6aa6d2d3b640aeefd'
}
function Config-Path([string]$Name){
    return $Name -cin @('server-settings.json','scripts/Run-Server.ps1','scripts/Start-Servers.ps1',
        'cs/cstrike/game_init.cfg','cs/cstrike/server.cfg','cs/cstrike/addons/sypb/sypb.cfg',
        'cs/cstrike/addons/amxmodx/configs/goldcraft/game_modes.ini','cs/cstrike/addons/amxmodx/configs/modules.ini') -or
        $Name -cmatch '^cs/cstrike/addons/amxmodx/configs/plugins(?:-[A-Za-z0-9_-]+)?\.ini$'
}
function Payload-Path([string]$Name){
    if($Name -cin @('scripts/Start-Scenario.ps1','cs/swds.dll','cs/filesystem_stdio.dll','cs/cstrike/dlls/mp.dll','cs/launcher-public/manifest.json',
        'cs/cstrike/delta.lst','cs/cstrike/addons/sypb/sypb.dll','cs/cstrike/bingsang_scenario_cycle.txt')){return $true}
    if($Name -cmatch '^cs/launcher-public/blobs/[0-9a-f]{64}/[A-Za-z0-9_]+\.dll$'){return $true}
    if($Name -cmatch '^cs/cstrike/addons/(amxmodx/(plugins/[A-Za-z0-9_]+\.amxx|modules/[A-Za-z0-9_]+\.dll|dlls/amxmodx_mm\.dll|data/lang/[A-Za-z0-9_]+\.txt)|metamod/metamod\.dll)$'){
        return $Name -notmatch '(?i)(test|probe|fixture|floating_damage)'
    }
    return $Name -match '^cs/cstrike/(?:(?:bgm|events|gfx|maps|materials|models|overviews|resource|shader|sound|sprites|classes|classes2|csol/weapons)/.+\.(?:bsp|wad|mdl|seq|spr|wav|mp3|ogg|tga|bmp|dds|png|jpg|jpeg|pcx|lmp|ttf|otf|webm|res|txt|json|cso|csv|jsoo|sc|lst|nav)|[^/]+\.(?:wad|lmp))$'
}
function Checked-Row($Row){
    if(!(Payload-Path $Row.path) -or $Row.sha256 -cnotmatch '^[0-9a-f]{64}$' -or $Row.bytes -le 0){throw 'Invalid payload identity or scope'}
    [void](Under $target $Row.path)
    foreach($digest in $Row.priorSha256){if($digest -cnotmatch '^[0-9a-f]{64}$'){throw 'Invalid prior identity'}}
}
function Changes {
    $result=@(foreach($row in $manifest.files){
        $before=Hash (Under $target $row.path)
        if($before -ceq $row.sha256){continue}
        if($before -and $before -cnotin $row.priorSha256){throw "Unknown existing file; preserved without overwriting: $($row.path)"}
        [pscustomobject]@{path=$row.path;existed=($null -ne $before);before=$before;after=$row.sha256;kind='payload'}
    })
    $script:configuration=Get-ConfigurationChanges $target $manifest
    $script:configurationBytes=@{}
    foreach($name in $configuration.Keys){
        if(!(Config-Path $name)){throw 'Unexpected configuration scope'}
        $bytes=[Text.UTF8Encoding]::new($false).GetBytes($configuration[$name])
        if($name.EndsWith('.ps1')){$bytes=[byte[]](@(239,187,191)+$bytes)}
        $configurationBytes[$name]=$bytes
        $before=Hash (Under $target $name);$after=Hash-Bytes $bytes
        if($before -cne $after){$result+=@([pscustomobject]@{path=$name;existed=($null -ne $before);before=$before;after=$after;kind='configuration'})}
    }
    return $result
}
function Check-Journal($Journal){
    if($Journal.scope -cne $scope -or $Journal.release -cne $manifest.release -or $Journal.manifestSha256 -cne $manifestSha -or
       $Journal.status -notin @('prepared','installed','restored') -or !@($Journal.files).Count){throw 'Invalid rollback identity'}
    $seen=@{}
    foreach($row in $Journal.files){
        [void](Under $target $row.path)
        if($seen.ContainsKey($row.path) -or $row.after -cnotmatch '^[0-9a-f]{64}$'){throw 'Invalid rollback record'}
        $seen[$row.path]=$true
        if($row.kind -ceq 'payload'){
            if(!$files.ContainsKey($row.path) -or $row.after -cne $files[$row.path].sha256 -or
               ($row.existed -and $row.before -cnotin $files[$row.path].priorSha256)){throw 'Rollback payload mismatch'}
        }elseif($row.kind -cne 'configuration' -or !(Config-Path $row.path)){throw 'Invalid rollback configuration path'}
        if($row.existed){
            if($row.before -cnotmatch '^[0-9a-f]{64}$' -or (Hash (Under $state ('files/'+$row.path))) -cne $row.before){throw 'Backup missing or changed'}
        }elseif($null -ne $row.before){throw 'Invalid absent backup identity'}
    }
}
function Unlocked($Rows){
    foreach($row in $Rows){
        $path=Under $target $row.path
        if(Test-Path -LiteralPath $path -PathType Leaf){$f=[IO.File]::Open($path,'Open','ReadWrite','None');$f.Dispose()}
    }
}
try {
    if($Plan -and $Restore){throw 'Choose -Plan or -Restore'}
    if([string]::IsNullOrWhiteSpace($TargetDirectory)){throw 'TargetDirectory cannot be empty'}
    $entered=$TargetDirectory.Trim().Trim('"').Trim("'")
    if($entered -notmatch '^(?:[A-Za-z]:[\\/]|\\\\[^\\]+\\[^\\]+)'){throw 'Use an absolute server root'}
    $target=(Plain $entered).TrimEnd('\');$package=(Plain $PSScriptRoot).TrimEnd('\')
    if($target -eq $package -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase) -or
       $package.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Extract the update outside the existing server'}
    foreach($name in @('Start.cmd','server-settings.json','scripts/Run-Server.ps1','scripts/Start-Servers.ps1','cs/hlds.exe','cs/swds.dll','cs/cstrike/dlls/mp.dll')){
        if(!(Test-Path -LiteralPath (Under $target $name) -PathType Leaf)){throw "Missing existing server file: $name"}
    }
    $manifestFile=Under $package 'update-manifest.json';$manifestSha=Hash $manifestFile
    $manifest=Text $manifestFile|ConvertFrom-Json
    if($manifest.schema -ne 1 -or $manifest.scope -cne $scope -or $manifest.release -cne '20261107-Deployed' -or
       !@($manifest.files).Count -or !@($manifest.requiredBaseline).Count){throw 'Wrong deployed-version update manifest'}
    $files=@{};$all=@{}
    foreach($row in @($manifest.files)+@($manifest.requiredBaseline)){
        Checked-Row $row
        if($all.ContainsKey($row.path)){throw 'Duplicate manifest path'}
        $all[$row.path]=$row
    }
    foreach($row in $manifest.files){$files[$row.path]=$row}
    foreach($name in $pins.Keys){if(!$all.ContainsKey($name) -or $all[$name].sha256 -cne $pins[$name]){throw 'Wrong deployed DLL pin'}}
    foreach($name in $manifest.requiredPlugins){
        if($name -cnotmatch '^[A-Za-z0-9_]+\.amxx$' -or !$all.ContainsKey('cs/cstrike/addons/amxmodx/plugins/'+$name)){throw 'Missing required production plugin'}
    }
    $state=Under $target '.bingsang-current-update/20261107';$journalPath=Under $state 'transaction.json'
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal}
    if($Restore){
        if(!$journal){throw 'No rollback record exists'}
        Stopped $target;Unlocked $journal.files
        $lock=[IO.File]::Open((Under $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
        $journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal
        if($journal.status -ne 'restored'){Restore-Files $target $state $journal}
        Write-Host 'Previous files and configuration restored. Use the original Start.cmd.';exit 0
    }
    Write-Host 'Checking release prerequisites...'
    foreach($name in $pins.Keys){
        $row=$all[$name];$before=Hash (Under $target $name)
        if($before -cne $row.sha256 -and $before -cnotin $row.priorSha256){throw "Unknown existing file; preserved without overwriting: $name"}
        if($files.ContainsKey($name)){
            $path=Under $package ('payload/'+$name)
            if((Hash $path) -cne $row.sha256){throw "Package integrity failure: $name"}
        }
    }
    if(!$Plan){Stopped $target;Unlocked @($pins.Keys|ForEach-Object {[pscustomobject]@{path=$_}})}
    foreach($row in $manifest.requiredBaseline){
        if(!(Test-Path -LiteralPath (Under $target $row.path) -PathType Leaf)){
            throw "Missing/changed baseline. Apply $($row.baselineRelease) first: $($row.path)"
        }
    }
    Write-Host 'Checking package files...'
    $allowed=@{};$i=0
    foreach($row in $manifest.files){
        $source=Under $package ('payload/'+$row.path);$allowed[$source]=$true
        if(!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -ne $row.bytes -or (Hash $source) -cne $row.sha256){throw "Package integrity failure: $($row.path)"}
        if((++$i % 100) -eq 0){Write-Host "Package $i / $(@($manifest.files).Count)"}
    }
    foreach($entry in Get-ChildItem -LiteralPath (Under $package 'payload') -Recurse -Force){
        [void](Plain $entry.FullName)
        if(!$entry.PSIsContainer -and !$allowed.ContainsKey($entry.FullName)){throw 'Unlisted payload file'}
    }
    Check-Publication $package $target $manifest $all
    $rows=@(Changes)
    if($journal -and $journal.status -eq 'installed' -and $rows.Count){
        throw 'This release is already installed, but files or settings changed afterwards. Original backups are retained. Restore the recorded installation before applying it again.'
    }
    # Check occupancy before the larger omitted-resource scan.
    if(!$Plan){Stopped $target;Unlocked $rows}
    Write-Host 'Checking omitted resources from previously issued packages...'
    $i=0
    foreach($row in $manifest.requiredBaseline){
        $path=Under $target $row.path
        if(!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne $row.bytes -or (Hash $path) -cne $row.sha256){
            throw "Missing/changed baseline. Apply $($row.baselineRelease) first: $($row.path)"
        }
        if((++$i % 100) -eq 0){Write-Host "Baseline $i / $(@($manifest.requiredBaseline).Count)"}
    }
    if($Plan){[ordered]@{release=$manifest.release;changed=$rows.Count;baselineVerified=$i;pendingRecovery=($journal -and $journal.status -eq 'prepared');applied=$false}|ConvertTo-Json;exit 0}
    if(!$rows.Count -and (!$journal -or $journal.status -ne 'prepared')){Write-Host 'Already current. No files copied.';exit 0}
    [IO.Directory]::CreateDirectory($state)|Out-Null
    $lock=[IO.File]::Open((Under $state 'operation.lock'),'OpenOrCreate','ReadWrite','None')
    $journal=$null
    if(Test-Path -LiteralPath $journalPath){$journal=Text $journalPath|ConvertFrom-Json;Check-Journal $journal}
    if($journal -and $journal.status -eq 'prepared'){Restore-Files $target $state $journal}
    $rows=@(Changes);Stopped $target;Unlocked $rows
    if($journal -and $journal.status -eq 'installed' -and $rows.Count){throw 'Installed transaction changed; original rollback retained. Restore before reapplying.'}
    if(!$rows.Count){Write-Host 'Already current. No files copied.';exit 0}
    Write-Host "Backing up $($rows.Count) changed files..."
    foreach($row in $rows){if($row.existed){
        $saved=Under $state ('files/'+$row.path);Atomic $saved (Under $target $row.path) $null
        if((Hash $saved) -cne $row.before){throw 'Destination changed during backup'}
    }}
    $journal=[pscustomobject]@{scope=$scope;release=$manifest.release;manifestSha256=$manifestSha;status='prepared';files=$rows}
    Json $journalPath $journal
    try {
        Stopped $target;Unlocked $rows;$i=0
        foreach($row in @($rows|Sort-Object @{Expression={if($_.path -ceq 'cs/launcher-public/manifest.json'){3}elseif($_.kind -ceq 'configuration'){2}elseif($_.path.EndsWith('.dll')){1}else{0}}},path)){
            $dest=Under $target $row.path
            if((Hash $dest) -cne $row.before){throw 'Destination changed before replacement'}
            if($row.kind -eq 'configuration'){Atomic $dest '' $configurationBytes[$row.path]}
            else{
                $source=Under $package ('payload/'+$row.path)
                if((Hash $source) -cne $row.after){throw 'Payload changed during installation'}
                Atomic $dest $source $null
            }
            if((Hash $dest) -cne $row.after){throw 'Installed content verification failed'}
            if((++$i % 100) -eq 0){Write-Host "Installed $i / $($rows.Count)"}
        }
        $journal.status='installed';Json $journalPath $journal
    }catch{Restore-Files $target $state $journal;throw}
    Write-Host "Updated $($rows.Count) files. Verified $(@($manifest.requiredBaseline).Count) omitted baseline files."
    Write-Host 'Mode 2 / zs_deadend ready. Ports, HTTP settings, passwords, admins, database and Minecraft preserved.'
    Write-Host 'Restart with the original Start.cmd. Players update the two core DLLs with Bingsang Launcher.'
}catch{Write-Error ($_.ToString()+"`n"+$_.ScriptStackTrace) -ErrorAction Continue;exit 1}finally{if($lock){$lock.Dispose()}}
