param()
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$ProgressPreference='SilentlyContinue'
$downloads=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/downloads')
$runtime=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/amxx-1.9.0.5303')
$metamod=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/metamod-1.3.0.149')
$reapi=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/reapi-5.29.0.358')
New-Item -ItemType Directory -Path $downloads,$runtime,$metamod,$reapi -Force | Out-Null
$packages=@(
    @{ name='metamod-bin-1.3.0.149.zip'; url='https://github.com/rehlds/Metamod-R/releases/download/1.3.0.149/metamod-bin-1.3.0.149.zip'; sha256='EDE7F59C4E0220AFE8C02AA348A130CCE527F87D36FFDB674E37A501CE57BE94'; target=$metamod },
    @{ name='amxmodx-1.9.0-git5303-base-windows.zip'; url='https://github.com/alliedmodders/amxmodx/releases/download/1.9.0.5303/amxmodx-1.9.0-git5303-base-windows.zip'; sha256='DD5C0F64B3974CE60E9A35D5BEC957E8E2DB95AE6FA12E45F24563373F550364'; target=$runtime },
    @{ name='amxmodx-1.9.0-git5303-cstrike-windows.zip'; url='https://github.com/alliedmodders/amxmodx/releases/download/1.9.0.5303/amxmodx-1.9.0-git5303-cstrike-windows.zip'; sha256='54C83A9C632C86FF4DCF90793B47950C7308AB1D96F57052DBCAA203BD2FEF35'; target=$runtime },
    @{ name='reapi-bin-5.29.0.358.zip'; url='https://github.com/rehlds/ReAPI/releases/download/5.29.0.358/reapi-bin-5.29.0.358.zip'; sha256='F33A7435540BEA8706DB3FA948E51A85B511F5B18C383B97402A204DC5419195'; target=$reapi }
)
$evidence=@()
foreach($package in $packages){
    $archive=Assert-WorkspacePath (Join-Path $downloads $package.name)
    if(-not(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $package.url -OutFile $archive}
    $digest=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if($package.sha256 -and $digest -ne $package.sha256){throw "Dependency hash mismatch: $($package.name)"}
    # Downloads are pinned official release archives; expansion stays in .tools.
    Expand-Archive -LiteralPath $archive -DestinationPath $package.target -Force
    $evidence+=@{name=$package.name;url=$package.url;sha256=$digest;releaseDigestVerified=(-not $package.name.StartsWith('metamod'));workspacePinVerified=[bool]$package.sha256}
}
$sourceIncludes=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'external/ReAPI/reapi/extra/amxmodx/scripting/include')
$matchingIncludes=@()
foreach($header in Get-ChildItem -LiteralPath $sourceIncludes -Filter '*.inc' -File){
    $packageHeader=Join-Path $reapi "addons/amxmodx/scripting/include/$($header.Name)"
    if((Get-FileHash -LiteralPath $header.FullName).Hash -ne (Get-FileHash -LiteralPath $packageHeader).Hash){throw "ReAPI source/package include mismatch: $($header.Name)"}
    $matchingIncludes+=$header.Name
}
@{amxxSource='b88b763a63bc7165f48e326c226eb05f9db0e54a';reapiSource='1c448d06e8c1cebaea061d6b81b94e85f6262649';packages=$evidence;matchingReapiIncludes=$matchingIncludes;rehldsApi='3.15';regamedllApi='5.30'} | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/amxx-dependencies.json')) -Encoding utf8
Write-Output "Pinned AMXX and Metamod packages prepared under .tools; no server configuration changed."
