. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')

function Remove-OldSandboxRunLogs {
    param(
        [Parameter(Mandatory)][string]$LogDirectory,
        [Parameter(Mandatory)][ValidateSet('CsClient','CsServer','MinecraftClient','MinecraftServer','MinecraftGameTest')][string]$Role,
        [switch]$ReserveRun
    )
    $directory = Assert-SandboxPath $LogDirectory
    if (-not (Test-Path -LiteralPath $directory)) { return 0 }
    $policy = Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sandbox-policy.json') -Raw | ConvertFrom-Json
    $keep = [int]$policy.keepRunsPerRole
    if ($keep -lt 1 -or $keep -gt 100) { throw 'keepRunsPerRole must be between 1 and 100' }
    # The caller has checked this role is stopped. Reserve one slot for its new
    # stdout/stderr pair; launcher logs already exist when this function runs.
    $keepRun = $keep - [int]$ReserveRun.IsPresent
    $groups = @(
        @{Pattern="^$Role-\d{8}-\d{6}\.stdout\.log$"; Keep=$keepRun},
        @{Pattern="^$Role-\d{8}-\d{6}\.stderr\.log$"; Keep=$keepRun},
        @{Pattern="^launch-$Role-\d{8}-\d{6}-\d{3}\.log$"; Keep=$keep},
        @{Pattern="^launch-$Role-\d{8}-\d{6}-\d{3}\.error\.log$"; Keep=$keep}
    )
    $files = @(Get-ChildItem -LiteralPath $directory -File)
    $removed = 0
    foreach ($group in $groups) {
        $old = @($files | Where-Object { $_.Name -match $group.Pattern } |
            Sort-Object LastWriteTimeUtc -Descending | Select-Object -Skip $group.Keep)
        foreach ($file in $old) {
            $target = Assert-SandboxPath $file.FullName
            Remove-Item -LiteralPath $target
            $removed++
        }
    }
    return $removed
}
