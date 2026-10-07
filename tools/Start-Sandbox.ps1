param(
    [Parameter(Mandatory)][ValidateSet('CsClient','CsServer','MinecraftClient','MinecraftServer','MinecraftGameTest')][string]$Role,
    [ValidatePattern('^[a-z0-9][a-z0-9-]{0,40}$')][string]$Instance='cs-client-a',
    [ValidatePattern('^[A-Za-z0-9_]{1,64}$')][string]$Map='cs_assault',
    [ValidateRange(2,32)][int]$MaxPlayers=12,
    [ValidateSet('neoforge','fabric')][string]$Loader='neoforge',
    [switch]$ListenServer,
    [switch]$Capture,
    [switch]$ConsoleLog,
    [switch]$WithDebugger,
    [switch]$Wait
)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
. (Join-Path $PSScriptRoot 'SandboxLogs.ps1')
$env:GOLDCRAFT_MOD_LOADER=$Loader
$mcPin=(Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json).minecraft
$packName=if($Loader -eq 'neoforge'){"modpack-neoforge/GoldCraft-$($mcPin.version)-NeoForge"}else{'modpack/GoldCraft-1.21'}
$runningRecord=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/process-$Role.json")
if(Test-Path -LiteralPath $runningRecord){
    $existing=Get-Content -LiteralPath $runningRecord -Raw | ConvertFrom-Json
    $live=Get-Process -Id $existing.pid -ErrorAction SilentlyContinue
    if($live -and [Math]::Abs(($live.StartTime.ToUniversalTime()-[DateTime]::Parse($existing.startedUtc).ToUniversalTime()).TotalSeconds) -lt 1){
        throw "$Instance $Role is already running. Stop the managed instance before switching loaders or restarting."
    }
}
if($ListenServer){throw 'Use the ReHLDS dedicated server: -Role CsServer -Instance cs-server; clients join without -ListenServer.'}
if($Role -in @('MinecraftClient','MinecraftServer') -and (Test-Path -LiteralPath (Join-Path $script:GoldCraftRoot "sandbox/$packName/instance.json"))) {
    & python (Join-Path $PSScriptRoot 'Modpack.py') sync --quiet
    if($LASTEXITCODE){throw 'Modpack is not synchronized. Use tools/Sync-Modpack.ps1 -Restart after resolving the reported conflict.'}
}
$instanceRoot=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance")
$config=Get-Content -LiteralPath (Join-Path $instanceRoot 'instance.json') -Raw | ConvertFrom-Json
$cluster=Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sandbox/cluster.json') -Raw | ConvertFrom-Json
$game=Assert-SandboxPath $config.game
if($Role -eq 'CsServer') {
    $deploymentPath=Join-Path $instanceRoot 'rehlds-deployment.json'
    if(-not(Test-Path -LiteralPath $deploymentPath)){throw 'Deploy the ReHLDS dedicated engine with Initialize-ReHLDS.ps1 first.'}
    $deployment=Get-Content -LiteralPath $deploymentPath -Raw | ConvertFrom-Json
    foreach($entry in $deployment.files){
        if((Get-FileHash -LiteralPath (Join-Path $game $entry.name) -Algorithm SHA256).Hash -ne $entry.sha256){throw "ReHLDS deployment changed: $($entry.name)"}
    }
}
$logDir=Assert-SandboxPath (Join-Path $instanceRoot 'logs')
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$start=[Diagnostics.ProcessStartInfo]::new()
$start.UseShellExecute=$false
# ReHLDS CTextConsoleWin32 reads console input events. A pipe/NUL input can
# repeatedly abort startup; inherit the launcher's hidden PTY for this role.
if($Role -eq 'CsServer' -and [Console]::IsInputRedirected){throw 'Run CsServer in a console/PTY; ReHLDS requires a console input handle.'}
$start.CreateNoWindow=$Role -notin @('CsClient','CsServer')
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
if($Role -eq 'CsClient'){$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal}
$start.Environment['GOLDCRAFT_CLIENT_PORT']=[string]$config.clientPort
$start.Environment['METAHOOK_ERROR_LOG']=Join-Path $logDir 'metahook-errors.log'
$start.Environment['GOLDCRAFT_CLIENT_SESSION']=$config.clientSession
$start.Environment['GOLDCRAFT_CLIENT_TOKEN']=$config.clientToken
$start.Environment['GOLDCRAFT_PROFILE_ID']=$config.profileId
$mcDirectory=if($Loader -eq 'neoforge'){"sandbox/neoforge-$Instance"}else{"sandbox/minecraft-$Instance"}
$start.Environment['GOLDCRAFT_MC_RUN_DIR']=Assert-SandboxPath (Join-Path $script:GoldCraftRoot $mcDirectory)
$start.Environment['GOLDCRAFT_MC_USERNAME']=if($config.PSObject.Properties['playerName']){$config.playerName}
    elseif($Instance -eq 'cs-client-a'){'GoldCraft_A'}else{throw 'Initialize this sandbox instance to assign its own player name'}
