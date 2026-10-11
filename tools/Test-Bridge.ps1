param([ValidateSet('x86','x64')][string]$Architecture='x86', [ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$javaRoot=Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1/bin'
$classes=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/java-interop')
$evidence=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/goldcraft-tests')
New-Item -ItemType Directory -Path $classes,$evidence -Force | Out-Null
$temporary=Assert-WorkspacePath (Join-Path $classes 'tmp')
New-Item -ItemType Directory -Path $temporary -Force | Out-Null
$transportSources=@()
$compileOptions=@()
$runtimeClasspath=$classes
if($Loader -eq 'neoforge'){
    $jars=@('jna','jna-platform') | ForEach-Object {
        $directory=Join-Path $script:GoldCraftRoot ".tools/gradle-cache/caches/modules-2/files-2.1/net.java.dev.jna/$_/5.14.0"
        $matches=@(Get-ChildItem -LiteralPath $directory -Filter "$($_)-5.14.0.jar" -Recurse)
        if($matches.Count -ne 1){throw "Expected one installed pinned JNA5.14.0 JAR: $_"}
        $matches[0].FullName
    }
    $compileOptions=@('-cp',($jars -join ';'))
    $runtimeClasspath="$classes;"+($jars -join ';')
    $transportSources=@(Join-Path $script:GoldCraftRoot 'neoforge/src/main/java/dev/goldcraft/bridge/SharedBulkSender.java')
}
& (Join-Path $javaRoot 'javac.exe') -encoding UTF-8 -d $classes @compileOptions @transportSources `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/Wire.java") `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/BridgeLink.java") `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/HudPixels.java") `
    (Join-Path $script:GoldCraftRoot "$Loader/src/main/java/dev/goldcraft/bridge/ParticleSnapshot.java") `
    (Join-Path $script:GoldCraftRoot 'tests/java/LinkInteropMain.java')
if($LASTEXITCODE){throw 'Java bridge test compile failed'}
$native=Join-Path $script:GoldCraftRoot "build/native-$Architecture/Release"
& (Join-Path $native 'goldcraft_native_tests.exe') | Tee-Object -FilePath (Join-Path $evidence "native-$Architecture.json")
if($LASTEXITCODE){throw 'Native tests failed'}
& (Join-Path $javaRoot 'java.exe') "-Djava.io.tmpdir=$temporary" "-Djna.tmpdir=$temporary" -cp $runtimeClasspath LinkInteropMain (Join-Path $native 'goldcraft_link_probe.exe') |
    Tee-Object -FilePath (Join-Path $evidence "interop-$Architecture.json")
if($LASTEXITCODE){throw 'C++/Java process interoperability failed'}
