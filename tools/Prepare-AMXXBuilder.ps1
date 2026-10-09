param()
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')

$node=(Get-Command node -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$npm=(Get-Command npm.cmd -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
$nodeVersion=& $node --version
if($LASTEXITCODE -or [version]$nodeVersion.TrimStart('v') -lt [version]'18.3.0'){throw 'The AMXX entry requires Node.js 18.3 or newer.'}

$lock=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json
$entry=$lock.sources.AMXXBuilder
$source=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $entry.path)
if(-not(Test-Path -LiteralPath $source)){
    & git clone --filter=blob:none --no-checkout $entry.url $source
    if($LASTEXITCODE){throw 'Cannot fetch amxx-builder.'}
    & git -C $source checkout --detach $entry.commit
    if($LASTEXITCODE){throw 'Cannot check out pinned amxx-builder revision.'}
}
$head=& git -C $source rev-parse HEAD
if($LASTEXITCODE -or $head -ne $entry.commit){throw 'amxx-builder revision differs from sources.lock.json; local work was preserved.'}
& git -C $source diff --quiet HEAD --
if($LASTEXITCODE){throw 'amxx-builder has tracked edits; restore or review them before using the pinned builder.'}

$package=Get-Content -LiteralPath (Join-Path $source 'package.json') -Raw | ConvertFrom-Json
if($package.version -ne $entry.version){throw 'amxx-builder package version mismatch.'}
$packageLock=Assert-WorkspacePath (Join-Path $source 'package-lock.json')
$modules=Assert-WorkspacePath (Join-Path $source 'node_modules')
$installedLock=Assert-WorkspacePath (Join-Path $modules '.package-lock.json')
$cache=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/npm-cache')
$stamp=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/amxx-builder-npm.json')
$lockHash=(Get-FileHash -LiteralPath $packageLock).Hash
$ready=$false
if((Test-Path -LiteralPath $stamp) -and (Test-Path -LiteralPath $installedLock)){
    $previous=Get-Content -LiteralPath $stamp -Raw | ConvertFrom-Json
    $ready=$previous.commit -eq $entry.commit -and $previous.packageLockSha256 -eq $lockHash -and
        $previous.nodeVersion -eq $nodeVersion -and
        $previous.installedLockSha256 -eq (Get-FileHash -LiteralPath $installedLock).Hash
}
if(-not $ready){
    # npm ci replaces this one dependency tree; never traverse a linked tree.
    if(Test-Path -LiteralPath $modules){
        Get-ChildItem -LiteralPath $modules -Force -Recurse | ForEach-Object { Assert-NoReparsePath $_.FullName }
    }
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    & $npm ci --prefix $source --cache $cache --omit=dev --ignore-scripts --no-audit --no-fund --update-notifier=false
    if($LASTEXITCODE){throw 'amxx-builder dependency installation failed.'}
    @{commit=$entry.commit;packageLockSha256=$lockHash;nodeVersion=$nodeVersion;
      installedLockSha256=(Get-FileHash -LiteralPath $installedLock).Hash} |
        ConvertTo-Json | Set-Content -LiteralPath $stamp -Encoding utf8
}
Write-Output "amxx-builder $($entry.version) ready (workspace-local dependencies)."

$sypb=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $lock.sources.SyPB.path)
$sypbHead=& git -C $sypb rev-parse HEAD
if($LASTEXITCODE -or $sypbHead -ne $lock.sources.SyPB.commit){throw 'Prepare the pinned SyPB sources before building AMXX.'}
& git -C $sypb diff --quiet HEAD -- 'Project SyPB/AMXX/*.inc'
if($LASTEXITCODE){throw 'SyPB API includes differ from the pinned SDK.'}
