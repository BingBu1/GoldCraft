param([string[]]$Plugins=@(),[string[]]$Includes=@(),[switch]$Deploy,
      [ValidatePattern('^plugins(?:-[a-zA-Z0-9_]+)?\.ini$')][string]$PluginList='plugins.ini')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$Plugins=@($Plugins | ForEach-Object { $_.Split(',') } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if($Deploy -and -not $Plugins.Count){throw 'Deployment requires explicit -Plugins names; compile-all does not enable test plugins.'}
$out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/amxx/plugins')
New-Item -ItemType Directory -Path $out -Force | Out-Null
$buildLock=Assert-WorkspacePath (Join-Path $out '../builder.lock')
try{$guard=[IO.File]::Open($buildLock,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
catch{throw 'Another AMXX build/deployment is active. Wait for it to finish.'}
try{
$compiler=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/amxx-1.9.0.5303/addons/amxmodx/scripting/amxxpc.exe')
$reapi=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include/reapi.inc')
$archives=@('amxmodx-1.9.0-git5303-base-windows.zip','amxmodx-1.9.0-git5303-cstrike-windows.zip','reapi-bin-5.29.0.358.zip')
$missingArchives=@($archives | Where-Object { -not(Test-Path -LiteralPath (Join-Path $script:GoldCraftRoot ".tools/downloads/$_")) })
if(-not(Test-Path -LiteralPath $compiler) -or -not(Test-Path -LiteralPath $reapi) -or $missingArchives.Count){
    & (Join-Path $PSScriptRoot 'Prepare-AMXX.ps1')
}
& (Join-Path $PSScriptRoot 'Prepare-AMXXBuilder.ps1')
$node=(Get-Command node -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$arguments=@()
foreach($plugin in $Plugins){$arguments+=@('--plugin',$plugin)}
foreach($include in $Includes){$arguments+=@('--include',$include)}
& $node (Join-Path $PSScriptRoot 'Build-AMXX.cjs') @arguments
if($LASTEXITCODE){throw 'amxx-builder compilation failed; last successful bytecode was preserved.'}
$built=@(Get-Content -LiteralPath (Join-Path $out '../last-build.json') -Raw | ConvertFrom-Json)
if($Deploy){
    $runtime=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/Half-Life/cstrike/addons/amxmodx')
    $config=Assert-SandboxPath (Join-Path $runtime "configs/$PluginList")
    if(-not(Test-Path -LiteralPath (Join-Path $runtime 'plugins'))){throw 'Initialize AMXX before deploying Pawn plugins.'}
    # Matched AMXX meta_api.cpp only loads plugins.ini and plugins-*.ini here.
    # plugins.stock.ini is a backup, not an active registration.
    $activeLists=@(Get-ChildItem -LiteralPath (Join-Path $runtime 'configs') -Filter '*.ini' -File |
        Where-Object { $_.Name -eq 'plugins.ini' -or $_.Name -clike 'plugins-*.ini' })
    foreach($entry in $built){
        $disabledPattern='^\s*'+[Regex]::Escape("$($entry.name).amxx")+'[ \t]+disabled(?:[ \t;]|$)'
        if($activeLists | Select-String -Pattern $disabledPattern){throw "Plugin explicitly disabled in an active list: $($entry.name)"}
    }
    $pendingLines=@()
    foreach($entry in $built){
        $target=Assert-SandboxPath (Join-Path $runtime "plugins/$($entry.name).amxx")
        Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot $entry.artifact) -Destination $target -Force
        if((Get-FileHash -LiteralPath $target).Hash -ne $entry.sha256){throw 'Pawn deployment hash mismatch'}
        $line="$($entry.name).amxx"
        $pattern='^\s*'+[Regex]::Escape($line)+'(?:\s|$)'
        $enabled=$activeLists | Select-String -Pattern $pattern
        if(-not $enabled -and $line -notin $pendingLines){$pendingLines+=$line}
    }
    if($pendingLines.Count){
        # Plugin names are ASCII. Append without recoding existing Chinese
        # comments or rewriting an already registered plugin list.
        $existing=if(Test-Path -LiteralPath $config){[IO.File]::ReadAllBytes($config)}else{[byte[]]@()}
        $prefix=if($existing.Length -and $existing[-1] -ne 10){"`r`n"}else{''}
        $bytes=[Text.Encoding]::ASCII.GetBytes($prefix+($pendingLines -join "`r`n")+"`r`n")
        $stream=[IO.File]::Open($config,[IO.FileMode]::Append,[IO.FileAccess]::Write)
        try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()}
    }
    Write-Output 'Bytecode deployed; reload the map to load it. Source remains in amxx; sv_restart does not reload Pawn.'
}
}finally{$guard.Dispose()}