$start.Environment['GOLDCRAFT_MC_CONNECT']="127.0.0.1:$($cluster.minecraftPort)"
$start.Environment['GOLDCRAFT_TEST_MAP']=$Map
$start.Environment['GOLDCRAFT_CLIENT_LOG']=Join-Path $logDir 'goldcraft-client.log'
$start.Environment['GOLDCRAFT_CLIENT_STATUS']=Join-Path $logDir 'goldcraft-client-status.json'
$start.Environment['GOLDCRAFT_TEST_COMMAND']=Join-Path $logDir 'goldcraft-test-command.txt'
$start.Environment['GOLDCRAFT_MOTION_CAPTURE']=Join-Path $logDir 'goldcraft-motion.csv'
$start.Environment['GOLDCRAFT_PERFORMANCE_LOG']=Join-Path $logDir "performance-$Role.json"
if($Role -eq 'CsClient'){'0 noop' | Set-Content -LiteralPath $start.Environment['GOLDCRAFT_TEST_COMMAND'] -Encoding ascii}
$start.Environment['GOLDCRAFT_SERVER_STATUS']=Join-Path $logDir 'goldcraft-server-status.json'
if($Capture){$start.Environment['GOLDCRAFT_CAPTURE_PATH']=Join-Path $logDir 'goldcraft-frame.bmp'}
else{$start.Environment.Remove('GOLDCRAFT_CAPTURE_PATH')|Out-Null}
$start.Environment['GOLDCRAFT_SERVER_LOG']=Join-Path $logDir 'goldcraft-server.log'
$start.Environment['GOLDCRAFT_TRACE_REPORT_DIR']=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot 'analysis/goldcraft-tests')
$start.Environment['GOLDCRAFT_BUILD_LOG']=Join-Path $logDir "gradle-$Role.log"
foreach($key in @('GOLDCRAFT_SERVER_PORT','GOLDCRAFT_SERVER_SESSION','GOLDCRAFT_SERVER_TOKEN')){$start.Environment.Remove($key) | Out-Null}
if($Role -in @('CsServer','MinecraftServer','MinecraftGameTest') -or $ListenServer){
    $start.Environment['GOLDCRAFT_SERVER_PORT']=[string]$cluster.serverPort
    $start.Environment['GOLDCRAFT_SERVER_SESSION']=$cluster.serverSession
    $start.Environment['GOLDCRAFT_SERVER_TOKEN']=$cluster.serverToken
}

