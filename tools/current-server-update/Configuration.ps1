function Get-ConfigurationChanges([string]$Root,$Manifest) {
    $changes=Configure-Scenario $Root $Manifest
    $configRoot=Under $Root 'cs/cstrike/addons/amxmodx/configs'
    $names=@('plugins.ini')+@(Get-ChildItem -LiteralPath $configRoot -Filter 'plugins-*.ini' -File|ForEach-Object {$_.Name})
    $active=@{}
    foreach($name in $names){
        $rel='cs/cstrike/addons/amxmodx/configs/'+$name
        $text=if($changes.ContainsKey($rel)){$changes[$rel]}else{Text (Under $Root $rel)}
        $text=[regex]::Replace($text,'(?im)^(\s*)addon_floating_damage\.amxx[^\r\n]*','$1addon_floating_damage.amxx disabled')
        foreach($line in ($text -split '\r?\n')){
            if($line -match '^\s*([\w.-]+\.amxx)\b([^;]*)' -and $Matches[2] -notmatch '\bdisabled\b'){$active[$Matches[1]]=$true}
        }
        if($text -cne (Text (Under $Root $rel))){$changes[$rel]=$text}
    }
    $missing=@($Manifest.requiredPlugins|Where-Object {!$active.ContainsKey($_)})
    if($missing.Count){
        $rel='cs/cstrike/addons/amxmodx/configs/plugins-bingsang-current.ini'
        $text=if($changes.ContainsKey($rel)){$changes[$rel]}else{Text (Under $Root $rel)}
        $changes[$rel]=$text.TrimEnd()+"`r`n"+($missing -join "`r`n")+"`r`n"
    }
    return $changes
}
