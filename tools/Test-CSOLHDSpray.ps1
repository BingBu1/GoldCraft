param([string]$Output='build/csol-hd-spray-20261030/offline',
      [ValidateSet('all','codec','server','import','client','geometry','options')][string]$Only='all')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/SandboxPaths.ps1"
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$dest=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $Output)
New-Item -ItemType Directory -Force -Path $dest | Out-Null
$cc=Join-Path $compiler.Root 'bin/clang-cl.exe'
$flags=@('--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/W3','/O2','/clang:-O3','/arch:AVX2','/fp:precise','/utf-8','/DNDEBUG','/D_CRT_SECURE_NO_WARNINGS')
if($Only -in @('all','codec')){
& $cc @flags "$script:GoldCraftRoot/tests/csol-hd-spray/codec.cpp" "/Fo$dest/codec.obj" "/Fe$dest/codec.exe" *> "$dest/codec-build.log"
if($LASTEXITCODE){throw "Codec build failed: $dest/codec-build.log"}
& "$dest/codec.exe" | Tee-Object "$dest/codec-result.txt"
if($LASTEXITCODE){throw 'HD spray codec/transfer check failed'}
}
if($Only -in @('all','server')){
$sdk="$script:GoldCraftRoot/external/ReGameDLL_CS/regamedll"
$includes=@('','common','dlls','engine','public','public/regamedll','pm_shared','regamedll','game_shared') | ForEach-Object {"/I$sdk/$_"}
$defines=@('REGAMEDLL_ADD','REGAMEDLL_API','REGAMEDLL_FIXES','REGAMEDLL_SSE','REGAMEDLL_SELF','REGAMEDLL_CHECKS','UNICODE_FIXES','BUILD_LATEST','BUILD_LATEST_FIXES','CLIENT_WEAPONS','USE_QSTRING','_MBCS') | ForEach-Object {"/D$_"}
& $cc @flags @includes @defines /Zc:strictStrings- "$script:GoldCraftRoot/tests/csol-hd-spray/server.cpp" "/Fo$dest/server.obj" "/Fe$dest/server.exe" *> "$dest/server-build.log"
if($LASTEXITCODE){throw "Matched SDK server build failed: $dest/server-build.log"}
& "$dest/server.exe" | Tee-Object "$dest/server-result.txt"
if($LASTEXITCODE){throw 'HD spray server check failed'}
}
if($Only -in @('all','import')){
& $cc @flags /DNOMINMAX "$script:GoldCraftRoot/tests/csol-hd-spray/import.cpp" "$script:GoldCraftRoot/native/csol/client/hd_spray_import.cpp" "/Fo$dest/" "/Fe$dest/import.exe" /link windowscodecs.lib ole32.lib *> "$dest/import-build.log"
if($LASTEXITCODE){throw "WIC import build failed: $dest/import-build.log"}
& "$dest/import.exe" "$dest/images" | Tee-Object "$dest/import-result.txt"
if($LASTEXITCODE){throw 'HD spray real image import check failed'}
}
if($Only -in @('all','client','geometry','options')){
$meta="$script:GoldCraftRoot/external/MetaHookSv-20261007"
$sdk="$meta/MetaHook/include"
$includes=@('','Interface','HLSDK/cl_dll','HLSDK/common','HLSDK/engine','HLSDK/pm_shared','HLSDK/public','SourceSDK','SourceSDK/tier0','SourceSDK/tier1') | ForEach-Object {"/I$sdk/$_"}
$includes+=@("/I$meta/MetaHook/thirdparty/capstone_fork/include","/I$meta/MetaHook/thirdparty/rapidjson/include","/I$meta/thirdparty/glew_fork/include","/I$meta/Plugins/Renderer/include/Interface")
$defines=@('/DNOMINMAX','/DNO_MALLOC_OVERRIDE','/DGLEW_STATIC','/DGLEW_NO_GLU')
if($Only -in @('all','options')){
$vguiIncludes=@('VGUI','Interface/VGUI','vgui_controls','SourceSDK/vstdlib') | ForEach-Object {"/I$sdk/$_"}
$vguiIncludes+="/I$meta/Plugins/VGUI2Extension/include/Interface"
$vguiDefines=@('VGUI_USE_SURFACE2','VGUI_USE_SCHEME2','VGUI_USE_INPUT2','NO_TIER0_MINIDUMP','NO_VCR') | ForEach-Object {"/D$_"}
& $cc @flags @includes @defines @vguiIncludes @vguiDefines /c "$script:GoldCraftRoot/native/csol/client/hd_spray_options.cpp" "/Fo$dest/options.obj" *> "$dest/options-build.log"
if($LASTEXITCODE){throw "Options compile failed: $dest/options-build.log"}
'Production options compiled against matched VGUI SDK; live UI pending' | Tee-Object "$dest/options-result.txt"
}
if($Only -in @('all','client')){
& $cc @flags @includes @defines "$script:GoldCraftRoot/tests/csol-hd-spray/client.cpp" "/Fo$dest/client.obj" "/Fe$dest/client.exe" *> "$dest/client-build.log"
if($LASTEXITCODE){throw "Client protocol build failed: $dest/client-build.log"}
& "$dest/client.exe" | Tee-Object "$dest/client-result.txt"
if($LASTEXITCODE){throw 'HD spray client protocol check failed'}
}
if($Only -in @('all','geometry')){
& $cc --target=i686-pc-windows-msvc /c /O2 /MT /DGLEW_STATIC "/I$meta/thirdparty/glew_fork/include" "$meta/thirdparty/glew_fork/src/glew.c" "/Fo$dest/glew.obj" *> "$dest/glew-build.log"
if($LASTEXITCODE){throw 'Geometry SDK GLEW build failed'}
& $cc @flags @includes @defines "$script:GoldCraftRoot/tests/csol-hd-spray/geometry.cpp" "$sdk/HLSDK/common/interface.cpp" "/Fo$dest/" "/Fe$dest/geometry.exe" /link "$dest/glew.obj" opengl32.lib gdi32.lib *> "$dest/geometry-build.log"
if($LASTEXITCODE){throw "Geometry build failed: $dest/geometry-build.log"}
& "$dest/geometry.exe" | Tee-Object "$dest/geometry-result.txt"
if($LASTEXITCODE){throw 'HD spray BSP geometry check failed'}
}
}
