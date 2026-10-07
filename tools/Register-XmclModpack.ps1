param([switch]$DryRun,[ValidateSet('neoforge','fabric')][string]$Loader='neoforge')
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$packFolder=if($Loader -eq 'neoforge'){'modpack-neoforge'}else{'modpack'}
$mcPin=(Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json).minecraft
$profile=if($Loader -eq 'neoforge'){"GoldCraft-$($mcPin.version)-NeoForge"}else{'GoldCraft-1.21'}
$source=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$packFolder/$profile")
if(-not(Test-Path -LiteralPath (Join-Path $source 'instance.json'))){throw 'Run Prepare-XmclModpack.ps1 first.'}

# XMCL 0.71.0 InstanceService reads this launcher-level registry on startup.
# This is the sole permitted write outside the workspace: append the requested
# external instance, preserving selection, groups, accounts and game data roots.
$registry=[IO.Path]::GetFullPath((Join-Path $env:APPDATA 'xmcl/instances.json'))
Assert-NoReparsePath $registry
if(-not(Test-Path -LiteralPath $registry)){throw 'The existing standard-EXE XMCL registry was not found; no new launcher registry was invented.'}
$original=[IO.File]::ReadAllBytes($registry)
$config=[Text.Encoding]::UTF8.GetString($original).TrimStart([char]0xFEFF) | ConvertFrom-Json
if(-not $config.PSObject.Properties['instances'] -or $config.instances -is [string]){throw 'Unrecognized XMCL instance registry; left unchanged.'}
if(@($config.instances | Where-Object { $_ -eq $source }).Count){Write-Output "Already registered: $source";return}
if($DryRun){Write-Output "Will append the external instance $source; preserve all $(@($config.instances).Count) existing registry entries and current selection.";return}
$launcher=Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'X Minecraft Launcher.exe' }
if($launcher){throw 'Please exit X Minecraft Launcher first, then run Add-to-XMCL.cmd. XMCL saves this registry itself while running; concurrent changes could be lost.'}
$backup=Assert-SandboxPath (Join-Path $script:GoldCraftRoot ("sandbox/$packFolder/xmcl-registry-backups/"+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff')+'.json'))
New-Item -ItemType Directory -Path (Split-Path $backup) -Force | Out-Null
[IO.File]::WriteAllBytes($backup,$original)
$config.instances=@($config.instances)+@($source)
$next=[Text.UTF8Encoding]::new($false).GetBytes(($config | ConvertTo-Json -Depth 40))
$beforeHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($original))
if((Get-FileHash -LiteralPath $registry -Algorithm SHA256).Hash -ne $beforeHash){throw 'XMCL registry changed during preparation; the existing registry was left untouched.'}
$temporary=Join-Path (Split-Path $registry) ('instances.goldcraft-'+[Guid]::NewGuid().ToString('N')+'.tmp')
Assert-NoReparsePath $temporary
try {
    [IO.File]::WriteAllBytes($temporary,$next)
    [IO.File]::Move($temporary,$registry,$true)
} finally {
    if(Test-Path -LiteralPath $temporary){Assert-NoReparsePath $temporary;[IO.File]::Delete($temporary)}
}
$actual=Get-Content -LiteralPath $registry -Raw | ConvertFrom-Json
if(-not(@($actual.instances | Where-Object {$_ -eq $source}).Count)){throw 'XMCL registration verification failed; the previous registry is preserved in the sandbox backup.'}
Write-Output "Registered $source. Reopen X Minecraft Launcher and select $profile · Mod 管理."
Write-Output "Previous launcher registry backed up at $backup"
