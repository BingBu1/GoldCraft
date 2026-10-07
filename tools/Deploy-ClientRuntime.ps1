param([switch]$ValidateOnly, [ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$stage=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/metahook-runtime')
$manifest=Get-Content -LiteralPath (Join-Path $stage 'manifest.json') -Raw | ConvertFrom-Json
foreach($file in $manifest.files){
    $source=Assert-WorkspacePath (Join-Path $stage $file.path)
    if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $file.sha256){throw "Runtime staging changed: $($file.path)"}
}
$built=@('dist/metahook/MetaHook.exe','dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll',
         'build/native-x86/Release/GoldCraft.dll','build/regamedll/Release/mp.dll')
$artifacts=@($built | ForEach-Object {
    $file=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $_)
    @{path=$_;sha256=(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash}
})
if($ValidateOnly){Write-Output 'Client runtime hashes verified; deployment requires stopped sandbox clients.';return}
foreach($instance in @('cs-client-a','cs-client-b')){
    $game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$instance/Half-Life")
    $running=Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -eq (Join-Path $game 'MetaHook.exe')}
    if($running){throw "Stop $instance before runtime deployment"}
}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')
$backup=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/deployment-backups/metahook-$stamp")
$relativeDirectories=@('cstrike/metahook','cstrike/renderer','cstrike/captionmod','cstrike/bulletphysics',
                       'cstrike/studioevents','cstrike/vgui2ext','platform')
$relativeFiles=@('MetaHook.exe','SDL2.dll','SDL3.dll','cstrike/dlls/mp.dll','cstrike/maps/cs_assault_entity.txt',
                 'cstrike/config.cfg','cstrike/autoexec.cfg','cstrike/server.cfg','cstrike/listenserver.cfg',
                 'cstrike/goldcraft_test.cfg','cstrike/goldcraft_renderer.cfg')
foreach($instance in @('cs-client-a','cs-client-b')){
    $game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$instance/Half-Life")
    foreach($relative in @($relativeDirectories)+@($relativeFiles)){
        $source=Assert-SandboxPath (Join-Path $game $relative)
        if(-not(Test-Path -LiteralPath $source)){continue}
        $to=Assert-SandboxPath (Join-Path $backup "$instance/$relative")
        New-Item -ItemType Directory -Path (Split-Path $to) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $to -Recurse
    }
}
foreach($instance in @('cs-client-a','cs-client-b')){
    & (Join-Path $PSScriptRoot 'Initialize-SandboxInstance.ps1') -Instance $instance -Renderer -Loader $Loader
}
@{release=$manifest.release;plugins=$manifest.plugins;artifacts=$artifacts;loader=$Loader;backup=$backup;utc=$stamp} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'deployment.json') -Encoding utf8
Write-Output "Updated sandbox clients; previous runtime files remain at $backup"
