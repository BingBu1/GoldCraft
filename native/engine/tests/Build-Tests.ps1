param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=(Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
. (Join-Path $script:GoldCraftRoot 'tools/ClangToolchain.ps1')
[Diagnostics.Process]::GetCurrentProcess().PriorityClass='BelowNormal'
$compiler=Initialize-GoldCraftCompiler
$arguments=Get-GoldCraftClangCMakeArguments $compiler
& cmake -S $PSScriptRoot -B $OutputDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" @arguments -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE){exit $LASTEXITCODE}
& cmake --build $OutputDirectory --parallel 1
if($LASTEXITCODE){exit $LASTEXITCODE}
& ctest --test-dir $OutputDirectory --output-on-failure
exit $LASTEXITCODE
