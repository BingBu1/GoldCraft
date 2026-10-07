# Shared clang-cl/MSVC-ABI toolchain selection. The machine's LLVM path is kept
# outside publication in .tools/clang-toolchain.json, or supplied by environment.
function Get-GoldCraftClang {
    $llvmRoot=$env:GOLDCRAFT_LLVM_ROOT
    $config=Join-Path $script:GoldCraftRoot '.tools/clang-toolchain.json'
    if(-not $llvmRoot -and (Test-Path -LiteralPath $config)){
        $llvmRoot=(Get-Content -LiteralPath $config -Raw | ConvertFrom-Json).root
    }
    if(-not $llvmRoot){throw 'Configure Clang with tools/Configure-Clang.ps1 -LLVMRoot <installation>, or set GOLDCRAFT_LLVM_ROOT.'}
    $llvmRoot=(Resolve-Path -LiteralPath $llvmRoot).Path
    foreach($name in @('clang-cl.exe','lld-link.exe','llvm-lib.exe')){
        if(-not(Test-Path -LiteralPath (Join-Path $llvmRoot "bin/$name"))){throw "Missing LLVM tool: $name"}
    }
    return $llvmRoot
}

function Initialize-GoldCraftCompiler {
    if(-not [System.Runtime.Intrinsics.X86.Avx2]::IsSupported){throw 'The optimized GoldCraft runtime requires an AVX2-capable build/test host.'}
    $llvmRoot=Get-GoldCraftClang
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(-not $vs){throw 'Visual Studio C++ headers and Windows SDK not found'}
    Import-Module (Join-Path $vs 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -winsdk=10.0.26100.0' | Out-Null
    $env:VSLANG='1033'
    $env:PATH=(Join-Path $llvmRoot 'bin')+';'+$env:PATH
    return @{Root=$llvmRoot;VisualStudio=$vs;Ninja=(Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe')}
}

function Get-GoldCraftClangCMakeArguments($Compiler) {
    return @("-DCMAKE_TOOLCHAIN_FILE=$script:GoldCraftRoot/cmake/clang-x86.cmake",
        "-DGOLDCRAFT_LLVM_ROOT=$($Compiler.Root)",'-DCMAKE_CXX_STANDARD=20','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON')
}

function Get-GoldCraftClangMSBuildArguments($Compiler) {
    # Override the compiler/linker tools without depending on the optional VS
    # ClangCL component. cl.exe is never called. Keep the installed SDK/CRT ABI.
    return @('/p:PlatformToolset=v145','/p:PreferredToolArchitecture=x64',
        '/p:WindowsTargetPlatformVersion=10.0.26100.0','/p:VcpkgEnabled=false',
        "/p:CLToolPath=$($Compiler.Root)/bin/",'/p:CLToolExe=clang-cl.exe',
        "/p:LinkToolPath=$($Compiler.Root)/bin/",'/p:LinkToolExe=lld-link.exe',
        "/p:LibToolPath=$($Compiler.Root)/bin/",'/p:LibToolExe=llvm-lib.exe',
        '/p:WholeProgramOptimization=false',
        "/p:ForceImportAfterCppTargets=$script:GoldCraftRoot/cmake/GoldCraftClang.targets")
}
