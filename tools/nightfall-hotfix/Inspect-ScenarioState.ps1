param([string]$TargetDirectory='E:\cs_MineCraft\GoldCraft-Servers', [int]$ProcessId=0)
$ErrorActionPreference='Stop'
# Matching MP60ba PDB layouts. No function is called inside the server.
# Keep this next to Inspect-Runtime.ps1; it supplies the read-only API.
$null=. (Join-Path $PSScriptRoot 'Inspect-Runtime.ps1') -TargetDirectory $TargetDirectory -ProcessId $ProcessId
function Read32([int]$Id,[long]$Address){U32 ([GoldCraftReadOnlyImage]::Read($Id,$Address,4)) 0}
function HexBytes([byte[]]$Bytes){([BitConverter]::ToString($Bytes)).Replace('-','').ToLowerInvariant()}
$result=[ordered]@{readOnly=$true;boundary='Non-atomic external snapshots of exact MP60ba; no server functions, input or writes';processes=@()}
foreach($process in $targets){
    $row=[ordered]@{pid=$process.Id;error=$null;matches=$false}
    try {
        $modules=@([GoldCraftReadOnlyImage]::Modules($process.Id)|Where-Object {$_.Name -ieq 'mp.dll'})
        if($modules.Count -ne 1){throw 'Expected one game DLL'}
        $base=$modules[0].Base.ToInt64()
        $identity=Loaded-Identity $process.Id $base
        if($identity -cne $expected){throw 'State offsets are disabled for this different loaded game DLL'}
        $row.matches=$true
        $rules=Read32 $process.Id ($base+6383400)
        if(!$rules){throw 'No live game rules'}
        $imageSize=Read32 $process.Id ($base+(Read32 $process.Id ($base+60))+80)
        $vtable=Read32 $process.Id $rules
        if($vtable-lt $base -or $vtable-ge ($base+$imageSize)){throw 'Rules vtable is outside the matched game DLL'}
        $locator=Read32 $process.Id ($vtable-4)
        $descriptor=Read32 $process.Id ($locator+12)
        if($descriptor-lt $base -or $descriptor-ge ($base+$imageSize)){throw 'Rules type descriptor is outside the matched game DLL'}
        $name=[Text.Encoding]::ASCII.GetString([GoldCraftReadOnlyImage]::Read($process.Id,$descriptor+8,128)).Split([char]0)[0]
        $row.rulesType=$name
        if($name -notmatch 'CCSOLScenarioRules'){throw 'The running mode does not own scenario rules'}
        $runtime=if($rules){Read32 $process.Id ($rules+748)}else{0}
        $row.rules=$rules;$row.runtime=$runtime
        $engine=@([GoldCraftReadOnlyImage]::Modules($process.Id)|Where-Object {$_.Name -ieq 'swds.dll'})
        $clients=0;$maxClients=0
        if($engine.Count -eq 1){
            $engineBase=$engine[0].Base.ToInt64()
            if((Loaded-Identity $process.Id $engineBase)-ceq '5253445315468aed7b8daa464c4c44205044422e01000000'){
                $svs=$engineBase+7830664
                $clients=Read32 $process.Id ($svs+4);$maxClients=Read32 $process.Id ($svs+8)
                if($maxClients-gt 32){throw 'Invalid matched engine client bound'}
                $directory=[GoldCraftReadOnlyImage]::Read($process.Id,$engineBase+4505408,260)
                $row.gameDirectory=[Text.Encoding]::UTF8.GetString($directory).Split([char]0)[0]
            }
        }
        if($runtime){
            $bytes=[GoldCraftReadOnlyImage]::Read($process.Id,$runtime,156)
            $row.runtimeGeneration=[BitConverter]::ToUInt64($bytes,144)
            $row.runtimeClosing=[bool]$bytes[152]
        }
        # Verify the exact inlined guard load before reading its unnamed flag.
        $guard=[GoldCraftReadOnlyImage]::Read($process.Id,$base+0x380397,7)
        if($guard[0]-ne 128 -or $guard[1]-ne 61 -or (U32 $guard 2)-ne ($base+0x695978) -or $guard[6]-ne 0){throw 'Unexpected stamina query guard code'}
        $row.staminaOperating=[bool]([GoldCraftReadOnlyImage]::Read($process.Id,$base+0x695978,1))[0]
        $states=[GoldCraftReadOnlyImage]::Read($process.Id,$base+6891192,12936)
        $row.players=@()
        for($slot=1;$slot-le 32;$slot++){
            $offset=$slot*392;$actor=U32 $states ($offset+56)
            if(!$actor){continue}
            $state=[ordered]@{slot=$slot;actor=$actor;retired=[bool]$states[$offset+181];
                runtimeMatches=((U32 $states $offset)-eq $runtime);life=[BitConverter]::ToUInt64($states,$offset+104);
                nativeSpawns=[BitConverter]::ToInt32($states,$offset+128);
                amount=[BitConverter]::ToSingle($states,$offset+296);
                jumpCost=[BitConverter]::ToSingle($states,$offset+272);
                publishedAmount=[BitConverter]::ToSingle($states,$offset+164);
                phase=$states[$offset+324];
                currentKey=(HexBytes $states[($offset+184)..($offset+207)]);
                staminaKey=(HexBytes $states[($offset+208)..($offset+231)]);
                retiredKey=(HexBytes $states[($offset+232)..($offset+255)])}
            $nativeSpawns=[BitConverter]::ToInt32([GoldCraftReadOnlyImage]::Read($process.Id,$actor+1460,4),0)
            $pev=Read32 $process.Id ($actor+4)
            $variables=[GoldCraftReadOnlyImage]::Read($process.Id,$pev,424)
            $state.actualSpawns=$nativeSpawns
            $state.health=[BitConverter]::ToSingle($variables,352)
            $state.deadflag=[BitConverter]::ToInt32($variables,368)
            $session=Read32 $process.Id $runtime
            $players=Read32 $process.Id ($runtime+8)
            $access=U32 $states ($offset+24)
            $profile=U32 $states ($offset+32)
            if($session -and $players -and $access -and $profile){
                $profileData=[GoldCraftReadOnlyImage]::Read($process.Id,$profile,432)
                $playerData=[GoldCraftReadOnlyImage]::Read($process.Id,$players,616)
                $sessionData=[GoldCraftReadOnlyImage]::Read($process.Id,$session,5152)
                $edict=U32 $states ($offset+48)
                $edictData=[GoldCraftReadOnlyImage]::Read($process.Id,$edict,128)
                $state.ownerChecks=[ordered]@{
                    runtimeSessionMatches=((U32 $states ($offset+8))-eq $session);
                    runtimePlayersMatches=((U32 $states ($offset+16))-eq $players);
                    sessionState=[BitConverter]::ToInt32($sessionData,5148);
                    roundMatches=([BitConverter]::ToUInt64($states,$offset+72)-eq [BitConverter]::ToUInt64($sessionData,832));
                    mapAccessMatches=($access-eq (U32 $sessionData 1108));
                    accessAllowed=[bool]([GoldCraftReadOnlyImage]::Read($process.Id,$access,1))[0];
                    playersActive=[bool]$playerData[612];playersInvalidating=[bool]$playerData[614];
                    registeredProfileMatches=($profile-eq (U32 $playerData (24+$slot*8)));
                    profileGenerationMatches=([BitConverter]::ToUInt64($states,$offset+80)-eq [BitConverter]::ToUInt64($profileData,24));
                    profileLifeMatches=([BitConverter]::ToUInt64($states,$offset+88)-eq [BitConverter]::ToUInt64($profileData,424));
                    profileSpawnMatches=([BitConverter]::ToUInt64($states,$offset+96)-eq [BitConverter]::ToUInt64($profileData,56));
                    profileEdictMatches=($edict-eq (U32 $profileData 12));
                    edictSerialMatches=((U32 $states ($offset+52))-eq (U32 $edictData 4));
                    privateActorMatches=($actor-eq (U32 $edictData 124));
                    disconnected=[bool]([GoldCraftReadOnlyImage]::Read($process.Id,$actor+132,1))[0];
                    flags=(U32 $variables 420)}
            }
            if($clients -and $slot-le $maxClients){
                $physics=[GoldCraftReadOnlyImage]::Read($process.Id,$clients+($slot-1)*20640+20220,256)
                $info=[Text.Encoding]::ASCII.GetString($physics).Split([char]0)[0]
                $state.basePhysicsJumpDenied=($info -match '(?:^|\\)cso_jdeny\\1(?:\\|$)')
            }
            $row.players+=@($state)
        }
        $map=Read32 $process.Id ($base+6905168+16)
        $row.guestMap=$map
        if($map){
            $bytes=[GoldCraftReadOnlyImage]::Read($process.Id,$map,8824)
            $row.guest=[ordered]@{epoch=[BitConverter]::ToUInt64($bytes,8);rulesMatch=((U32 $bytes 16)-eq $rules);
                active=[bool]$bytes[8812];ready=[bool]$bytes[8816];runtimeIssued=[bool]$bytes[8813];runtimeReleased=[bool]$bytes[8814]}
        }
        $row.identityStable=((Loaded-Identity $process.Id $base)-ceq $identity)
    } catch {$row.error=$_.Exception.Message}
    $result.processes+=@($row)
}
$result|ConvertTo-Json -Depth 6
