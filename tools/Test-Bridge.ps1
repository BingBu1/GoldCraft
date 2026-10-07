param([ValidateSet('x86','x64')][string]$Architecture='x86', [ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$javaRoot=Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1/bin'
$classes=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/java-interop')
$evidence=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/goldcraft-tests')
New-Item -ItemType Directory -Path $classes,$evidence -Force | Out-Null
& (Join-Path $javaRoot 'javac.exe') -encoding UTF-8 -d $classes `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/Wire.java") `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/BridgeLink.java") `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/HudPixels.java") `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/ParticleSnapshot.java") `
    (Join-Path $script:GoldCraftRoot 'tests/java/LinkInteropMain.java')
if($LASTEXITCODE){throw 'Java bridge test compile failed'}
$native=Join-Path $script:GoldCraftRoot "build/native-$Architecture/Release"
& (Join-Path $native 'goldcraft_native_tests.exe') | Tee-Object -FilePath (Join-Path $evidence "native-$Architecture.json")
if($LASTEXITCODE){throw 'Native tests failed'}
& (Join-Path $javaRoot 'java.exe') -cp $classes LinkInteropMain (Join-Path $native 'goldcraft_link_probe.exe') |
    Tee-Object -FilePath (Join-Path $evidence "interop-$Architecture.json")
if($LASTEXITCODE){throw 'C++/Java process interoperability failed'}
