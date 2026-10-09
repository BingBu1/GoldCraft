param()
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$lock=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json
$release=$lock.metahookRelease
$archive=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot ".tools/downloads/MetaHookSv-$($release.tag)-windows-x86.7z")
New-Item -ItemType Directory -Path (Split-Path $archive) -Force | Out-Null
if(-not(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $release.url -OutFile $archive}
if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $release.sha256){throw 'MetaHook release checksum mismatch'}
$unpack=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "dist/upstream-metahook-$($release.tag)")
New-Item -ItemType Directory -Path $unpack -Force | Out-Null
Push-Location $unpack
try {
    & (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe') -E tar xf $archive
    if($LASTEXITCODE){throw 'MetaHook release extraction failed'}
} finally {Pop-Location}
$upstream=Join-Path $unpack 'install/output'
$stage=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/metahook-runtime')
New-Item -ItemType Directory -Path $stage -Force | Out-Null

function Copy-StageFile([string]$From,[string]$Relative) {
    $to=Assert-WorkspacePath (Join-Path $stage $Relative)
    Assert-NoReparsePath $From
    New-Item -ItemType Directory -Path (Split-Path $to) -Force | Out-Null
    Copy-Item -LiteralPath $From -Destination $to -Force
}
function Copy-StageTree([string]$From,[string]$Relative) {
    Assert-NoReparsePath $From
    foreach($file in Get-ChildItem -LiteralPath $From -File -Recurse){
        if($file.Extension -eq '.pdb'){continue}
        Copy-StageFile $file.FullName (Join-Path $Relative $file.FullName.Substring($From.Length+1))
    }
}

# Read normal CS only as an installation/configuration reference. Stage all
# writes in dist; the stopped-instance deployer is the only runtime writer.
$normal=Join-Path $script:GoldCraftOriginal 'cstrike'
$pluginList=Join-Path $normal 'metahook/configs/plugins.lst'
Assert-NoReparsePath $pluginList
$plugins=[Collections.Generic.List[string]]::new()
foreach($line in Get-Content -LiteralPath $pluginList){
    $name=($line -split '//',2)[0].Trim()
    if(-not $name -or $name.StartsWith('#') -or $name -eq 'GoldCraft.dll'){continue}
    if($name -notmatch '^[A-Za-z0-9_.-]+\.dll$'){throw "Unsupported plugin-list entry: $name"}
    $plugins.Add($name)
    if($name -match '^Renderer(?:_AVX2)?\.dll$'){$plugins.Add('GoldCraft.dll')}
}
if(-not $plugins.Contains('GoldCraft.dll')){throw 'Normal CS must have Renderer enabled before preparing GoldCraft scene integration'}
# New upstream defaults supplement the normal installation's plugin set.
# Append without disturbing Renderer/GoldCraft's existing wrapper order.
$defaults=Join-Path (Get-MetaHookSourceRoot) 'assets/svencoop/metahook/configs/plugins_goldsrc.lst'
if((Get-Content -LiteralPath $defaults) -contains 'InterpFix.dll' -and -not $plugins.Contains('InterpFix.dll')){
    $plugins.Add('InterpFix.dll')
}
foreach($name in $plugins){
    if($name -in @('GoldCraft.dll','Renderer.dll','Renderer_AVX2.dll')){continue}
    $dll=Join-Path $upstream "svencoop/metahook/plugins/$name"
    if($name -eq 'VGUI2Extension.dll'){
        $dll=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/vgui2extension/svencoop/metahook/plugins/VGUI2Extension.dll')
    }
    elseif($name -eq 'InterpFix.dll'){
        $dll=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/interpfix/svencoop/metahook/plugins/InterpFix.dll')
    }
    elseif($name -match '^BulletPhysics(?:_AVX2)?\.dll$'){
        $dll=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/bulletphysics/svencoop/metahook/plugins/BulletPhysics.dll')
    }
    if(-not(Test-Path -LiteralPath $dll)){throw "The pinned release does not contain normal plugin $name"}
    Copy-StageFile $dll "cstrike/metahook/plugins/$name"
}
foreach($directory in @('captionmod','bulletphysics','studioevents','vgui2ext','metahook/gamedata')){
    Copy-StageTree (Join-Path $upstream "svencoop/$directory") "cstrike/$directory"
}
Copy-StageTree (Join-Path $script:GoldCraftRoot 'dist/vgui2extension/svencoop/metahook/gamedata/vgui2extension') 'cstrike/metahook/gamedata/vgui2extension'
Copy-StageTree (Join-Path $script:GoldCraftRoot 'dist/interpfix/svencoop/metahook/gamedata/interpfix') 'cstrike/metahook/gamedata/interpfix'
Copy-StageTree (Join-Path $script:GoldCraftRoot 'dist/renderer/svencoop/metahook/gamedata/renderer') 'cstrike/metahook/gamedata/renderer'
Copy-StageTree (Join-Path $script:GoldCraftRoot 'dist/bulletphysics/svencoop/metahook/gamedata/bulletphysics') 'cstrike/metahook/gamedata/bulletphysics'
Copy-StageTree (Join-Path $upstream 'platform') 'platform'
foreach($name in @('SDL2.dll','SDL3.dll')){Copy-StageFile (Join-Path $upstream $name) $name}
foreach($file in Get-ChildItem -LiteralPath (Join-Path $upstream 'svencoop/metahook/dlls') -File -Recurse){
    # No Steam API bridge is needed by the normal plugin set. Keep the normal
    # game's ownership/authentication path and load only the requested plugins.
    if($file.Name -eq 'SteamAPIBridge.dll' -or $file.Extension -eq '.pdb'){continue}
    $relative=$file.FullName.Substring((Join-Path $upstream 'svencoop/metahook/dlls').Length+1)
    Copy-StageFile $file.FullName (Join-Path 'cstrike/metahook/dlls' $relative)
}
$configs=Assert-WorkspacePath (Join-Path $stage 'cstrike/metahook/configs')
New-Item -ItemType Directory -Path $configs -Force | Out-Null
$plugins | Set-Content -LiteralPath (Join-Path $configs 'plugins.lst') -Encoding ascii
$dllpaths=Join-Path $normal 'metahook/configs/dllpaths.lst'
if(Test-Path -LiteralPath $dllpaths){Copy-StageFile $dllpaths 'cstrike/metahook/configs/dllpaths.lst'}
else{'FreeImage' | Set-Content -LiteralPath (Join-Path $configs 'dllpaths.lst') -Encoding ascii}

$lighting=[ordered]@{brightness='2.0';gamma='3';r_hdr='0';r_ssao_intensity='3.0';r_deferred_lightmap_pow='1';r_deferred_lightmap_scale='1'}
foreach($line in Get-Content -LiteralPath (Join-Path $normal 'config.cfg')){
    if($line -match '^\s*(\w+)\s+"?([-+]?\d+(?:\.\d+)?)"?\s*$' -and $lighting.Contains($Matches[1])){
        $lighting[$Matches[1]]=$Matches[2]
    }
}
$cfg=Assert-WorkspacePath (Join-Path $stage 'cstrike/goldcraft_renderer.cfg')
@('// Lighting values copied from normal CS; no key bindings are changed.',
  'r_shadow 1','r_dynamic 1','r_deferred_lighting 1','r_gamma_blend 0',
  'r_drawlowerbody 0','r_drawlowerbodyattachments 0',
    'r_studio_diagnostics 0','r_renderer_profile 0','r_shadow_caster_cull 1') |
    Set-Content -LiteralPath $cfg -Encoding ascii
foreach($key in $lighting.Keys){"$key $($lighting[$key])" | Add-Content -LiteralPath $cfg -Encoding ascii}
$normalMap=Join-Path $normal 'maps/cs_assault_entity.txt'
if(Test-Path -LiteralPath $normalMap){
    $destination=Assert-WorkspacePath (Join-Path $stage 'cstrike/maps/cs_assault_entity.txt')
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    $defaults=Get-Content -LiteralPath (Join-Path $upstream 'svencoop/renderer/default_entity.txt') -Raw
    $defaults+[Environment]::NewLine+(Get-Content -LiteralPath $normalMap -Raw) |
        Set-Content -LiteralPath $destination -Encoding utf8NoBOM
}
$files=@(Get-ChildItem -LiteralPath $stage -File -Recurse | Where-Object Name -ne 'manifest.json' | ForEach-Object {
    @{path=$_.FullName.Substring($stage.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}
})
@{release=$release.tag;archiveSha256=$release.sha256;plugins=@($plugins);lighting=$lighting;files=$files} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
Write-Output "Prepared $($plugins.Count) plugins and $($files.Count) files in $stage; no running instance changed."
