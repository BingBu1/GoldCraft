param([Parameter(Mandatory=$true)][string]$LLVMRoot)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$resolved=(Resolve-Path -LiteralPath $LLVMRoot).Path
foreach($name in @('clang-cl.exe','lld-link.exe','llvm-lib.exe')){
    if(-not(Test-Path -LiteralPath (Join-Path $resolved "bin/$name"))){throw "Missing LLVM tool: $name"}
}
$version=& (Join-Path $resolved 'bin/clang-cl.exe') --version
if($LASTEXITCODE){throw 'Clang version probe failed'}
$config=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/clang-toolchain.json')
@{root=$resolved;version=($version -join "`n");target='i686-pc-windows-msvc';standard='c++20'} |
    ConvertTo-Json | Set-Content -LiteralPath $config -Encoding utf8
Write-Output $version
Write-Output 'Configured x86 clang-cl, C++20, Release O3 + ThinLTO, precise floating point.'
