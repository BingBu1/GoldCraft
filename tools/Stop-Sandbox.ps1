param(
    [Parameter(Mandatory)][ValidateSet('CsClient','CsServer','MinecraftClient','MinecraftServer')][string]$Role,
    [ValidatePattern('^[a-z0-9][a-z0-9-]{0,40}$')][string]$Instance='cs-client-a'
)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$recordPath=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/process-$Role.json")
if(-not(Test-Path -LiteralPath $recordPath)){throw 'No recorded sandbox process for this role'}
$record=Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json -DateKind String
$sandboxProcess=Get-CimInstance Win32_Process -Filter "ProcessId=$($record.pid)"
if(-not $sandboxProcess){Write-Output "$Instance $Role has already stopped";return}
$instanceGame=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/Half-Life")
$expectedExecutable=if($Role -in @('CsClient','CsServer')) {
    Join-Path $instanceGame $(if($Role -eq 'CsClient'){'MetaHook.exe'}else{'hlds.exe'})
}else{Assert-WorkspacePath (Join-Path $script:GoldCraftRoot '.tools/java/jdk-21.0.12.1+1/bin/java.exe')}
if($sandboxProcess.ExecutablePath -ne $expectedExecutable -or $record.executable -ne $expectedExecutable -or
   [Math]::Abs(($sandboxProcess.CreationDate.ToUniversalTime()-[DateTime]::Parse($record.startedUtc).ToUniversalTime()).TotalSeconds) -gt 1) {
    throw 'Refusing to stop a process whose executable or start time differs from the sandbox record'
}
if($Role -eq 'MinecraftServer') {
    & python (Join-Path $PSScriptRoot 'Minecraft-Command.py') 'save-all flush'
    if($LASTEXITCODE){throw 'Minecraft save failed; server was left running'}
    & python (Join-Path $PSScriptRoot 'Minecraft-Command.py') 'stop'
    $watched=[Diagnostics.Process]::GetProcessById($sandboxProcess.ProcessId)
    if(-not $watched.WaitForExit(45000)){throw 'Server is still shutting down; it was not force-terminated'}
}else{
    $watched=[Diagnostics.Process]::GetProcessById($sandboxProcess.ProcessId)
    Stop-Process -Id $sandboxProcess.ProcessId
    if(-not $watched.WaitForExit(10000)){throw 'Sandbox process has not finished exiting; do not replace its loaded binaries yet'}
}
Write-Output "Stopped $Instance $Role PID $($sandboxProcess.ProcessId)"
