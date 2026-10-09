param([ValidateSet('cs-client-a','cs-client-b')][string[]]$Clients=@('cs-client-b'), [switch]$ClientOnly,
      [switch]$WithMetaHookRuntime, [switch]$ValidateOnly)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
if($WithMetaHookRuntime -and -not $ClientOnly){throw 'Use -ClientOnly for a MetaHook runtime update.'}
$instances=@($Clients | Select-Object -Unique)
if(-not $ClientOnly){$instances=@('cs-server')+$instances}
$running=Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('MetaHook.exe','hlds.exe') -and $_.ExecutablePath
}
foreach($process in $running){
    foreach($instance in $instances){
        $root=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$instance/Half-Life")
        if(-not $ValidateOnly -and $process.ExecutablePath.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw "Stop the recorded $instance process before native deployment."
        }
    }
}
$files=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
function Queue-File([string]$Source,[string]$Destination){
    $from=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $Source)
    $to=Assert-SandboxPath (Join-Path $script:GoldCraftRoot $Destination)
    $hash=(Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash
    # Built adaptations queued later take precedence over release payloads.
    $files[$to]=@{source=$from;destination=$to;path=$Destination;sha256=$hash}
}
$runtime=$null
$preserved=[Collections.Generic.List[string]]::new()
if($WithMetaHookRuntime){
    $runtimeStage=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/metahook-runtime')
    $runtime=Get-Content -LiteralPath (Join-Path $runtimeStage 'manifest.json') -Raw | ConvertFrom-Json
    $release=(Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json).metahookRelease
    if($runtime.release -ne $release.tag -or $runtime.archiveSha256 -ne $release.sha256){throw 'Prepared MetaHook runtime does not match sources.lock.json.'}
    foreach($file in $runtime.files){
        $source=Assert-WorkspacePath (Join-Path $runtimeStage $file.path)
        if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256){throw "Prepared runtime hash mismatch: $($file.path)"}
    }
}
function Merge-RuntimeList([string]$Instance,[string]$Relative){
    $source=Assert-WorkspacePath (Join-Path $runtimeStage $Relative)
    $destination=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/Half-Life/$Relative")
    if(-not(Test-Path -LiteralPath $destination)){return "dist/metahook-runtime/$Relative"}
    $text=[IO.File]::ReadAllText($destination)
    $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($line in $text -split '\r?\n'){
        $name=($line -split '//',2)[0].Trim()
        if($name -and -not $name.StartsWith('#')){[void]$names.Add($name)}
    }
    $newline=if($text.Contains("`r`n")){"`r`n"}else{"`n"}
    foreach($line in Get-Content -LiteralPath $source){
        $name=($line -split '//',2)[0].Trim()
        if($name -and -not $name.StartsWith('#') -and $names.Add($name)){
            if($text -and -not $text.EndsWith("`n")){$text+=$newline}
            $text+=$name+$newline
        }
    }
    if($Relative.EndsWith('/plugins.lst')){
        $entries=@($text -split '\r?\n' | ForEach-Object {($_ -split '//',2)[0].Trim()} | Where-Object {$_ -and -not $_.StartsWith('#')})
        $renderer=@(0..($entries.Count-1) | Where-Object {$entries[$_] -match '^Renderer(?:_AVX2)?\.dll$'})
        if($renderer.Count -ne 1 -or ($renderer[0]+1) -ge $entries.Count -or $entries[$renderer[0]+1] -ne 'GoldCraft.dll'){
            throw "Preserve the Renderer/GoldCraft adjacency in $Instance before updating its plugin list."
        }
    }
    $candidate="build/metahook-runtime-update/$Instance/$Relative"
    $path=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $candidate)
    New-Item -ItemType Directory -Path (Split-Path $path) -Force | Out-Null
    [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    return $candidate
}
if(-not $ClientOnly){
    foreach($name in @('hlds.exe','swds.dll','filesystem_stdio.dll')){
        Queue-File "dist/rehlds/$name" "sandbox/cs-server/Half-Life/$name"
    }
    Queue-File 'build/regamedll/Release/mp.dll' 'sandbox/cs-server/Half-Life/cstrike/dlls/mp.dll'
    Queue-File 'build/native-x86/Release/goldcraft_amxx.dll' 'sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/modules/goldcraft_amxx.dll'
    Queue-File 'build/sypb/Release/sypb.dll' 'sandbox/cs-server/Half-Life/cstrike/addons/sypb/sypb.dll'
    Queue-File 'build/sypb/Release/sypb_amxx.dll' 'sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/modules/sypb_amxx.dll'
}
foreach($instance in $Clients){
    $game="sandbox/$instance/Half-Life"
    if($WithMetaHookRuntime){
        foreach($file in $runtime.files){
            $relative=$file.path.Replace('\','/')
            # Routine updates preserve personal renderer/map settings. They do
            # not reinitialize game/Steam profiles or rewrite native bindings.
            if($relative -in @('cstrike/goldcraft_renderer.cfg','cstrike/maps/cs_assault_entity.txt') -or $relative.StartsWith('platform/config/')){
                $preserved.Add("$game/$relative")
                continue
            }
            $from=if($relative -in @('cstrike/metahook/configs/plugins.lst','cstrike/metahook/configs/dllpaths.lst')){
                Merge-RuntimeList $instance $relative
            }else{"dist/metahook-runtime/$relative"}
            Queue-File $from "$game/$relative"
        }
        Queue-File 'dist/renderer/svencoop/metahook/plugins/Renderer.dll' "$game/cstrike/metahook/plugins/Renderer.dll"
    }
    # Each instance gets immutable candidate files. An older A catalog must
    # not decide which symbols the new B/common plugin set will provide.
    $generatedCatalog="build/metahook-runtime-update/$instance/gamedata"
    $catalogInput=if($runtime){Join-Path $runtimeStage 'cstrike/metahook/gamedata'}else{Join-Path $script:GoldCraftRoot "$game/cstrike/metahook/gamedata"}
    & python (Join-Path $PSScriptRoot 'Build-VisibleEntityGameData.py') --engine (Join-Path $script:GoldCraftRoot "$game/hw.dll") `
        --output (Join-Path $script:GoldCraftRoot "$generatedCatalog/goldcraft-visible")
    if($LASTEXITCODE){throw 'Visible-entity engine identity verification failed.'}
    & python (Join-Path $PSScriptRoot 'Build-ClientGameData.py') --client (Join-Path $script:GoldCraftRoot "$game/cstrike/cl_dlls/client.dll") `
        --existing-catalog $catalogInput --output (Join-Path $script:GoldCraftRoot "$generatedCatalog/goldcraft-cs")
    if($LASTEXITCODE){throw 'CS media-reader catalog identity verification failed.'}
    Queue-File 'dist/metahook/MetaHook.exe' "$game/MetaHook.exe"
    Queue-File 'build/native-x86/Release/GoldCraft.dll' "$game/cstrike/metahook/plugins/GoldCraft.dll"
    Queue-File 'dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll' "$game/cstrike/metahook/plugins/Renderer_AVX2.dll"
    Queue-File 'dist/vgui2extension/svencoop/metahook/plugins/VGUI2Extension.dll' "$game/cstrike/metahook/plugins/VGUI2Extension.dll"
    Queue-File 'dist/interpfix/svencoop/metahook/plugins/InterpFix.dll' "$game/cstrike/metahook/plugins/InterpFix.dll"
    Queue-File 'dist/bulletphysics/svencoop/metahook/plugins/BulletPhysics.dll' "$game/cstrike/metahook/plugins/BulletPhysics.dll"
    # MetaHook prefers the AVX2-suffixed plugin before the unsuffixed fallback.
    Queue-File 'dist/bulletphysics/svencoop/metahook/plugins/BulletPhysics.dll' "$game/cstrike/metahook/plugins/BulletPhysics_AVX2.dll"
    foreach($relative in @('metahook/dlls','metahook/gamedata/renderer','renderer/shader')){
        $directory=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "dist/renderer/svencoop/$relative")
        foreach($file in Get-ChildItem -LiteralPath $directory -Recurse -File | Where-Object {$_.Extension -in @('.dll','.json','.glsl')}){
            $suffix=$file.FullName.Substring($directory.Length+1).Replace('\','/')
            Queue-File "dist/renderer/svencoop/$relative/$suffix" "$game/cstrike/$relative/$suffix"
        }
    }
    $bulletGameData='dist/bulletphysics/svencoop/metahook/gamedata/bulletphysics'
    $bulletDirectory=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $bulletGameData)
    foreach($file in Get-ChildItem -LiteralPath $bulletDirectory -File -Filter '*.json'){
        Queue-File "$bulletGameData/$($file.Name)" "$game/cstrike/metahook/gamedata/bulletphysics/$($file.Name)"
    }
    $uiGameData='dist/vgui2extension/svencoop/metahook/gamedata/vgui2extension'
    $uiDirectory=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $uiGameData)
    foreach($file in Get-ChildItem -LiteralPath $uiDirectory -File -Filter '*.json'){
        Queue-File "$uiGameData/$($file.Name)" "$game/cstrike/metahook/gamedata/vgui2extension/$($file.Name)"
    }
    $interpGameData='dist/interpfix/svencoop/metahook/gamedata/interpfix'
    $interpDirectory=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $interpGameData)
    foreach($file in Get-ChildItem -LiteralPath $interpDirectory -File -Filter '*.json'){
        Queue-File "$interpGameData/$($file.Name)" "$game/cstrike/metahook/gamedata/interpfix/$($file.Name)"
    }
    $catalogs=@(
        @{source='dist/gamedata/goldcraft-precache';target='metahook/gamedata/goldcraft-precache'},
        @{source="$generatedCatalog/goldcraft-visible";target='metahook/gamedata/goldcraft-visible'},
        @{source="$generatedCatalog/goldcraft-cs";target='metahook/gamedata/goldcraft-cs'},
        @{source='dist/metahook/svencoop/metahook/gamedata';target='metahook/gamedata'})
    foreach($catalog in $catalogs){
        $relative=$catalog.source
        $directory=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $relative)
        $destination=$catalog.target
        foreach($file in Get-ChildItem -LiteralPath $directory -File -Filter '*.json'){
            Queue-File "$relative/$($file.Name)" "$game/cstrike/$destination/$($file.Name)"
        }
    }
}
foreach($entry in $files.Values){
    if((Get-FileHash -LiteralPath $entry.source -Algorithm SHA256).Hash -ne $entry.sha256){
        throw "Queued source changed during preparation: $($entry.path)"
    }
}
if($ValidateOnly){
    $changes=@($files.Values | Where-Object {
        -not(Test-Path -LiteralPath $_.destination) -or
        (Get-FileHash -LiteralPath $_.destination -Algorithm SHA256).Hash -ne $_.sha256
    })
    $plan=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/goldcraft-tests/native-deployment-plan.json')
    New-Item -ItemType Directory -Path (Split-Path $plan) -Force | Out-Null
    @{clients=$Clients;runtimeRelease=$(if($runtime){$runtime.release}else{$null});files=@($files.Values);changed=@($changes | ForEach-Object {$_.path});preserved=$preserved.ToArray()} |
        ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $plan -Encoding utf8
    Write-Output "Validated $($files.Count) files, $($changes.Count) changes; no runtime file written. Plan: $plan"
    return
}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$backup=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/deployment-backups/clang-precache-$stamp")
$records=[Collections.Generic.List[object]]::new()
foreach($entry in $files.Values){
    $previous=$null
    if(Test-Path -LiteralPath $entry.destination){$previous=(Get-FileHash -LiteralPath $entry.destination -Algorithm SHA256).Hash}
    if($previous -ne $entry.sha256){
        $saved=Assert-SandboxPath (Join-Path $backup $entry.path.Substring('sandbox/'.Length))
        New-Item -ItemType Directory -Path (Split-Path $saved),(Split-Path $entry.destination) -Force | Out-Null
        if($previous){Copy-Item -LiteralPath $entry.destination -Destination $saved}
        Copy-Item -LiteralPath $entry.source -Destination $entry.destination -Force
    }
    if((Get-FileHash -LiteralPath $entry.destination -Algorithm SHA256).Hash -ne $entry.sha256){throw 'Native deployment hash mismatch.'}
    $records.Add(@{path=$entry.path;sha256=$entry.sha256;previous=$previous;changed=($previous -ne $entry.sha256)})
}
foreach($name in @('rehlds-deployment.json','amxx-deployment.json','zombie-deployment.json')){
    if($ClientOnly){continue}
    $path=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/cs-server/$name")
    $data=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    foreach($entry in $data.files){
        $relative=if($entry.PSObject.Properties['name']){"sandbox/cs-server/Half-Life/$($entry.name)"}else{$entry.path.Replace('\','/')}
        $updated=$records | Where-Object {$_.path -eq $relative}
        if($updated){
            $entry.sha256=$updated.sha256
            if($entry.PSObject.Properties['bytes']){$entry.bytes=(Get-Item -LiteralPath (Join-Path $script:GoldCraftRoot $relative)).Length}
        }
    }
    $data | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding utf8
}
$report=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "analysis/goldcraft-tests/clang-precache-deployment-$stamp.json")
New-Item -ItemType Directory -Path (Split-Path $report) -Force | Out-Null
@{utc=$stamp;clients=$Clients;files=$records.ToArray();backup=$backup;runtimeRelease=$(if($runtime){$runtime.release}else{$null});preserved=$preserved.ToArray();note='C++20 clang-cl, x86, O3/ThinLTO/AVX2. Runtime validation follows separately.'} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding utf8
Write-Output "Deployed $($records.Count) verified native files. Evidence: $report"
