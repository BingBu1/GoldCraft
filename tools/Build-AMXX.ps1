param([string[]]$Plugins=@('goldcraft'),[string[]]$Includes=@(),[switch]$Deploy,
      [ValidatePattern('^plugins(?:-[a-zA-Z0-9_]+)?\.ini$')][string]$PluginList='plugins.ini')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$compiler=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/amxx-1.9.0.5303/addons/amxmodx/scripting/amxxpc.exe')
if(-not(Test-Path -LiteralPath $compiler)){& (Join-Path $PSScriptRoot 'Prepare-AMXX.ps1')}
$out=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/amxx/plugins')
New-Item -ItemType Directory -Path $out -Force | Out-Null
$built=@()
$extraIncludes=@($Includes | ForEach-Object { '-i'+(Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $_)) })
foreach($plugin in $Plugins){
    if($plugin -notmatch '^[a-z0-9_]+$'){throw 'Invalid plugin filename'}
    $sourceMatches=@(Get-ChildItem -LiteralPath (Join-Path $script:GoldCraftRoot 'amxx') -Filter "$plugin.sma" -File -Recurse)
    if($sourceMatches.Count -ne 1){throw "Expected one classified source for $plugin; found $($sourceMatches.Count)"}
    $source=Assert-WorkspacePath $sourceMatches[0].FullName
    $relative=[IO.Path]::GetRelativePath((Join-Path $script:GoldCraftRoot 'amxx'),$source)
    $mod=$relative.Split([IO.Path]::DirectorySeparatorChar)[0]
    $modInclude=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "amxx/$mod/include")
    $localIncludes=@()
    if(Test-Path -LiteralPath $modInclude){$localIncludes+=('-i'+$modInclude)}
    & $compiler $source @localIncludes ("-i"+(Join-Path $script:GoldCraftRoot 'amxx/goldcraft/include')) ("-i"+(Join-Path $script:GoldCraftRoot '.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include')) ("-i"+(Join-Path (Split-Path $compiler) 'include')) @extraIncludes ("-o"+(Join-Path $out "$plugin.amxx"))
    if($LASTEXITCODE){throw "Pawn compilation failed: $plugin"}
    $artifact=Assert-WorkspacePath (Join-Path $out "$plugin.amxx")
    $built+=@{name=$plugin;source=[IO.Path]::GetRelativePath($script:GoldCraftRoot,$source).Replace('\','/');sourceSha256=(Get-FileHash -LiteralPath $source).Hash;artifact="build/amxx/plugins/$plugin.amxx";sha256=(Get-FileHash -LiteralPath $artifact).Hash}
}
$built | ConvertTo-Json -Depth 4 -AsArray | Set-Content -LiteralPath (Join-Path $out '../last-build.json') -Encoding utf8
if($Deploy){
    $runtime=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/Half-Life/cstrike/addons/amxmodx')
    $config=Assert-SandboxPath (Join-Path $runtime "configs/$PluginList")
    if(-not(Test-Path -LiteralPath (Join-Path $runtime 'plugins'))){throw 'Initialize AMXX before deploying Pawn plugins.'}
    $pendingLines=@()
    foreach($entry in $built){
        $target=Assert-SandboxPath (Join-Path $runtime "plugins/$($entry.name).amxx")
        Copy-Item -LiteralPath (Join-Path $script:GoldCraftRoot $entry.artifact) -Destination $target -Force
        if((Get-FileHash -LiteralPath $target).Hash -ne $entry.sha256){throw 'Pawn deployment hash mismatch'}
        $line="$($entry.name).amxx"
        $pattern='^\s*'+[Regex]::Escape($line)+'(?:\s|$)'
        $enabled=Get-ChildItem -LiteralPath (Join-Path $runtime 'configs') -Filter 'plugins*.ini' -File | Select-String -Pattern $pattern
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
