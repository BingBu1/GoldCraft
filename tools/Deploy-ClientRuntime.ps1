param([switch]$ValidateOnly, [ValidateSet('neoforge','fabric')][string]$Loader='neoforge',
      [ValidateSet('cs-client-a','cs-client-b')][string[]]$Clients=@('cs-client-a','cs-client-b'))
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
# Loader remains accepted by existing Mod synchronization callers. A native
# runtime update preserves instance pairing, Minecraft selection and CS settings.
# Use Initialize-SandboxInstance only when preparing a new independent copy.
& (Join-Path $PSScriptRoot 'Deploy-OptimizedNative.ps1') -Clients $Clients -ClientOnly `
    -WithMetaHookRuntime -ValidateOnly:$ValidateOnly
