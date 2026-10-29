param([string]$OutputDirectory='build/engine-cache-20261029')
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
$output=[IO.Path]::GetFullPath((Join-Path $script:GoldCraftRoot $OutputDirectory))
if(-not $output.StartsWith($script:GoldCraftRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Test output must remain in the workspace.'}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$includes=@('','metamod/include','public/rehlds','common','engine','public','pm_shared','rehlds','testsuite') | ForEach-Object {"/I$script:GoldCraftRoot/external/ReHLDS/rehlds/$_"}
$includes+=@("/I$script:GoldCraftRoot/native/engine","/I$script:GoldCraftRoot/native/include","/I$script:GoldCraftRoot/external/ReHLDS/dep/bzip2/include","/I$script:GoldCraftRoot/external/ReHLDS/dep/cppunitlite/include")
$flags=@('--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/Gy','/Gw','/fp:precise','/DNDEBUG','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/Y-','/c','/DREHLDS_API','/DREHLDS_FIXES','/DREHLDS_SSE','/DREHLDS_OPT_PEDANTIC','/DREHLDS_SELF','/DREHLDS_CHECKS','/DHAVE_OPT_STRTOOLS','/DUSE_BREAKPAD_HANDLER','/DDEDICATED','/DSWDS')
$inputs=@('tests/engine-cache/main.cpp','external/ReHLDS/rehlds/engine/zone.cpp','external/ReHLDS/rehlds/engine/zone.h')
$before=@($inputs | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()} })
@{compiler=$clang;parallelCompiles=1;flags=$flags;includes=$includes;inputs=$before} | ConvertTo-Json -Depth 8 | Set-Content "$output/commands.json" -Encoding utf8
& $clang @flags @includes tests/engine-cache/main.cpp "/Fo$output/main.obj" 2>&1 | Tee-Object "$output/build.log"
if($LASTEXITCODE -ne 0){throw 'Cache fixture compilation failed'}
& $clang '--target=i686-pc-windows-msvc' '-fuse-ld=lld' '/MT' "$output/main.obj" "/Fe$output/cache-test.exe" '/link' '/OPT:REF' '/OPT:ICF' '/OPT:LLDLTO=3' '/threads:1' 2>&1 | Tee-Object "$output/link.log"
if($LASTEXITCODE -ne 0){throw 'Cache fixture link failed'}
& "$output/cache-test.exe" 2>&1 | Tee-Object "$output/result.log"
$result=$LASTEXITCODE
foreach($input in $before){if((Get-FileHash -LiteralPath $input.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $input.sha256){throw "Input changed during tests: $($input.path)"}}
@{exitCode=$result;inputHashesUnchanged=$true;actualProductionSource=$true;sdkBoundaryStubs=$true;liveGameplay=$false} | ConvertTo-Json | Set-Content "$output/results.json" -Encoding utf8
if($result -ne 0){throw 'Cache fixture execution failed'}
