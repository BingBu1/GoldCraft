param([string]$Output='build/csol-runtime-20261021/client')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/SandboxPaths.ps1"
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$destination=[IO.Path]::GetFullPath((Join-Path $script:GoldCraftRoot $Output))
if(-not $destination.StartsWith($script:GoldCraftRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Build output must stay inside workspace' }
$arguments=Get-GoldCraftClangCMakeArguments $compiler
& cmake -S "$script:GoldCraftRoot/native/csol" -B $destination -G Ninja "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" -DCMAKE_BUILD_TYPE=Release @arguments
if($LASTEXITCODE){throw 'CSOLSupport configure failed'}
& cmake --build $destination --parallel 4
if($LASTEXITCODE){throw 'CSOLSupport build failed'}
