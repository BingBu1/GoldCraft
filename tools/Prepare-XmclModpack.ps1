param([ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$env:GOLDCRAFT_MOD_LOADER=$Loader
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this preparation once with PowerShell 7.'}
if($Loader -eq 'neoforge'){
    $mcPin=(Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json).minecraft
    $installed=Join-Path $script:GoldCraftRoot 'build/neoforge-production/installation.json'
    if((Test-Path -LiteralPath $installed) -and
        (Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json).neoForge -eq $mcPin.neoforge){
        & python (Join-Path $PSScriptRoot 'Prepare-NeoForgeRuntime.py') --verify
    }else{& python (Join-Path $PSScriptRoot 'Prepare-NeoForgeRuntime.py')}
    if($LASTEXITCODE){throw 'Production NeoForge preparation failed; the Mod source was not changed.'}
}
& python (Join-Path $PSScriptRoot 'Modpack.py') init
if($LASTEXITCODE){throw 'The XMCL source instance could not be prepared.'}
$packFolder=if($Loader -eq 'neoforge'){'modpack-neoforge'}else{'modpack'}
$mcPin=(Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json).minecraft
$profile=if($Loader -eq 'neoforge'){"GoldCraft-$($mcPin.version)-NeoForge"}else{'GoldCraft-1.21'}
$pack=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$packFolder")
if($Loader -eq 'neoforge'){
    $source=Assert-SandboxPath (Join-Path $pack $profile)
    $runtime=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot ".tools/neoforge-runtime/$($mcPin.neoforge)")
    # XMCL sees the actual installed production version without a second copy
    # of its libraries and without pointing at a personal Minecraft installation.
    foreach($name in @('libraries','versions')){
        $target=Assert-WorkspacePath (Join-Path $runtime $name)
        if(-not(Test-Path -LiteralPath $target -PathType Container)){throw "Missing prepared runtime directory: $target"}
        $link=Join-Path $source $name
        if(Test-Path -LiteralPath $link){
            $item=Get-Item -LiteralPath $link -Force
            if(-not($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
                [IO.Path]::GetFullPath([string]$item.Target) -ne $target){
                throw "Existing XMCL $name path differs from the prepared runtime; left unchanged."
            }
        }else{
            $link=Assert-SandboxPath $link
            New-Item -ItemType Junction -Path $link -Target $target | Out-Null
        }
    }
    $instancePath=Assert-SandboxPath (Join-Path $source 'instance.json')
    $instanceConfig=Get-Content -LiteralPath $instancePath -Raw | ConvertFrom-Json
    $manifest=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'build/neoforge-production/runClient.json') -Raw | ConvertFrom-Json
    $versionIndex=[Array]::IndexOf($manifest.args,'--version')
    if($versionIndex -lt 0){throw 'Prepared client manifest is missing its installed version identity'}
    $instanceConfig.version=$manifest.args[$versionIndex+1]
    $instanceConfig | ConvertTo-Json -Depth 40 | Set-Content -LiteralPath $instancePath -Encoding utf8NoBOM
}
$shell=Join-Path $PSHOME 'pwsh.exe'
$synchronizer=Assert-WorkspacePath (Join-Path $PSScriptRoot 'Sync-Modpack.ps1')
$shortcut=Assert-SandboxPath (Join-Path $pack 'Sync-and-Start.cmd')
@"
@echo off
"$shell" -NoLogo -NoProfile -File "$synchronizer" -Restart -Start -Loader $Loader
if errorlevel 1 pause
"@ | Set-Content -LiteralPath $shortcut -Encoding ascii
$register=Assert-WorkspacePath (Join-Path $PSScriptRoot 'Register-XmclModpack.ps1')
$importShortcut=Assert-SandboxPath (Join-Path $pack 'Add-to-XMCL.cmd')
@"
@echo off
"$shell" -NoLogo -NoProfile -File "$register" -Loader $Loader
pause
"@ | Set-Content -LiteralPath $importShortcut -Encoding ascii
Write-Output "XMCL external instance: $(Join-Path $pack $profile)"
Write-Output "One-time registration after exiting XMCL: $importShortcut"
Write-Output "Sync and start the complete local cluster: $shortcut"
