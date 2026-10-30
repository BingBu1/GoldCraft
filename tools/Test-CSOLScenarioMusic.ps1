param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python='python',
    [switch]$CountdownOnly,
    [switch]$ClientOnly
)
$ErrorActionPreference='Stop'
if($CountdownOnly -and $ClientOnly){throw 'Choose either CountdownOnly or ClientOnly'}
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
$arguments=@('-B','-X','utf8',"$PSScriptRoot/Test-CSOLScenarioMusic.py",'--clang',$clang,'--output',$OutputDirectory)
if($CountdownOnly){$arguments+='--countdown-only'}
if($ClientOnly){$arguments+='--client-only'}
& $Python @arguments
if($LASTEXITCODE -ne 0){throw 'Scenario music checks failed'}
