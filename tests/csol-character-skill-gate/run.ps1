param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$OldRef
)
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $script:GoldCraftRoot 'tools/ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
Set-Location -LiteralPath $script:GoldCraftRoot
python (Join-Path $PSScriptRoot 'run.py') --compiler (Join-Path $compiler.Root 'bin/clang-cl.exe') `
    --output $OutputDirectory --old-ref $OldRef
if($LASTEXITCODE -ne 0){throw 'Character skill gate validation failed'}
