param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangCMakeArguments $compiler
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe')
$ninja=Join-Path $compiler.VisualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$source=Get-MetaHookSourceRoot
$clangArgs+="-DFORMAT_VALIDATION_SOURCE_PATH=$source/thirdparty/FormatValidation"
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/vgui2extension-clang-$Configuration")
$prefix=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/vgui2extension')
& $cmake -S "$source/Plugins/VGUI2Extension" -B $build -G Ninja @clangArgs `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_INSTALL_PREFIX=$prefix" `
    '-DVGUI2EXTENSION_BUILD_TESTS=ON' "-DMETAHOOK_SOURCE_PATH=$source/MetaHook" `
    "-DSDL2_INCLUDE_DIRS=$source/MetaHook/thirdparty/sdl2-compat-fork/include" `
    "-DSDL3_INCLUDE_DIRS=$source/MetaHook/thirdparty/SDL3_fork/include" `
    "-DVC_LTL_Root=$source/MetaHook/thirdparty/cache/VC-LTL-5.3.1"
if($LASTEXITCODE){throw 'VGUI2Extension configure failed'}
& $cmake --build $build --parallel 4
if($LASTEXITCODE){throw 'VGUI2Extension build failed'}
& $cmake -E chdir $build ctest --output-on-failure
if($LASTEXITCODE){throw 'VGUI2Extension regression tests failed'}
& $cmake --install $build
if($LASTEXITCODE){throw 'VGUI2Extension staging failed'}
