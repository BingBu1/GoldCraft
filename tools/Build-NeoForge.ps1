param([string[]]$Tasks = @('build'))
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$env:JAVA_HOME = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1')
$env:GRADLE_USER_HOME = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/gradle-cache')
$env:PATH = (Join-Path $env:JAVA_HOME 'bin') + ';' + $env:PATH
$gradle = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/gradle/gradle-8.10.2/bin/gradle.bat')
if (-not (Test-Path -LiteralPath $gradle) -or -not (Test-Path -LiteralPath (Join-Path $env:JAVA_HOME 'bin/java.exe'))) {
    & (Join-Path $PSScriptRoot 'Prepare-JavaBuild.ps1')
}
Push-Location (Join-Path $script:GoldCraftRoot 'neoforge')
try {
    & $gradle --console=plain --no-daemon @Tasks
    if ($LASTEXITCODE -ne 0) { throw "NeoForge Gradle failed with exit code $LASTEXITCODE" }
} finally { Pop-Location }
