param()
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$env:PIP_CACHE_DIR=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/pip-cache')
# Core/Renderer use Ninja and CMake 3 policies in pinned Capstone. Native
# GoldCraft uses the VS 2026 generator, first supported by CMake 4.2.
foreach($tool in @(@{folder='cmake331';version='3.31.10'},@{folder='cmake434';version='4.3.4'})){
    $package=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot ".tools/$($tool.folder)")
    $cmake=Join-Path $package 'cmake/data/bin/cmake.exe'
    if(-not(Test-Path -LiteralPath $cmake)){
        & python -m pip install --disable-pip-version-check --target $package "cmake==$($tool.version)"
        if($LASTEXITCODE){throw 'Workspace CMake installation failed'}
    }
    & $cmake --version
    if($LASTEXITCODE){throw 'Workspace CMake is not runnable'}
}
Write-Output 'Install Visual Studio C++ x86 tools and Windows SDK 10.0.26100.0 before building native components.'
