param([Parameter(Mandatory)][int]$Protocol)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$ErrorActionPreference='Stop'
if((Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'native/include/goldcraft/wire.hpp') -Raw) -notmatch "protocol_version = $Protocol;"){
    throw 'Requested protocol differs from the native source.'
}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
$backup=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/deployment-backups/before-protocol$Protocol-$stamp")
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$running=Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -like "$script:GoldCraftRoot\sandbox\cs-client-*\Half-Life\MetaHook.exe" -or
    $_.ExecutablePath -eq "$script:GoldCraftRoot\sandbox\cs-server\Half-Life\hlds.exe"
}
if($running){throw 'Stop recorded sandbox native processes before deployment.'}
$files=[Collections.Generic.List[object]]::new()
function Deploy-One([string]$Source,[string]$Destination){
    $from=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $Source)
    $to=Assert-SandboxPath (Join-Path $script:GoldCraftRoot $Destination)
    $old=Assert-SandboxPath (Join-Path $backup $Destination.Substring('sandbox/'.Length))
    New-Item -ItemType Directory -Path (Split-Path $old) -Force | Out-Null
    if(Test-Path -LiteralPath $to){Copy-Item -LiteralPath $to -Destination $old}
    Copy-Item -LiteralPath $from -Destination $to -Force
    $hash=(Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash
    if($hash -ne (Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash){throw 'Deployed hash mismatch.'}
    $files.Add(@{path=$Destination;sha256=$hash})
}
foreach($instance in @('cs-client-a','cs-client-b')){
    Deploy-One 'build/native-x86/Release/GoldCraft.dll' "sandbox/$instance/Half-Life/cstrike/metahook/plugins/GoldCraft.dll"
    Deploy-One 'build/regamedll/Release/mp.dll' "sandbox/$instance/Half-Life/cstrike/dlls/mp.dll"
}
Deploy-One 'build/regamedll/Release/mp.dll' 'sandbox/cs-server/Half-Life/cstrike/dlls/mp.dll'
foreach($name in @('goldcraft','goldcraft_test')){
    Deploy-One "dist/amxx/$name.amxx" "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/plugins/$name.amxx"
}
foreach($name in @('rehlds-deployment.json','amxx-deployment.json')){
    $path=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/cs-server/$name")
    $data=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    foreach($entry in $data.files){
        $item=Get-Item -LiteralPath (Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/cs-server/Half-Life/$($entry.name)"))
        $hash=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash
        $deployed=$files | Where-Object {$_.path -eq "sandbox/cs-server/Half-Life/$($entry.name)"}
        if(-not $deployed -and $entry.sha256 -ne $hash){throw "Undeployed dependency changed: $($entry.name)"}
        $entry.sha256=$hash
        if($entry.PSObject.Properties['bytes']){$entry.bytes=$item.Length}
    }
    $data | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $path -Encoding utf8
}
$report=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "analysis/goldcraft-tests/deployment$Protocol-$stamp.json")
@{protocol=$Protocol;backup=$backup;files=$files.ToArray();utc=$stamp;note='Compiled Minecraft runtime is loaded through the selected loader manifests on restart.'} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $report -Encoding utf8
Write-Output "Deployed protocol $Protocol native artifacts to stopped sandbox copies. Evidence: $report"
