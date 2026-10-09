param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangCMakeArguments $compiler
$vs=$compiler.VisualStudio
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe')
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$source=Get-MetaHookSourceRoot
$clangArgs+="-DFORMAT_VALIDATION_SOURCE_PATH=$source/thirdparty/FormatValidation"
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/renderer-clang-avx2-$Configuration")
$prefix=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/renderer')
& $cmake -S "$source/Plugins/Renderer" -B $build -G Ninja @clangArgs `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_INSTALL_PREFIX=$prefix" `
    '-DRENDERER_BUILD_AVX2=ON' '-DRENDERER_BUILD_TESTS=ON' `
    "-DGOLDCRAFT_INCLUDE_DIR=$script:GoldCraftRoot/native/include" `
    "-DMETAHOOK_SOURCE_PATH=$source/MetaHook" "-DVGUI2EXTENSION_SOURCE_PATH=$source/Plugins/VGUI2Extension" `
    "-DUTILTHREADTASK_SOURCE_PATH=$source/PluginLibs/UtilThreadTask" `
    "-DFREEIMAGE_SOURCE_PATH=$source/thirdparty/FreeImage_clone" "-DGLEW_SOURCE_PATH=$source/thirdparty/glew_fork" `
    "-DSCOPEEXIT_SOURCE_PATH=$source/thirdparty/ScopeExit" "-DTINYOBJLOADER_SOURCE_PATH=$source/thirdparty/tinyobjloader" `
    "-DVC_LTL_Root=$source/MetaHook/thirdparty/cache/VC-LTL-5.3.1" `
    "-DSDL2_INCLUDE_DIRS=$source/MetaHook/thirdparty/sdl2-compat-fork/include" `
    "-DSDL3_INCLUDE_DIRS=$source/MetaHook/thirdparty/SDL3_fork/include"
if($LASTEXITCODE){throw 'Renderer configure failed'}
$log=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/logs/renderer-build.log')
New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null
& $cmake --build $build --parallel 4 *> $log
if($LASTEXITCODE){
    Get-Content -LiteralPath $log | Select-String -Pattern 'FAILED:|error |fatal error|LNK[0-9]+' | Select-Object -First 18
    throw "Renderer build failed; full log: $log"
}
Get-Content -LiteralPath $log -Tail 5
& $cmake -E chdir $build ctest --output-on-failure
if($LASTEXITCODE){throw 'Renderer regression tests failed'}
& $cmake --install $build
if($LASTEXITCODE){throw 'Renderer staging failed'}
$utilitySource=Join-Path $source 'PluginLibs/UtilThreadTask'
$utilityBuild=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/utilthreadtask-clang-$Configuration")
& $cmake -S $utilitySource -B $utilityBuild -G Ninja @clangArgs "-DCMAKE_BUILD_TYPE=$Configuration" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_INSTALL_PREFIX=$prefix" `
    "-DMETAHOOK_SOURCE_PATH=$source/MetaHook" "-DVC_LTL_Root=$source/MetaHook/thirdparty/cache/VC-LTL-5.3.1"
if($LASTEXITCODE){throw 'UtilThreadTask configure failed'}
& $cmake --build $utilityBuild --parallel 4
if($LASTEXITCODE){throw 'UtilThreadTask build failed'}
& $cmake -E chdir $utilityBuild ctest --output-on-failure
if($LASTEXITCODE){throw 'UtilThreadTask regression tests failed'}
& $cmake --install $utilityBuild
if($LASTEXITCODE){throw 'UtilThreadTask staging failed'}
