# Uses the path/hash helpers in ../scenario-update/Files.ps1. Read-only: run
# before signing or creating any publication directory.
function Assert-LauncherStaticContract([string]$Bundle,[string]$Runtime,$Release) {
    $cfg=Text (Plain $Bundle)|ConvertFrom-Json
    $root=Plain $Runtime
    if($cfg.schema -ne 1 -or $cfg.bundleId -cne $Release.bundleId){throw 'Launcher bundle does not match the signed baseline'}
    $cores=@{}
    foreach($name in $cfg.corePaths){
        [void](Under $root $name)
        if($cores.ContainsKey($name)){throw 'Duplicate launcher core path'}
        $cores[$name]=$true
    }
    $released=@{}
    foreach($row in $Release.files){
        if(!$cores.ContainsKey($row.path) -or $released.ContainsKey($row.path)){throw 'Launcher core contract differs from the signed baseline'}
        $released[$row.path]=$true
    }
    if($cores.Count -ne $released.Count){throw 'Launcher core contract is incomplete'}
    $files=@{}
    foreach($row in $cfg.files){
        $file=Under $root $row.path
        if($files.ContainsKey($row.path) -or $row.bytes -le 0 -or $row.sha256 -cnotmatch '^[0-9a-f]{64}$'){throw 'Invalid launcher file contract'}
        $files[$row.path]=$row
        if($cores.ContainsKey($row.path)){continue}
        # Notices are installed from the source distribution, not the game tree.
        if($row.path.StartsWith('goldcraft-licenses/',[StringComparison]::Ordinal)){continue}
        if((Hash $file) -cne $row.sha256 -or (Get-Item -LiteralPath $file).Length -ne $row.bytes){
            throw "Static launcher resource changed: $($row.path). Build a matching launcher; a DLL-only update is incompatible."
        }
    }
    foreach($name in $cores.Keys){if(!$files.ContainsKey($name)){throw 'Launcher core is absent from its embedded files'}}
    # A newly added symbol catalog is also a static contract change, even when
    # every old catalog still has its original hash.
    $catalog=Under $root 'cstrike/metahook/gamedata'
    foreach($file in Get-ChildItem -LiteralPath $catalog -Recurse -File -Filter '*.json'){
        [void](Plain $file.FullName)
        $name=$file.FullName.Substring($root.TrimEnd('\').Length+1).Replace('\','/')
        if(!$files.ContainsKey($name)){throw "New launcher symbol catalog: $name. Build a matching launcher."}
    }
    foreach($entry in @(@('hw.dll',$cfg.engineSha256),@('cstrike/cl_dlls/client.dll',$cfg.clientSha256))){
        if((Hash (Under $root $entry[0])) -cne $entry[1]){throw 'Launcher base-game identity mismatch'}
    }
}
