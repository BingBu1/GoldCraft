param()
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$lock=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json
foreach($name in @('MetaHookSv','ReGameDLL_CS','ReHLDS','AMXModX','ReAPI','SyPB','AMXXBuilder','EpicFightMaid','YsmGeoCompat')){
    $entry=$lock.sources.$name
    $path=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $entry.path)
    if(-not(Test-Path -LiteralPath $path)){
        & git clone --filter=blob:none --no-checkout $entry.url $path
        if($LASTEXITCODE){throw "Clone failed: $name"}
        & git -C $path fetch origin $entry.commit
        if($LASTEXITCODE){throw "Pinned revision unavailable: $name"}
        & git -C $path checkout --detach $entry.commit
        if($LASTEXITCODE){throw "Checkout failed: $name"}
    } else {
        $head=& git -C $path rev-parse HEAD
        if($LASTEXITCODE -or $head -ne $entry.commit){throw "$name has a different revision; preserve local work and prepare the pinned source separately."}
    }
}
$meta=Get-MetaHookSourceRoot
& git -C $meta submodule update --init --recursive --jobs 4 MetaHook Plugins/Renderer Plugins/VGUI2Extension Plugins/BulletPhysics `
    PluginLibs/UtilThreadTask thirdparty/glew_fork thirdparty/ScopeExit thirdparty/FreeImage_clone thirdparty/tinyobjloader thirdparty/bullet3
if($LASTEXITCODE){throw 'MetaHook component/dependency preparation failed'}
foreach($name in @('ReGameDLL_CS','ReHLDS','AMXModX','ReAPI')){
    $path=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $lock.sources.$name.path)
    & git -C $path submodule update --init --recursive --jobs 4
    if($LASTEXITCODE){throw "Dependency preparation failed: $name"}
}
foreach($name in @('MetaHook','Renderer','BulletPhysics','VGUI2Extension','FreeImage','ReGameDLL_CS','ReHLDS','SyPB','EpicFightMaid','YsmGeoCompat')){
    $entry=$lock.sources.$name
    $path=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $entry.path)
    $patch=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $entry.patch)
    if((& git -C $path rev-parse HEAD) -ne $entry.commit){throw "Component revision mismatch: $name"}
    & git -C $path apply --reverse --check $patch 2>$null
    if($LASTEXITCODE -eq 0){Write-Output "$name adaptation already applied";continue}
    & git -C $path apply --check $patch
    if($LASTEXITCODE){throw "$name contains conflicting local changes; no reset was performed."}
    & git -C $path apply $patch
    if($LASTEXITCODE){throw "$name patch failed"}
}
Write-Output 'Pinned sources and GoldCraft adaptations are ready. Game installations were not accessed.'
