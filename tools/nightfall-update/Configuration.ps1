# Merge the single gameplay document; keep the operator's numeric values.
function Add-NightfallConfiguration([string]$Root,$Changes) {
    $relative='cs/cstrike/addons/goldcraft/config/gameplay.json'
    $defaults=Text (Under $PSScriptRoot 'gameplay-defaults.json')|ConvertFrom-Json
    $path=Under $Root $relative
    if(Test-Path -LiteralPath $path){
        $settings=Text $path|ConvertFrom-Json
        if($settings.version -ne 2 -or !$settings.features){throw 'Unsupported existing gameplay.json; preserved without changes'}
        foreach($feature in $defaults.features.PSObject.Properties){
            $old=$settings.features.PSObject.Properties[$feature.Name]
            if(!$old){$settings.features|Add-Member -NotePropertyName $feature.Name -NotePropertyValue $feature.Value;continue}
            if(!$old.Value.PSObject.Properties['enabled'] -or $old.Value.enabled -isnot [bool] -or !$old.Value.PSObject.Properties['values']){throw 'Malformed gameplay feature; preserved'}
            foreach($value in $feature.Value.values.PSObject.Properties){
                if(!$old.Value.values.PSObject.Properties[$value.Name]){$old.Value.values|Add-Member -NotePropertyName $value.Name -NotePropertyValue $value.Value}
            }
        }
        $settings.features.time_cycle.enabled=$true
    }else{$settings=$defaults}
    $Changes[$relative]=($settings|ConvertTo-Json -Depth 30)+"`r`n"
}
