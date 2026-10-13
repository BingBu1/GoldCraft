param([Parameter(Mandatory)][ValidateSet('cs','minecraft')][string]$Role,
      [Parameter(Mandatory)][string]$Command)
. (Join-Path $PSScriptRoot 'Common.ps1')
Send-RoleCommand $Role $Command
