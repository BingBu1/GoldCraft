param([Parameter(Mandatory)][ValidateSet('cs','minecraft')][string]$Role, [switch]$Hidden, [switch]$Diagnostics)
. (Join-Path $PSScriptRoot 'Common.ps1')
$settings = Read-Json (Join-Path $PackageRoot 'server-settings.json')
$secrets = Read-Json (Join-Path $State 'secrets.json')
# One supervisor per role/package, independent of PID reuse or working directory.
$lock = [IO.File]::Open((Join-Path $State "$Role.lock"), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    $env:GOLDCRAFT_SERVER_PORT = [string]$settings.bridgePort
    $env:GOLDCRAFT_SERVER_SESSION = $secrets.session
    $env:GOLDCRAFT_SERVER_TOKEN = $secrets.token
    $env:GOLDCRAFT_REQUIRE_CS_PAIR = '1'
    $env:GOLDCRAFT_SERVER_STATUS = Join-Path $State 'goldcraft-server-status.json'
    # No test-command reader, RCON listener, trace dumps or unbounded bridge log.
    foreach ($key in @('GOLDCRAFT_SERVER_LOG','GOLDCRAFT_TRACE_REPORT_DIR','GOLDCRAFT_TEST_COMMAND',
                       'GOLDCRAFT_PERFORMANCE_LOG','MOD_CLASSES','CLASSPATH','JAVA_TOOL_OPTIONS','_JAVA_OPTIONS','JDK_JAVA_OPTIONS')) {
        [Environment]::SetEnvironmentVariable($key, $null, 'Process')
    }
    if ($Diagnostics) { $env:GOLDCRAFT_SERVER_LOG = Join-Path $State 'native-bridge.log' }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $false
    if ($Hidden) { $info.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden }
    if ($Role -eq 'cs') {
        $info.WorkingDirectory = Join-Path $PackageRoot 'cs'
        $info.FileName = Join-Path $info.WorkingDirectory 'hlds.exe'
        $info.Arguments = "-console -game cstrike -maxplayers 32 -port $($settings.csPort) -sport $($settings.steamPort) +ip $($settings.bindAddress) +sv_lan 0 +map $($settings.map) +exec goldcraft-runtime.cfg"
        if (-not $settings.publicListing) { $info.Arguments = '-nomaster ' + $info.Arguments }
        if ($Diagnostics) { $info.Arguments = '-condebug ' + $info.Arguments }
    } else {
        $info.WorkingDirectory = Join-Path $PackageRoot 'minecraft'
        $info.FileName = Join-Path $PackageRoot 'runtime/java/bin/java.exe'
        $info.Arguments = "-Xms1G -Xmx$($settings.minecraftMemoryGB)G -Dfile.encoding=UTF-8 @launch.args nogui"
    }
    $Host.UI.RawUI.WindowTitle = "GoldCraft $Role console"
    if ($Role -eq 'cs') {
        . (Join-Path $PSScriptRoot 'NativeConsole.ps1')
        $proc = [GoldCraftNativeConsole]::Start($info.FileName, $info.Arguments, $info.WorkingDirectory, $Hidden.IsPresent)
    } else { $proc = [Diagnostics.Process]::Start($info) }
    $runId = Random-Hex 12
    Write-Json (Join-Path $State "$Role.json") @{
        pid=$proc.Id; startTicks=$proc.StartTime.ToUniversalTime().Ticks.ToString(); runId=$runId; role=$Role
    }
    $commandPath = Join-Path $State "$runId.command"
    Write-Host "$Role started. Type commands here; use Stop.cmd for a graceful dual-server shutdown."
    while (-not $proc.WaitForExit(100)) {
        if (Test-Path -LiteralPath $commandPath) {
            $sender = New-Object Diagnostics.ProcessStartInfo
            $sender.FileName = Join-Path $PSHOME 'powershell.exe'
            if (-not (Test-Path -LiteralPath $sender.FileName)) { $sender.FileName = Join-Path $PSHOME 'pwsh.exe' }
            $sender.UseShellExecute = $false
            $sender.CreateNoWindow = $true
            $sender.Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$(Join-Path $PSScriptRoot 'Send-Console.ps1')`" -ProcessId $($proc.Id) -StartTicks $($proc.StartTime.ToUniversalTime().Ticks) -CommandFile `"$commandPath`""
            $sending = [Diagnostics.Process]::Start($sender)
            if (-not $sending.WaitForExit(10000)) {
                Write-Text "$commandPath.error" 'Console sender timed out.'
                $sending.Kill()
                if (-not $sending.WaitForExit(2000)) { throw 'Console sender did not exit after termination.' }
            } elseif ($sending.ExitCode -ne 0 -and -not (Test-Path -LiteralPath "$commandPath.error")) {
                Write-Text "$commandPath.error" "Console sender failed: $($sending.ExitCode)"
            }
            $sending.Dispose()
            Remove-Item -LiteralPath $commandPath
        }
    }
    Write-Host "$Role exited with code $($proc.ExitCode)."
    Write-Json (Join-Path $State "$Role-exit.json") @{exitCode=$proc.ExitCode; runId=$runId}
} finally {
    if ($proc) { $proc.Dispose() }
    if ($Role -eq 'cs' -and ('GoldCraftNativeConsole' -as [type])) { [GoldCraftNativeConsole]::Release() }
    $lock.Dispose()
}
