# Run against an extracted CoF package from this workspace.
param([Parameter(Mandatory=$true)][string]$Package)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$package = (Get-Item -LiteralPath $Package).FullName
. (Join-Path $package 'Files.ps1')
. (Join-Path $package 'OperatorSettings.ps1')

$manifest = Text (Join-Path $package 'update-manifest.json') | ConvertFrom-Json
$work = Under $root ('build/operator-settings-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($work)
$utf8 = [Text.UTF8Encoding]::new($false)
$guns = 'cs/cstrike/configs/guest/cof/scenario_damage.json'
$arsenal = 'cs/cstrike/configs/guest/cof/arsenal_damage.json'
$game = 'cs/cstrike/addons/goldcraft/config/gameplay.json'

function Assert-Case([bool]$Passed, [string]$Case) {
    if (!$Passed) { throw "Failed $Case" }
    Write-Host $Case
}
function Trial([string]$Name, [byte[]]$Bytes) {
    $script:configurationBytes = @{}
    $script:target = Under $work ([Guid]::NewGuid().ToString('N'))
    $path = Under $target $Name
    if ($null -ne $Bytes) {
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
        [IO.File]::WriteAllBytes($path, $Bytes)
    }
    $row = $manifest.files | Where-Object { $_.path -ceq $Name }
    return Get-OperatorChange $row (Get-OperatorPolicy $Name) (Hash $path)
}

try {
    foreach ($name in @($game, $guns, $arsenal)) {
        $row = Trial $name $null
        $expected = Hash (Under $package ('payload/' + $name))
        Assert-Case ($row -and !$row.existed -and $row.after -ceq $expected) "Initialize missing $name"
    }
    $legacy = '{"version":1,"pistols":{"glock":{"shot":17.5,"melee":42},"p345":{"shot":0,"melee":0},"revolver":{"shot":234,"melee":100,"penetration":9},"vp70":{"shot":31,"melee":25}}}'
    $row = Trial $guns ($utf8.GetBytes($legacy))
    $bytes = $configurationBytes[$guns]
    $migration = $utf8.GetString($bytes) | ConvertFrom-Json
    Assert-Case ($migration.version -eq 7 -and $migration.firearms.glock.shot -eq 17.5 -and
        $migration.firearms.revolver.penetration -eq 9 -and $migration.firearms.famas.shot -eq 80) 'Migrate custom v1 and add FAMAS'
    Assert-Case ($null -eq (Trial $guns $bytes)) 'Preserve migrated bytes on reinstallation'

    $row = $manifest.files | Where-Object { $_.path -ceq $guns }
    $stock = (Text (Under $root 'native/guest/cof/scenario_damage.v6.json')).Replace("`r`n", "`n")
    $bytes = $utf8.GetBytes($stock)
    if ((Hash-Bytes $bytes) -cnotin $row.priorSha256) {
        $bytes = $utf8.GetBytes($stock.Replace("`n", "`r`n"))
    }
    Assert-Case ((Trial $guns $bytes).after -ceq $row.sha256) 'Upgrade recognized v6 factory table'

    $bad = $utf8.GetBytes('{"version":6,"version":7,"firearms":{}}')
    $rejected = $false
    try { $null = Trial $guns $bad } catch { $rejected = $_.Exception.Message -like '*Duplicate*' }
    Assert-Case ($rejected -and (Hash (Under $target $guns)) -ceq (Hash-Bytes $bad)) 'Reject duplicate keys and preserve the file'
    Assert-Case ($null -eq (Trial $game ($utf8.GetBytes('{  }')))) 'Preserve operator gameplay bytes'

    $row = Trial $arsenal ($utf8.GetBytes('{"rifle":{"primary":123,"secondary":7,"penetration":11}}'))
    $result = $utf8.GetString($configurationBytes[$arsenal]) | ConvertFrom-Json
    Assert-Case ($result.rifle.penetration -eq 11 -and $result.rifle.PSObject.Properties.Name -cnotcontains 'wallLayers' -and
        $result.rifle.primary -eq 123 -and $result.axe.primary -eq 350) 'Keep custom penetration while adding missing arsenal entries'
    $oldArsenal = '{"rifle":{"primary":123,"secondary":7,"penetration":11},"nightstick":{"primary":11}}'
    $row = Trial $arsenal ($utf8.GetBytes($oldArsenal))
    $bytes = $configurationBytes[$arsenal]
    $result = $utf8.GetString($bytes) | ConvertFrom-Json
    Assert-Case ($result.nightstick.primary -eq 200 -and $result.rifle.primary -eq 123 -and
        $result.rifle.penetration -eq 11) 'Upgrade the former Nightstick default in a custom table'
    Assert-Case ($null -eq (Trial $arsenal $bytes)) 'Keep the migrated arsenal unchanged on reinstallation'

    $result.nightstick.primary = 177
    $custom = $result | ConvertTo-Json -Depth 16
    Assert-Case (($custom | ConvertFrom-Json).nightstick.primary -eq 177 -and
        $null -eq (Trial $arsenal ($utf8.GetBytes($custom)))) 'Keep a deliberately customized Nightstick damage'
} finally {
    if ((Plain $work) -cne $work -or [IO.Path]::GetDirectoryName($work) -cne (Under $root 'build')) {
        throw 'Unexpected test directory'
    }
    Remove-Item -LiteralPath $work -Recurse -Force
}
