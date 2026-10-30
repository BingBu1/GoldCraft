param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$InventorySource='native/csol/client/weapon_inventory.cpp',
    [string]$Python='python',
    [switch]$ExpectFailure
)
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
$arguments=@('-B','-X','utf8',"$PSScriptRoot/Test-CSOLWeaponMenu.py",'--clang',$clang,'--output',$OutputDirectory,'--inventory',$InventorySource)
if($ExpectFailure){$arguments+='--expect-failure'}
& $Python @arguments
if($LASTEXITCODE -ne 0){throw 'Weapon menu checks failed'}
