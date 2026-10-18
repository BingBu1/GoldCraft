$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
. "$root/tools/SandboxPaths.ps1"
. "$root/tools/ClangToolchain.ps1"
$compiler = Initialize-GoldCraftCompiler
$output = "$root/build/floating-damage-20261018"
Push-Location $output
try {
    & "$($compiler.Root)/bin/clang-cl.exe" --target=i686-pc-windows-msvc /nologo /std:c++20 /O2 /EHsc /MD /DNDEBUG /DHAVE_STDINT_H /DPAWN_CELL_SIZE=32 `
        "/I$root/external/amxmodx/amxmodx" "$PSScriptRoot/pawn_runner.cpp" "$root/external/amxmodx/amxmodx/amx.cpp" `
        /Fepawn_runner.exe /link /MACHINE:X86
    if ($LASTEXITCODE) { throw 'Offline Pawn interpreter build failed' }
} finally { Pop-Location }
