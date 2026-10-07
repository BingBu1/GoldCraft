param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangCMakeArguments $compiler
$vs=$compiler.VisualStudio
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe')
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$source=Get-MetaHookSourceRoot
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/bulletphysics-clang-$Configuration")
$prefix=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/bulletphysics')
& $cmake -S "$source/Plugins/BulletPhysics" -B $build -G Ninja @clangArgs `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_INSTALL_PREFIX=$prefix" `
    '-DBULLETPHYSICS_BUILD_TESTS=ON' "-DGOLDCRAFT_INCLUDE_DIR=$script:GoldCraftRoot/native/include" `
    "-DMETAHOOK_SOURCE_PATH=$source/MetaHook" "-DVGUI2EXTENSION_SOURCE_PATH=$source/Plugins/VGUI2Extension" `
    "-DGLEW_SOURCE_PATH=$source/thirdparty/glew_fork" "-DBULLET3_SOURCE_PATH=$source/thirdparty/bullet3" `
    "-DSCOPEEXIT_SOURCE_PATH=$source/thirdparty/ScopeExit" "-DTINYOBJLOADER_SOURCE_PATH=$source/thirdparty/tinyobjloader" `
    "-DVC_LTL_Root=$source/MetaHook/thirdparty/cache/VC-LTL-5.3.1"
if($LASTEXITCODE){throw 'BulletPhysics configure failed'}
& $cmake --build $build --parallel 4
if($LASTEXITCODE){throw 'BulletPhysics build failed'}
& $cmake -E chdir $build ctest --output-on-failure
if($LASTEXITCODE){throw 'BulletPhysics tests failed'}
& $cmake --install $build
if($LASTEXITCODE){throw 'BulletPhysics staging failed'}
