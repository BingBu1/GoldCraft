param()
. (Join-Path $PSScriptRoot 'Common.ps1')
$count = 0
function Check([bool]$Value, [string]$Name) { if (!$Value) { throw $Name }; $script:count++ }
$required = @('VGUI2Extension.dll','CaptionMod.dll','Renderer_AVX2.dll','GoldCraft.dll','BulletPhysics.dll','StudioEvents.dll','PrecacheManager.dll','HeapPatch.dll','ResourceReplacer.dll','InterpFix.dll')
foreach ($source in @('', "// Keep comment`r`nThirdParty.dll`r`n", "GoldCraft.dll`nRenderer.dll`nCaptionMod.dll`nMetaAudio.dll`nGoldCraft_AVX2.dll`n", ($required -join "`r`n"))) {
    $merged = Merge-Plugins $source $required
    $entries = @($merged -split '\r?\n' | ForEach-Object { Get-ActiveEntry $_ } | Where-Object { $_ })
    $owned = @($entries | Where-Object { $_ -in $required })
    Check (($owned -join ',') -ceq ($required -join ',')) 'Required order/uniqueness'
    Check ($entries[[Array]::IndexOf($entries, 'Renderer_AVX2.dll') + 1] -eq 'GoldCraft.dll') 'Renderer adjacency'
    Check ((Merge-Plugins $merged $required) -ceq $merged) 'Merge must be idempotent'
    if ($source.Contains('MetaAudio.dll')) { Check ([Array]::IndexOf($entries, 'MetaAudio.dll') -lt [Array]::IndexOf($entries, 'CaptionMod.dll')) 'MetaAudio order' }
    if ($source.Contains('ThirdParty.dll')) { Check ($merged.Contains("// Keep comment`r`nThirdParty.dll")) 'Unrelated plugin/comment preservation' }
}
$interleaved = "Before.dll`nVGUI2Extension.dll`nMiddle.dll`nRenderer.dll`nGoldCraft.dll`nAfter.dll`n"
$merged = Merge-Plugins $interleaved $required
Check ($merged.IndexOf('Before.dll') -lt $merged.IndexOf('Middle.dll') -and $merged.IndexOf('Middle.dll') -lt $merged.IndexOf('After.dll')) 'Unrelated relative order'
Check ((Merge-DllPaths "Custom`r`nfreeimage`r`n" @('FreeImage')) -eq "Custom`r`nfreeimage`r`n") 'Case-insensitive DLL path merge'
$candidates = @(Get-ManagedPluginCandidates $required)
foreach ($name in @('GoldCraft_AVX2.dll','GoldCraft_AVX.dll','GoldCraft_SSE2.dll','GoldCraft_SSE.dll','GoldCraft.dll','Renderer.dll','Renderer_AVX2_SSE.dll','Renderer_AVX2_AVX2.dll')) {
    Check ($candidates -contains ('cstrike/metahook/plugins/' + $name)) ('Actual loader candidate covered: ' + $name)
}
$aliases = Merge-Plugins "GoldCraft_SSE.dll`nGoldCraft_AVX.dll`nRenderer_SSE2.dll`n" $required
Check (!$aliases.Contains('GoldCraft_SSE.dll') -and !$aliases.Contains('GoldCraft_AVX.dll') -and !$aliases.Contains('Renderer_SSE2.dll')) 'All old optimized list entries normalized'
foreach ($bad in @('../escape','x/../../escape','/root','C:/root','x\bad','x//bad')) {
    $rejected = $false
    try { [void](Join-CheckedPath $PSScriptRoot $bad) } catch { $rejected = $true }
    Check $rejected ('Unsafe path rejected: ' + $bad)
}
$valid = Join-CheckedPath $PSScriptRoot 'directory with spaces/file.txt'
Check ($valid.StartsWith($PSScriptRoot)) 'Literal path with spaces'
foreach ($file in @('Common.ps1','Install-GoldCraft.ps1','Test-Plan.ps1')) {
    $tokens=$null; $errors=$null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $file),[ref]$tokens,[ref]$errors)
    Check (!$errors) ('PowerShell syntax: ' + $file)
}
[ordered]@{checks=$count; passed=$count; gameFilesWritten=$false; officialInstallExecuted=$false} | ConvertTo-Json
