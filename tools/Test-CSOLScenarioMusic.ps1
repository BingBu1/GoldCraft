param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python='python',
    [switch]$CountdownOnly
)
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
$arguments=@('-B','-X','utf8',"$PSScriptRoot/Test-CSOLScenarioMusic.py",'--clang',$clang,'--output',$OutputDirectory)
if($CountdownOnly){$arguments+='--countdown-only'}
& $Python @arguments
if($LASTEXITCODE -ne 0){throw 'Scenario music checks failed'}
