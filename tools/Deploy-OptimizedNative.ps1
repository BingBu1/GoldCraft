param([ValidateSet('cs-client-a','cs-client-b')][string[]]$Clients=@('cs-client-b'), [switch]$ClientOnly)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$instances=@($Clients | Select-Object -Unique)
if(-not $ClientOnly){$instances=@('cs-server')+$instances}
$running=Get-CimInstance Win32_Process | Where-Object {
    $_.Name -in @('MetaHook.exe','hlds.exe') -and $_.ExecutablePath
}
foreach($process in $running){
    foreach($instance in $instances){
        $root=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$instance/Half-Life")
        if($process.ExecutablePath.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){
            throw "Stop the recorded $instance process before native deployment."
        }
    }
}
$files=[Collections.Generic.List[object]]::new()
function Queue-File([string]$Source,[string]$Destination){
    $from=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $Source)
    $to=Assert-SandboxPath (Join-Path $script:GoldCraftRoot $Destination)
    $hash=(Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash
    $files.Add(@{source=$from;destination=$to;path=$Destination;sha256=$hash})
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
    & python (Join-Path $PSScriptRoot 'Build-VisibleEntityGameData.py') --engine (Join-Path $script:GoldCraftRoot "$game/hw.dll")
    if($LASTEXITCODE){throw 'Visible-entity engine identity verification failed.'}
    & python (Join-Path $PSScriptRoot 'Build-ClientGameData.py') --client (Join-Path $script:GoldCraftRoot "$game/cstrike/cl_dlls/client.dll") --existing-catalog (Join-Path $script:GoldCraftRoot "$game/cstrike/metahook/gamedata")
    if($LASTEXITCODE){throw 'CS media-reader catalog identity verification failed.'}
    Queue-File 'dist/metahook/MetaHook.exe' "$game/MetaHook.exe"
    Queue-File 'build/native-x86/Release/GoldCraft.dll' "$game/cstrike/metahook/plugins/GoldCraft.dll"
    Queue-File 'dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll' "$game/cstrike/metahook/plugins/Renderer_AVX2.dll"
    Queue-File 'dist/vgui2extension/svencoop/metahook/plugins/VGUI2Extension.dll' "$game/cstrike/metahook/plugins/VGUI2Extension.dll"
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
    foreach($relative in @('dist/gamedata/goldcraft-precache','dist/gamedata/goldcraft-visible','dist/gamedata/goldcraft-cs','dist/metahook/svencoop/metahook/gamedata')){
        $directory=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $relative)
        $destination=if($relative -like 'dist/gamedata/*'){'metahook/gamedata/'+(Split-Path $relative -Leaf)}else{'metahook/gamedata'}
        foreach($file in Get-ChildItem -LiteralPath $directory -File -Filter '*.json'){
            Queue-File "$relative/$($file.Name)" "$game/cstrike/$destination/$($file.Name)"
        }
    }
}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$backup=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/deployment-backups/clang-precache-$stamp")
$records=[Collections.Generic.List[object]]::new()
foreach($entry in $files){
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
@{utc=$stamp;clients=$Clients;files=$records.ToArray();backup=$backup;note='C++20 clang-cl, x86, O3/ThinLTO/AVX2. Runtime validation follows separately.'} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding utf8
Write-Output "Deployed $($records.Count) verified native files. Evidence: $report"
