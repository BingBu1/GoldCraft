param(
    [ValidateSet('paced','paced-reliable','burst','rollover')][string]$Mode='burst',
    [ValidateRange(1,100000)][int]$Frames=360,
    [string]$Receiver,
    [switch]$TcpOnly
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$javaRoot=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1/bin')
$classes=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'build/java-shared-memory')
$evidence=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/goldcraft-tests/shared-memory')
$temporary=Assert-WorkspacePath (Join-Path $classes 'tmp')
if(-not $Receiver){$Receiver=Join-Path $script:GoldCraftRoot 'build/native-x86/Release/goldcraft_shared_bulk_receiver.exe'}
$Receiver=Assert-WorkspacePath $Receiver
if(-not (Test-Path -LiteralPath $Receiver -PathType Leaf)){throw 'Build the isolated shared bulk native receiver, or supply -Receiver with its workspace path.'}
if($TcpOnly -and $Mode -eq 'burst'){throw 'Full-ring burst requires shared memory. Use paced or paced-reliable for TCP comparisons.'}
$jars=@('jna','jna-platform') | ForEach-Object {
    $directory=Join-Path $script:GoldCraftRoot ".tools/gradle-cache/caches/modules-2/files-2.1/net.java.dev.jna/$_/5.14.0"
    $matches=@(Get-ChildItem -LiteralPath $directory -Filter "$($_)-5.14.0.jar" -Recurse)
    if($matches.Count -ne 1){throw "Expected one installed pinned JNA5.14.0 JAR: $_"}
    $matches[0].FullName
}
New-Item -ItemType Directory -Path $classes,$evidence,$temporary -Force | Out-Null
$classpath=$jars -join ';'
$source=@('neoforge/src/main/java/dev/goldcraft/bridge/Wire.java','neoforge/src/main/java/dev/goldcraft/bridge/BridgeLink.java','neoforge/src/main/java/dev/goldcraft/bridge/SharedBulkSender.java','tools/java/SharedBulkInteropMain.java') | ForEach-Object {Join-Path $script:GoldCraftRoot $_}
& (Join-Path $javaRoot 'javac.exe') --release 21 -encoding UTF-8 -cp $classpath -d $classes @source
if($LASTEXITCODE){throw 'Shared-memory Java transport compile failed'}
$output=Join-Path $evidence "$Mode-$(if($TcpOnly){'tcp'}else{'shared'}).json"
$previous=[Environment]::GetEnvironmentVariable('GOLDCRAFT_SHARED_MEMORY','Process')
try {
    $env:GOLDCRAFT_SHARED_MEMORY=if($TcpOnly){'0'}else{'1'}
    & (Join-Path $javaRoot 'java.exe') -Xmx768m "-Djava.io.tmpdir=$temporary" "-Djna.tmpdir=$temporary" -cp "$classes;$classpath" dev.goldcraft.bridge.SharedBulkInteropMain $Receiver $Mode $output $Frames
    if($LASTEXITCODE){throw 'Actual Java/native shared-memory interoperability failed'}
}finally{[Environment]::SetEnvironmentVariable('GOLDCRAFT_SHARED_MEMORY',$previous,'Process')}
Get-Item -LiteralPath $output
