# Windows PowerShell 5.1. All writes use the parent updater's journal and Atomic.
function Get-OperatorPolicy([string]$Name) {
    switch -CaseSensitive ($Name) {
        'cs/cstrike/configs/guest/cof/scenario_damage.json' {return 'firearms'}
        'cs/cstrike/configs/guest/cof/arsenal_damage.json' {return 'arsenal'}
        'cs/cstrike/addons/goldcraft/config/gameplay.json' {return 'createOnly'}
        'scripts/Start-Scenario.ps1' {return 'createOnly'}
    }
    if ($Name -cmatch '^cs/cstrike/csol/weapons/(balrog11|beamgun|cannon|dragonsword|k3|m249ex|skull1|skull2|skull3|skull5|skullaxe)_damage\.json$') {return 'createOnly'}
    return $null
}
function Read-WeaponSettings([string]$Path) {
    $data=[IO.File]::ReadAllBytes((Plain $Path))
    if ($data.Length -gt 65536 -or $data.Length -eq 0) {throw 'Weapon settings must be 1 to 65536 bytes'}
    # ConvertFrom-Json folds some duplicate keys. Reject ambiguous input before migrating it.
    Add-Type -AssemblyName System.Runtime.Serialization
    $reader=[Runtime.Serialization.Json.JsonReaderWriterFactory]::CreateJsonReader($data,[Xml.XmlDictionaryReaderQuotas]::Max)
    try {
        $doc=New-Object Xml.XmlDocument
        $doc.Load($reader)
        foreach ($obj in $doc.SelectNodes('//*[@type="object"]')) {
            $keys=@{}
            foreach ($child in $obj.ChildNodes) {
                $key=$child.LocalName
                if ($child.NamespaceURI -eq 'item') {$key=$child.GetAttribute('item')}
                if ($keys.ContainsKey($key)) {throw "Duplicate setting '$key'"}
                $keys[$key]=$true
            }
        }
    } finally {$reader.Dispose()}
    $value=(Text $Path | ConvertFrom-Json)
    if ($value -isnot [pscustomobject]) {throw 'Expected a weapon settings object'}
    return $value
}
function Test-WeaponNumber($Value,[int]$Min,[int]$Max,[bool]$Integer) {
    if ($Value -is [int] -or $Value -is [long]) {return $Value -ge $Min -and $Value -le $Max}
    if ($Integer -or ($Value -isnot [double] -and $Value -isnot [decimal])) {return $false}
    return ($Value -ge $Min -and $Value -le $Max)
}
function Merge-FirearmSettings($Current,$Defaults) {
    $version=$Current.version
    if (!(Test-WeaponNumber $version 1 7 $true) -or @($Current.PSObject.Properties).Count -ne 2) {throw 'Unrecognized CoF firearm settings version or envelope'}
    $group=if ($version -eq 1) {'pistols'} else {'firearms'}
    if ($Current.PSObject.Properties.Name -cnotcontains $group -or $Current.$group -isnot [pscustomobject]) {throw "Missing CoF '$group' group"}
    $names=@('glock','p345','revolver','vp70','tmp','mp5','m16','g43','shotgun','famas')
    $count=3+$version
    if (@($Current.$group.PSObject.Properties).Count -ne $count) {throw "Incomplete weapon list for CoF v$version"}
    for ($i=0; $i -lt $count; $i++) {
        $name=$names[$i]
        $v=$Current.$group.$name
        if ($Current.$group.PSObject.Properties.Name -cnotcontains $name -or $v -isnot [pscustomobject]) {throw "Missing CoF weapon '$name'"}
        $fields=@($v.PSObject.Properties.Name)
        if ($fields -cnotcontains 'shot' -or $fields -cnotcontains 'melee' -or !(Test-WeaponNumber $v.shot 0 1000000 $false) -or !(Test-WeaponNumber $v.melee 0 1000000 $false)) {throw "Invalid damage for '$name'"}
        if ($fields.Count -ne 2) {
            if ($fields.Count -ne 3 -or $fields -cnotcontains 'penetration' -or $name -cnotin @('revolver','m16','g43','famas') -or !(Test-WeaponNumber $v.penetration 0 32 $true)) {throw "Invalid penetration for '$name'"}
        }
        $Defaults.firearms.$name=$v
    }
    if ($version -lt 7) {return $Defaults}
}
function Merge-ArsenalSettings($Current,$Defaults) {
    $changed=$false
    foreach ($gun in $Defaults.PSObject.Properties) {
        if ($Current.PSObject.Properties.Name -cnotcontains $gun.Name) {
            $Current | Add-Member NoteProperty $gun.Name $gun.Value
            $changed=$true
            continue
        }
        $row=$Current.($gun.Name)
        if ($row -isnot [pscustomobject]) {throw "Invalid CoF arsenal entry '$($gun.Name)'"}
        foreach ($field in $gun.Value.PSObject.Properties) {
            $key=$field.Name
            if ($key -ceq 'wallLayers' -and $row.PSObject.Properties.Name -cnotcontains $key -and $row.PSObject.Properties.Name -ccontains 'penetration') {
                if (!(Test-WeaponNumber $row.penetration 3 32 $true)) {throw 'Invalid legacy rifle penetration'}
                continue
            }
            if ($row.PSObject.Properties.Name -cnotcontains $key) {
                $row | Add-Member NoteProperty $key $field.Value
                $changed=$true
            }
            $valid=if ($key -ceq 'wallLayers') {Test-WeaponNumber $row.$key 2 31 $true} else {Test-WeaponNumber $row.$key 0 1000000 $true}
            if (!$valid) {throw "Invalid $($gun.Name).$key"}
            if ($gun.Name -ceq 'nightstick' -and $key -ceq 'primary' -and $row.$key -eq 11 -and $field.Value -eq 200) {
                $row.$key=$field.Value
                $changed=$true
            }
        }
    }
    if ($changed) {return $Current}
}
function Get-OperatorChange($Row,[string]$Policy,[string]$Before) {
    if ($Before -ceq $Row.sha256 -or ($Before -and $Policy -ceq 'createOnly')) {return}
    $template=Under $package ('payload/'+$Row.path)
    if (!$Before -or $Before -cin $Row.priorSha256) {
        $bytes=[IO.File]::ReadAllBytes($template)
    } else {
        try {
            $current=Read-WeaponSettings (Under $target $Row.path)
            $defaults=Read-WeaponSettings $template
            $value=if ($Policy -ceq 'firearms') {Merge-FirearmSettings $current $defaults} else {Merge-ArsenalSettings $current $defaults}
            if ($null -eq $value) {return}
            $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($value | ConvertTo-Json -Depth 16)+[Environment]::NewLine)
        } catch {throw "Cannot migrate $($Row.path). Existing file preserved. $($_.Exception.Message)"}
    }
    $after=Hash-Bytes $bytes
    if ((!$Before -or $Before -cin $Row.priorSha256) -and $after -cne $Row.sha256) {throw "Configuration template changed: $($Row.path)"}
    if ($after -ceq $Before) {return}
    $script:configurationBytes[$Row.path]=$bytes
    [pscustomobject]@{path=$Row.path;existed=[bool]$Before;before=$(if ($Before) {$Before} else {$null});after=$after;kind='configuration'}
}
