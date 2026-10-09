param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangCMakeArguments $compiler
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe')
$source=Get-MetaHookSourceRoot
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/interpfix-clang-$Configuration")
$prefix=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/interpfix')
& $cmake -S "$source/Plugins/InterpFix" -B $build -G Ninja @clangArgs `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" "-DCMAKE_INSTALL_PREFIX=$prefix" `
    '-DINTERPFIX_BUILD_TESTS=ON' "-DMETAHOOK_SOURCE_PATH=$source/MetaHook" `
    "-DFORMAT_VALIDATION_SOURCE_PATH=$source/thirdparty/FormatValidation" `
    "-DVC_LTL_Root=$source/MetaHook/thirdparty/cache/VC-LTL-5.3.1"
if($LASTEXITCODE){throw 'InterpFix configure failed'}
& $cmake --build $build --parallel 4
if($LASTEXITCODE){throw 'InterpFix build failed'}
& $cmake -E chdir $build ctest --output-on-failure
if($LASTEXITCODE){throw 'InterpFix regression tests failed'}
& $cmake --install $build
if($LASTEXITCODE){throw 'InterpFix staging failed'}
