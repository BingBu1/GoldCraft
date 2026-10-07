param([ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$env:GOLDCRAFT_MOD_LOADER=$Loader
if($PSVersionTable.PSVersion.Major -lt 7){throw 'Run this preparation once with PowerShell 7.'}
& python (Join-Path $PSScriptRoot 'Modpack.py') init
if($LASTEXITCODE){throw 'The XMCL source instance could not be prepared.'}
$packFolder=if($Loader -eq 'neoforge'){'modpack-neoforge'}else{'modpack'}
$profile=if($Loader -eq 'neoforge'){'GoldCraft-1.21-NeoForge'}else{'GoldCraft-1.21'}
$pack=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$packFolder")
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
