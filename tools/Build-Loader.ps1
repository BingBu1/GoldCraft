param([ValidateSet('Release','Debug')][string]$Configuration='Release')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'ClangToolchain.ps1')
$compiler=Initialize-GoldCraftCompiler
$clangArgs=Get-GoldCraftClangCMakeArguments $compiler
$vs=$compiler.VisualStudio
$cmake=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/cmake331/cmake/data/bin/cmake.exe')
if(-not(Test-Path -LiteralPath $cmake)){throw 'Install workspace cmake==3.31.10 first (pinned Capstone uses CMake 3 policies).'}
$ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$source=Get-MetaHookSourceRoot
$formatArgs=@("-DFORMAT_VALIDATION_SOURCE_PATH=$source/thirdparty/FormatValidation")
$build=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/metahook-clang-$Configuration-sdk26100")
$prefix=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/metahook')
& $cmake -S (Join-Path $source 'MetaHook') -B $build -G Ninja @clangArgs @formatArgs `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja" '-DMETAHOOK_BUILD_SDL=OFF' "-DCMAKE_INSTALL_PREFIX=$prefix"
if($LASTEXITCODE){throw 'Loader configure failed'}
$log=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/logs/metahook-build.log')
New-Item -ItemType Directory -Path (Split-Path $log) -Force | Out-Null
& $cmake --build $build --parallel 4 *> $log
if($LASTEXITCODE){
    Get-Content -LiteralPath $log | Select-String -Pattern 'FAILED:|error |fatal error|LNK[0-9]+' | Select-Object -First 16
    throw "Loader build failed; full log: $log"
}
Get-Content -LiteralPath $log -Tail 6
& $cmake --install $build
if($LASTEXITCODE){throw 'Loader staging failed'}

# The loader's standalone suite includes the appended command-line API and
# verifies the old virtual slots through an independently compiled interface.
$testBuild=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/metahook-tests-clang-$Configuration")
& $cmake -S (Join-Path $source 'MetaHook/tests') -B $testBuild -G Ninja @clangArgs `
    "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_MAKE_PROGRAM=$ninja"
if($LASTEXITCODE){throw 'Loader test configure failed'}
& $cmake --build $testBuild --parallel 4
if($LASTEXITCODE){throw 'Loader test build failed'}
& $cmake -E chdir $testBuild ctest --output-on-failure
if($LASTEXITCODE){throw 'Loader regression tests failed'}
