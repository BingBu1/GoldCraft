param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python='python'
)
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
& $Python -B -X utf8 "$PSScriptRoot/Test-CSOLScenarioActiveSkill.py" --clang $clang --output $OutputDirectory
if($LASTEXITCODE -ne 0){throw 'Scenario active skill checks failed'}
