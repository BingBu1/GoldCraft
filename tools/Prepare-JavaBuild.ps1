. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$javaRoot=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1')
if(-not(Test-Path -LiteralPath (Join-Path $javaRoot 'bin/java.exe'))){
    $javaArchive=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/downloads/jdk21.zip')
    New-Item -ItemType Directory -Path (Split-Path $javaArchive) -Force | Out-Null
    $javaUrl='https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip'
    if(-not(Test-Path -LiteralPath $javaArchive)){Invoke-WebRequest -Uri $javaUrl -OutFile $javaArchive}
    if((Get-FileHash -LiteralPath $javaArchive -Algorithm SHA256).Hash -ne 'F9D6E191AB098C0D416E7D588A24420A8621CD2F4720DAB2459B8B7B2D2D8B4E'){
        throw 'Pinned Temurin 21 download checksum mismatch'
    }
    Expand-Archive -LiteralPath $javaArchive -DestinationPath (Split-Path $javaRoot)
}
$gradleVersion = '8.10.2'
$gradleRoot = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot ".tools/gradle/gradle-$gradleVersion")
$archive = Assert-WorkspacePath (Join-Path $script:GoldCraftRoot ".tools/downloads/gradle-$gradleVersion-bin.zip")
if (-not (Test-Path -LiteralPath (Join-Path $gradleRoot 'bin/gradle.bat'))) {
    New-Item -ItemType Directory -Path (Split-Path $archive) -Force | Out-Null
    $uri = "https://services.gradle.org/distributions/gradle-$gradleVersion-bin.zip"
    $response = (Invoke-WebRequest -Uri "$uri.sha256").Content
    $expected = if ($response -is [byte[]]) { [Text.Encoding]::UTF8.GetString($response).Trim() } else { $response.Trim() }
    if ($expected -notmatch '^[a-f0-9]{64}$') { throw 'Invalid official Gradle checksum.' }
    if (-not (Test-Path -LiteralPath $archive)) { Invoke-WebRequest -Uri $uri -OutFile $archive }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) { throw 'Gradle download SHA-256 mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath (Split-Path $gradleRoot)
    $expected | Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
}
Write-Host "Gradle ready: $gradleRoot"
