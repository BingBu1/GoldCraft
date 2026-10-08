param([ValidateRange(0,30)][int]$Bots=24)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/Half-Life')
$client=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-client-b/Half-Life')
$running=Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -eq (Join-Path $game 'hlds.exe') -or $_.ExecutablePath -eq (Join-Path $client 'MetaHook.exe')
}
if($running){throw 'Stop the sandbox server and B client before installing Zombie Plague/SyPB.'}
$stage=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'dist/zombieplague')
$manifest=Get-Content -LiteralPath (Join-Path $stage 'manifest.json') -Raw | ConvertFrom-Json
$amxx=Assert-SandboxPath (Join-Path $game 'cstrike/addons/amxmodx')
$source=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'amxx/zombie_plague')
$files=[Collections.Generic.List[object]]::new()
function Copy-Verified([string]$From,[string]$To,[string]$Expected=''){
    $from=Assert-WorkspacePath $From;$to=Assert-SandboxPath $To
    $hash=(Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash
    if($Expected -and $hash -ne $Expected){throw "Build/staging hash changed: $from"}
    New-Item -ItemType Directory -Path (Split-Path $to) -Force | Out-Null
    if(-not(Test-Path -LiteralPath $to) -or (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $hash){
        Copy-Item -LiteralPath $from -Destination $to -Force
    }
    if((Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash -ne $hash){throw "Copy verification failed: $to"}
    $files.Add(@{path=$to.Substring($script:GoldCraftRoot.Length+1);sha256=$hash})
}
function Add-Line([string]$Path,[string]$Line){
    $path=Assert-SandboxPath $Path
    $lines=if(Test-Path -LiteralPath $path){@(Get-Content -LiteralPath $path)}else{@()}
    if($Line -notin $lines){Add-Content -LiteralPath $path -Value $Line -Encoding ascii}
}
foreach($entry in $manifest.plugins){Copy-Verified (Join-Path $script:GoldCraftRoot "build/amxx/plugins/$($entry.name).amxx") (Join-Path $amxx "plugins/$($entry.name).amxx") $entry.sha256}
# Keep editable configuration on subsequent deployments; retain upstream notices.
foreach($item in Get-ChildItem -LiteralPath (Join-Path $source 'configs') -File -Recurse){
    $relative=$item.FullName.Substring($source.Length+1)
    $target=Assert-SandboxPath (Join-Path $amxx $relative)
    if(-not(Test-Path -LiteralPath $target)){Copy-Verified $item.FullName $target}
}
foreach($name in @('zombie_plague','zombie_plague50')){
    $target=Assert-SandboxPath (Join-Path $amxx "data/lang/$name.txt")
    if(-not(Test-Path -LiteralPath $target)){Copy-Verified (Join-Path $source "lang/$name.txt") $target}
}
foreach($name in @('goldcraft_zp50','goldcraft_bloodmoon','goldcraft_spawn_capacity')){
    Copy-Verified (Join-Path $script:GoldCraftRoot "build/amxx/plugins/$name.amxx") (Join-Path $amxx "plugins/$name.amxx")
}
& python (Join-Path $PSScriptRoot 'Localize-ZombiePlague.py') --install
if($LASTEXITCODE){throw 'Zombie Plague Chinese localization failed'}
Add-Line (Join-Path $amxx 'configs/plugins-zp50_ammopacks.ini') 'goldcraft_zp50.amxx'
Add-Line (Join-Path $amxx 'configs/plugins.ini') 'goldcraft_bloodmoon.amxx'
Add-Line (Join-Path $amxx 'configs/plugins.ini') 'goldcraft_spawn_capacity.amxx'
Copy-Verified (Join-Path $script:GoldCraftRoot 'build/sypb/Release/sypb.dll') (Join-Path $game 'cstrike/addons/sypb/sypb.dll')
Copy-Verified (Join-Path $script:GoldCraftRoot 'build/sypb/Release/sypb_amxx.dll') (Join-Path $amxx 'modules/sypb_amxx.dll')
Copy-Verified (Join-Path $script:GoldCraftRoot 'external/SyPB/LICENSE') (Join-Path $game 'cstrike/addons/sypb/LICENSE')
Add-Line (Join-Path $amxx 'configs/modules.ini') 'sypb'
$meta=Assert-SandboxPath (Join-Path $game 'cstrike/addons/metamod/plugins.ini')
$line='win32 addons/sypb/sypb.dll'
$metaLines=@(Get-Content -LiteralPath $meta | Where-Object {$_ -ne $line})
@($line)+$metaLines | Set-Content -LiteralPath $meta -Encoding ascii
$botRoot=Assert-SandboxPath (Join-Path $game 'cstrike/addons/sypb')
foreach($folder in @('language','logs','wptdefault/data')){New-Item -ItemType Directory -Path (Assert-SandboxPath (Join-Path $botRoot $folder)) -Force | Out-Null}
$population=if($Bots){$Bots+1}else{-1}
$botConfig=Get-Content -LiteralPath (Join-Path $source 'integration/configs/sypb.cfg') -Raw
foreach($setting in @{sypb_quota=$Bots;sypb_auto_players=$population}.GetEnumerator()){
    $pattern='(?m)^'+$setting.Key+'\s+[^\r\n]+'
    if(-not [Regex]::IsMatch($botConfig,$pattern)){throw "Missing required Bot setting: $($setting.Key)"}
    $botConfig=[Regex]::Replace($botConfig,$pattern,$setting.Key+' '+$setting.Value)
}
$botConfig | Set-Content -LiteralPath (Join-Path $botRoot 'sypb.cfg') -Encoding utf8NoBOM
# server.cfg can execute the base test cfg after plugin_cfg and command-line
# cvars. Apply the zombie settings at the end of that cfg on every map load.
Add-Line (Join-Path $game 'cstrike/goldcraft_test.cfg') 'exec addons/sypb/sypb.cfg'
if(-not(Test-Path -LiteralPath (Join-Path $botRoot 'language/en_names.cfg'))){
    1..32 | ForEach-Object {"GoldCraft_Bot$_"} | Set-Content -LiteralPath (Join-Path $botRoot 'language/en_names.cfg') -Encoding ascii
}
foreach($entry in $manifest.resources){
    foreach($root in @($game,$client)){
        $available=@('cstrike','valve') | Where-Object {
            $candidate=Assert-SandboxPath (Join-Path $root "$_/$($entry.path)")
            (Test-Path -LiteralPath $candidate) -and (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -eq $entry.sha256
        }
        if(-not $available){Copy-Verified (Join-Path $stage "media/$($entry.path)") (Join-Path $root "cstrike/$($entry.path)") $entry.sha256}
    }
}
$result=Assert-SandboxPath (Join-Path $script:GoldCraftRoot 'sandbox/cs-server/zombie-deployment.json')
@{zp='5.0.8a';sypb='1.50';sypbCommit='4c364fbe40d8356154f66527827bb75100aa7265';bots=$Bots;populationTarget=$population;files=$files.ToArray();resourceManifest='dist/zombieplague/manifest.json'} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $result -Encoding utf8
Write-Output "Installed 71 ZP plugins, SyPB 1.50, infection integration and map spawn adapters; bot quota $Bots, population target $population."
