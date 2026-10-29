param(
    [Parameter(Mandatory)][string]$Output,
    [ValidateRange(1,2)][int]$MaxJobs=2,
    [string]$ResourcePlan='',
    [string]$ResourceRoot='',
    [string]$ServerGame='sandbox/cs-server/Half-Life',
    [string]$ClientGame='sandbox/cs-client-b/Half-Life',
    [string]$CharacterFont='',
    [string]$HttpUrl='http://127.0.0.1:27080/goldcraft/',
    [ValidateRange(1,65535)][int]$HttpPort=27080
)
. "$PSScriptRoot/SandboxPaths.ps1"
$buildPath=if([IO.Path]::IsPathRooted($Output)){$Output}else{Join-Path $script:GoldCraftRoot $Output}
$destination=Assert-WorkspacePath $buildPath
if(Test-Path -LiteralPath $destination){throw 'A coordinated candidate requires a new output directory; old PCH/SDK objects cannot be reused.'}
$serverPath=if([IO.Path]::IsPathRooted($ServerGame)){$ServerGame}else{Join-Path $script:GoldCraftRoot $ServerGame}
$clientPath=if([IO.Path]::IsPathRooted($ClientGame)){$ClientGame}else{Join-Path $script:GoldCraftRoot $ClientGame}
$server=Assert-SandboxPath $serverPath
$client=Assert-SandboxPath $clientPath
$python=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/hlam-venv/Scripts/python.exe')
$helper=Join-Path $PSScriptRoot 'Build-CSOLCandidate.py'
& $python -B $helper begin --output $destination
if($LASTEXITCODE){throw 'Candidate source identity failed'}

function Invoke-CandidateStep([string]$Name,[scriptblock]$Action) {
    Write-Output "Candidate: $Name"
    & $Action *>&1 | Tee-Object -FilePath (Join-Path $destination "$Name.log")
}
Invoke-CandidateStep 'engine' {
    & "$PSScriptRoot/Build-ReHLDS.ps1" -Output "$destination/engine" -Fresh -NoPublish -MaxJobs $MaxJobs
}
Invoke-CandidateStep 'server' {
    & "$PSScriptRoot/Build-CSOLWeapons.ps1" -Output "$destination/server" -Fresh -BuildAmxxModule -MaxJobs $MaxJobs
}
Invoke-CandidateStep 'client' {
    & "$PSScriptRoot/Build-CSOLSupport.ps1" -Output "$destination/client" -Fresh -LibraryOnly -MaxJobs $MaxJobs -CharacterFont $CharacterFont
}
Invoke-CandidateStep 'amxx' {
    # The pinned builder launches at most one process per selected plugin.
    # Exactly these two production plugins keep this phase within two CPUs.
    & "$PSScriptRoot/Build-AMXX.ps1" -Plugins @('goldcraft_game_mode','goldcraft_scenario_admin') -Output "$destination/amxx"
}
Invoke-CandidateStep 'engine-catalog' {
    & $python -B "$PSScriptRoot/Build-PrecacheGameData.py" --engine "$client/hw.dll" --output "$destination/gamedata/goldcraft-precache"
    if($LASTEXITCODE){throw 'Engine catalog generation failed'}
}
Invoke-CandidateStep 'client-catalog' {
    & $python -B "$PSScriptRoot/Build-ClientGameData.py" --client "$client/cstrike/cl_dlls/client.dll" `
        --existing-catalog "$client/cstrike/metahook/gamedata" --output "$destination/gamedata/goldcraft-cs"
    if($LASTEXITCODE){throw 'Client catalog generation failed'}
}
& $python -B $helper seal --output $destination
if($LASTEXITCODE){throw 'Candidate source drift or artifact verification failed'}
if($ResourcePlan){
    $resourceArguments=@()
    if($ResourceRoot){$resourceArguments+=@('--resource-root',$ResourceRoot)}
    & $python -B $helper plan --output $destination --resources $ResourcePlan --server $server --client $client --http-url $HttpUrl --http-port $HttpPort @resourceArguments
    if($LASTEXITCODE){throw 'Candidate deployment planning failed'}
}
Write-Output "Candidate built at $destination. No runtime was replaced or started."
