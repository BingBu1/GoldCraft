function Set-Cvar([string]$Original,[string]$Name,[string]$Value) {
    $lines=@($Original -split '\r?\n'|Where-Object {$_ -notmatch ('^\s*'+[regex]::Escape($Name)+'(?=\s|$)')})
    return ($lines -join "`r`n").TrimEnd()+"`r`n"+$Name+' '+$Value+"`r`n"
}
function Configure-Scenario([string]$Root,$Manifest) {
    $changes=@{}
    $settings=Text (Under $Root 'server-settings.json')|ConvertFrom-Json
    foreach($name in @('map','botTotalPlayers')){if(!$settings.PSObject.Properties[$name]){throw "Missing existing server setting: $name"}}
    $settings.map='zs_deadend';$settings.botTotalPlayers=0
    $settings|Add-Member -NotePropertyName csolGameMode -NotePropertyValue 2 -Force
    $changes['server-settings.json']=($settings|ConvertTo-Json -Depth 30)+"`r`n"
    $original=Text (Under $Root 'scripts/Run-Server.ps1')
    $lines=@($original -split '\r?\n');$hits=0
    for($i=0;$i -lt $lines.Count;$i++) {
        if($lines[$i] -match '^\s*\$info\.Arguments\s*=\s*".*-game cstrike.*\+map '){
            $hits++;$line=$lines[$i] -replace '\s+-csol_game_mode\s+\d+','' -replace '\s+\+csol_weapons\s+\d+',''
            $lines[$i]=$line -replace '-game cstrike','-game cstrike -csol_game_mode 2 +csol_weapons 1'
        }
    }
    if($hits -ne 1){throw 'Run-Server.ps1 has an unknown startup layout; no files were changed.'}
    $changes['scripts/Run-Server.ps1']=($lines -join "`r`n").TrimEnd()+"`r`n"
    # Native scenario has no verified Minecraft dimension. Route before the
    # legacy dual-server validations, retaining that entire branch verbatim.
    $start=Text (Under $Root 'scripts/Start-Servers.ps1')
    $start=[regex]::Replace($start,'(?ms)^# BEGIN BINGSANG SCENARIO\r?\n.*?^# END BINGSANG SCENARIO\r?\n','')
    $marker=[regex]::new('(?m)^\. \(Join-Path \$PSScriptRoot ''Common\.ps1''\)(?:\r?\n|$)')
    if($marker.Matches($start).Count -ne 1){throw 'Unknown Start-Servers.ps1 layout; no files changed.'}
    $branch=@'
# BEGIN BINGSANG SCENARIO
$scenarioSettings = Read-Json (Join-Path $PackageRoot 'server-settings.json')
if ($scenarioSettings.PSObject.Properties['csolGameMode'] -and [int]$scenarioSettings.csolGameMode -eq 2) {
    & (Join-Path $PSScriptRoot 'Start-Scenario.ps1') -Hidden:$Hidden -Diagnostics:$Diagnostics -CheckOnly:$CheckOnly
    return
}
# END BINGSANG SCENARIO
'@
    $m=$marker.Match($start);$pos=$m.Index+$m.Length
    $changes['scripts/Start-Servers.ps1']=$start.Substring(0,$pos)+$branch.Replace("`r`n","`n").Replace("`n","`r`n")+"`r`n"+$start.Substring($pos)
    foreach($name in @('cs/cstrike/game_init.cfg','cs/cstrike/server.cfg')){
        $text=Text (Under $Root $name)
        $text=Set-Cvar $text 'csol_game_mode' '2';$text=Set-Cvar $text 'csol_weapons' '1'
        $text=Set-Cvar $text 'mapcyclefile' '"bingsang_scenario_cycle.txt"'
        $changes[$name]=$text
    }
    $bot='cs/cstrike/addons/sypb/sypb.cfg';$text=Text (Under $Root $bot)
    foreach($pair in @(@('sypb_auto_players','-1'),@('sypb_quota_save','-1'),@('sypb_quota','0'))){$text=Set-Cvar $text $pair[0] $pair[1]}
    $changes[$bot]=$text
    $configs='cs/cstrike/addons/amxmodx/configs';$rootConfig=Under $Root $configs
    $names=@('plugins.ini')+@(Get-ChildItem -LiteralPath $rootConfig -File -Filter 'plugins-*.ini'|ForEach-Object {$_.Name})
    foreach($name in $names){
        $rel=$configs+'/'+$name;$text=Text (Under $Root $rel)
        $kept=@($text -split '\r?\n'|Where-Object {$_ -notmatch '^\s*(goldcraft_game_mode|goldcraft_scenario_admin)\.amxx(?=\s|;|$)'})
        $next=($kept -join "`r`n").TrimEnd()+"`r`n"
        if($name -eq 'plugins.ini'){$next="goldcraft_game_mode.amxx`r`ngoldcraft_scenario_admin.amxx`r`n"+$next}
        if($text -cne $next){$changes[$rel]=$next}
    }
    $policy=$configs+'/goldcraft/game_modes.ini';$text=Text (Under $Root $policy)
    $required=@('zp50_* 2','goldcraft_zp* 2','goldcraft_zombie_* 2','zombieplague* 2','nademodes.amxx 3','goldcraft_scenario_admin.amxx 4')
    $patterns=@($required|ForEach-Object {($_ -split ' ')[0]})
    $kept=@($text -split '\r?\n'|Where-Object {(($_.Trim() -split '\s+')[0]) -notin $patterns})
    $changes[$policy]=($required -join "`r`n")+"`r`n"+($kept -join "`r`n").TrimEnd()+"`r`n"
    $modules=$configs+'/modules.ini';$text=Text (Under $Root $modules)
    if(!@($text -split '\r?\n'|Where-Object {$_ -match '^\s*goldcraft(?:_amxx(?:\.dll)?)?\s*(;.*)?$'}).Count){$changes[$modules]=$text.TrimEnd()+"`r`ngoldcraft`r`n"}
    return $changes
}