if($Role -in @('CsClient','CsServer')){
    $start.WorkingDirectory=$game
    $start.FileName=Assert-SandboxPath (Join-Path $game $(if($Role -eq 'CsClient'){'MetaHook.exe'}else{'hlds.exe'}))
    # GoldSrc's -basedir overrides the base game folder (normally valve), not the install root.
    # The executable location and guarded working directory already select our isolated install.
    $hostPort=$cluster.csPort
    if($Role -eq 'CsClient' -and -not $ListenServer) {
        if(-not $config.PSObject.Properties['csHostPort'] -or -not $config.PSObject.Properties['csClientPort']) {
            throw 'Initialize this sandbox instance to assign independent GoldSrc UDP ports'
        }
        $hostPort=$config.csHostPort
    }
    # ReHLDS net_ws.cpp registers the ip cvar; it does not parse a -ip option.
    $arguments=@('-game','cstrike','-insecure','-nomaster','-console','+ip','127.0.0.1','-port',[string]$hostPort)
    # qconsole duplicates the normal diagnostics and can grow without limit.
    if($ConsoleLog -or $WithDebugger){$arguments+='-condebug'}
    if($Role -eq 'CsClient'){
        $arguments+=@('-nomutex','-gl','-windowed','-w','1280','-h','720')
        # GoldSrc has separate NS_SERVER and NS_CLIENT sockets, even in a joining client.
        # Set the client cvar before +connect; -port only sets the local server socket.
        if($config.PSObject.Properties['csClientPort']){$arguments+=@('+clientport',[string]$config.csClientPort)}
        $arguments+=@('+name',$start.Environment['GOLDCRAFT_MC_USERNAME'])
        if($ListenServer){$arguments+=@('+maxplayers','4','+map',$Map,'+exec','goldcraft_test.cfg','+jointeam','2','+joinclass','1')}
        # The sandbox server's mp_auto_join_team/humans_join_team choose team/model after signon.
        # Issuing +jointeam immediately after asynchronous +connect would run too early.
        else {$arguments+=@('+connect',"127.0.0.1:$($cluster.csPort)")}
    }else{$arguments+=@('-maxplayers',"$MaxPlayers",'+sv_lan','1','+map',$Map,'+exec','goldcraft_test.cfg')}
    if($WithDebugger){
        $executable=$start.FileName
        $start.FileName='C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
        $dump=(Join-Path $logDir 'client-exception.dmp').Replace('\','/')
        $debugCommands=Assert-SandboxPath (Join-Path $logDir 'debug-commands.txt')
        @"
sxe -c ".exr -1; .ecxr; kp 20; .dump /m /o $dump; q" av
g
"@ | Set-Content -LiteralPath $debugCommands -Encoding ascii
        foreach($arg in @('-sins','-y',(Join-Path $script:GoldCraftRoot 'build/native-x86/Release'),'-G','-logo',(Join-Path $logDir 'debugger.log'),'-cf',$debugCommands,$executable)){$start.ArgumentList.Add($arg)}
    }
    foreach($arg in $arguments){$start.ArgumentList.Add($arg)}
}else{
    $task=switch($Role){'MinecraftClient'{'runClient'} 'MinecraftServer'{'runServer'} default{'runGameTestServer'}}
    $production=$Loader -eq 'neoforge' -and $Role -ne 'MinecraftGameTest'
    $runtimeDirectory=if($production){'neoforge-production'}else{"$Loader-runtime"}
    $manifestPath=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot "build/$runtimeDirectory/$task.json")
    if(-not(Test-Path -LiteralPath $manifestPath)){throw "Prepare the $Loader runtime first; NeoForge managed clients/server require tools/Prepare-NeoForgeRuntime.py"}
    $manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if($production -and $manifest.runtimeKind -ne 'production'){throw 'Managed NeoForge instances require the production runtime for ordinary Mod compatibility'}
    if($manifest.PSObject.Properties['clearEnvironment']){
        foreach($key in $manifest.clearEnvironment){$start.Environment.Remove($key)|Out-Null}
    }
    if($manifest.PSObject.Properties['environment']){
        foreach($entry in $manifest.environment.PSObject.Properties){$start.Environment[$entry.Name]=[string]$entry.Value}
    }
    $start.WorkingDirectory=if($Role -eq 'MinecraftClient'){$start.Environment['GOLDCRAFT_MC_RUN_DIR']}else{Assert-SandboxPath $manifest.workdir}
    $start.FileName=Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1/bin/java.exe')
    $arguments=[Collections.Generic.List[string]]::new()
    foreach($arg in $manifest.jvm){$arguments.Add($arg)}
    if($production){
        $temporary=Assert-SandboxPath (Join-Path $start.WorkingDirectory 'tmp')
        $natives=Assert-SandboxPath (Join-Path $start.WorkingDirectory 'natives')
        foreach($directory in @($temporary,$natives)){New-Item -ItemType Directory -Path $directory -Force|Out-Null}
        $arguments.Add("-Djava.io.tmpdir=$temporary")
        foreach($key in @('java.library.path','jna.tmpdir','org.lwjgl.system.SharedLibraryExtractPath','io.netty.native.workdir')){
            $arguments.Add("-D$key=$natives")
        }
    }
    if($manifest.classpath.Count){$arguments.Add('-cp');$arguments.Add(($manifest.classpath -join ';'))}
    $arguments.Add($manifest.main)
    # Instance-sensitive program arguments are supplied here, never frozen in a shared Gradle manifest.
    for($i=0;$i -lt $manifest.args.Count;$i++){
        if($manifest.args[$i] -in @('--username','--quickPlayMultiplayer','--gameDir')){$i++;continue}
        $arguments.Add($manifest.args[$i])
    }
    if($Role -eq 'MinecraftClient'){
        $arguments.Add('--username');$arguments.Add($start.Environment['GOLDCRAFT_MC_USERNAME'])
        # NeoForge's Loom launch.cfg already supplies --gameDir .; jopt-simple
        # rejects a second value. Its working directory is the isolated instance.
        if($Loader -eq 'fabric' -or $production){$arguments.Add('--gameDir');$arguments.Add($start.WorkingDirectory)}
        if($production){
            # Stable identity for the existing loopback/offline development server.
            # Java UUID.nameUUIDFromBytes uses network-order MD5 bytes, unlike Guid(byte[]).
            $uuidBytes=[Security.Cryptography.MD5]::HashData([Text.Encoding]::UTF8.GetBytes('OfflinePlayer:'+$start.Environment['GOLDCRAFT_MC_USERNAME']))
            $uuidBytes[6]=($uuidBytes[6] -band 15) -bor 48
            $uuidBytes[8]=($uuidBytes[8] -band 63) -bor 128
            $uuidHex=[Convert]::ToHexString($uuidBytes).ToLowerInvariant()
            $arguments.Add('--uuid');$arguments.Add($uuidHex)
        }
        $arguments.Add('--quickPlayMultiplayer');$arguments.Add($start.Environment['GOLDCRAFT_MC_CONNECT'])
    }
    $argumentFile=Assert-SandboxPath (Join-Path $instanceRoot "$Role.args")
    $arguments | ForEach-Object {'"'+$_.Replace('\','\\').Replace('"','\"')+'"'} |
        Set-Content -LiteralPath $argumentFile -Encoding utf8NoBOM
    $start.ArgumentList.Add('@'+$argumentFile)
}
if($Wait){$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true}
$prunedLogs=Remove-OldSandboxRunLogs -LogDirectory $logDir -Role $Role -ReserveRun:$Wait
if($prunedLogs){Write-Host "Removed $prunedLogs old $Role log files; retaining the latest runs."}
$process=[Diagnostics.Process]::Start($start)
if($Wait){
    $stamp=[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss')
    $outFile=[IO.File]::Open((Assert-SandboxPath (Join-Path $logDir "$Role-$stamp.stdout.log")),[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $errFile=[IO.File]::Open((Assert-SandboxPath (Join-Path $logDir "$Role-$stamp.stderr.log")),[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $outCopy=$process.StandardOutput.BaseStream.CopyToAsync($outFile)
    $errCopy=$process.StandardError.BaseStream.CopyToAsync($errFile)
}
@{role=$Role;pid=$process.Id;startedUtc=$process.StartTime.ToUniversalTime().ToString('O');executable=$start.FileName;game=$game} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $instanceRoot "process-$Role.json") -Encoding utf8
Write-Host "$Role started as PID $($process.Id), isolated game path $game"
if($Wait){
    try{$process.WaitForExit();$outCopy.GetAwaiter().GetResult();$errCopy.GetAwaiter().GetResult()}
    finally{$outFile.Dispose();$errFile.Dispose()}
    Write-Host "$Role exited with code $($process.ExitCode)";exit $process.ExitCode
}
