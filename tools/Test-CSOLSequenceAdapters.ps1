param([string]$OutputDirectory='build/csol-sequence-runtime-20261029/adapter-tests')
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
$output=[IO.Path]::GetFullPath((Join-Path $script:GoldCraftRoot $OutputDirectory))
if(-not $output.StartsWith($script:GoldCraftRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Test output must remain in the workspace.'}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$clientIncludes=@('MetaHook/include','MetaHook/include/Interface','MetaHook/include/HLSDK/cl_dll','MetaHook/include/HLSDK/common','MetaHook/include/HLSDK/engine','MetaHook/include/HLSDK/pm_shared','MetaHook/include/HLSDK/public','MetaHook/include/SourceSDK','MetaHook/include/SourceSDK/tier0','MetaHook/include/SourceSDK/tier1','MetaHook/thirdparty/capstone_fork/include','MetaHook/thirdparty/rapidjson/include','thirdparty/glew_fork/include','Plugins/Renderer/include/Interface') | ForEach-Object {"/I$script:GoldCraftRoot/external/MetaHookSv-20261007/$_"}
$engineIncludes=@('','metamod/include','public/rehlds','common','engine','public','pm_shared','rehlds','testsuite') | ForEach-Object {"/I$script:GoldCraftRoot/external/ReHLDS/rehlds/$_"}
$engineIncludes+=@("/I$script:GoldCraftRoot/native/engine","/I$script:GoldCraftRoot/native/include","/I$script:GoldCraftRoot/external/ReHLDS/dep/bzip2/include","/I$script:GoldCraftRoot/external/ReHLDS/dep/cppunitlite/include","/I$script:GoldCraftRoot/external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include")
$common=@('--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/Gy','/Gw','/fp:precise','/DNDEBUG','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS','/Y-','/c')
$plan=@(
    @{name='client';source='native/csol/sequence_client_adapter_tests.cpp';flags=($common+$clientIncludes+@('/DGLEW_NO_GLU','/DGLEW_STATIC','/DNO_MALLOC_OVERRIDE'))},
    @{name='engine';source='native/csol/sequence_engine_adapter_tests.cpp';flags=($common+$engineIncludes+@('/DREHLDS_API','/DREHLDS_FIXES','/DREHLDS_SSE','/DREHLDS_OPT_PEDANTIC','/DREHLDS_SELF','/DREHLDS_CHECKS','/DHAVE_OPT_STRTOOLS','/DUSE_BREAKPAD_HANDLER','/DDEDICATED','/DSWDS'))}
)
$inputs=@('native/csol/sequence_client_adapter_tests.cpp','native/csol/sequence_engine_adapter_tests.cpp','native/csol/sequence_adapter_fixture.hpp','native/csol/client/sequence_guard.cpp','native/csol/client/sequence_guard.hpp','native/engine/csol_sequence_guard.cpp','native/engine/csol_sequence_guard.h','native/csol/sequence_owners.hpp','native/csol/sequence_safety.hpp','native/include/goldcraft/csol_sequence_api.hpp')
$before=@($inputs | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()} })
@{compiler=$clang;parallelCompiles=1;linkThreads=1;entries=$plan;inputs=$before;realGameExecutions=0} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$output/commands.json" -Encoding utf8
$results=@()
foreach($entry in $plan){
    $name=$entry.name;$object="$output/$name.obj";$exe="$output/$name.exe"
    & $clang @($entry.flags) $entry.source "/Fo$object" 2>&1 | Tee-Object -FilePath "$output/$name-build.log"
    if($LASTEXITCODE -ne 0){throw "$name adapter test compilation failed: $LASTEXITCODE"}
    & $clang '--target=i686-pc-windows-msvc' '-fuse-ld=lld' '/MT' $object "/Fe$exe" '/link' '/OPT:REF' '/OPT:ICF' '/OPT:LLDLTO=3' '/threads:1' 2>&1 | Tee-Object -FilePath "$output/$name-link.log"
    if($LASTEXITCODE -ne 0){throw "$name adapter test link failed: $LASTEXITCODE"}
    & $exe 2>&1 | Tee-Object -FilePath "$output/$name-result.log"
    $result=$LASTEXITCODE;$results+=@{name=$name;exitCode=$result;exeSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()}
    if($result -ne 0){throw "$name adapter tests failed: $result"}
}
foreach($input in $before){if((Get-FileHash -LiteralPath $input.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $input.sha256){throw "Input changed during tests: $($input.path)"}}
@{results=$results;inputHashesUnchanged=$true;productionTranslationUnitsExecuted=2;hostBoundaryStubs=$true;realHooks=$false;liveGameplay=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath "$output/results.json" -Encoding utf8
