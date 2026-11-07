$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$script:GoldCraftRoot=$taskRoot
. (Join-Path $taskRoot 'tools/ClangToolchain.ps1')
$toolchain=Initialize-GoldCraftCompiler
$clang=Join-Path $toolchain.Root 'bin/clang-cl.exe'
$output=Join-Path $taskRoot 'build/engine-model-profile-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$inputs=@($PSCommandPath,(Join-Path $PSScriptRoot 'fixture.cpp'),(Join-Path $taskRoot 'native/engine/model_load_profile.h'))
$before=@($inputs | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()} })
$object=Join-Path $output 'fixture.obj'
$exe=Join-Path $output 'fixture.exe'
$arguments=@('--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/Gy','/Gw','/fp:precise','/DNDEBUG','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/Y-','/c',(Join-Path $PSScriptRoot 'fixture.cpp'),"/Fo$object")
@{compiler=$clang;arguments=$arguments;inputs=$before;singleJob=$true;actualProductionHeader=$true} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'plan.json') -Encoding utf8
& $clang @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'compile.log')
if($LASTEXITCODE -ne 0){throw 'profile fixture compilation failed'}
& $clang '--target=i686-pc-windows-msvc' '-fuse-ld=lld' '/MT' $object "/Fe$exe" '/link' '/OPT:REF' '/OPT:ICF' '/OPT:LLDLTO=3' '/threads:1' 2>&1 | Tee-Object -FilePath (Join-Path $output 'link.log')
if($LASTEXITCODE -ne 0){throw 'profile fixture link failed'}
& $exe 2>&1 | Tee-Object -FilePath (Join-Path $output 'run.log')
if($LASTEXITCODE -ne 0){throw 'profile fixture failed'}
foreach($item in $before){if((Get-FileHash -LiteralPath $item.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.sha256){throw "Input changed during test: $($item.path)"}}
@{success=$true;inputsUnchanged=$true;inputs=$before;exeSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant();fakeClockAndLogger=$true;liveGameplay=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $output 'result.json') -Encoding utf8
