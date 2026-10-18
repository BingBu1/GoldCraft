param([string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
if (-not $OutputDirectory) { $OutputDirectory = "$root/build/amxx/plugins" }
$output = [IO.Path]::GetFullPath($OutputDirectory)
$workspace = $root.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $output.StartsWith($workspace, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Build output must stay inside the workspace.'
}
. "$root/tools/SandboxPaths.ps1"
$output = Assert-WorkspacePath $output
New-Item -ItemType Directory -Force -Path $output | Out-Null
$compiler = "$root/.tools/amxx-1.10.0.5487/addons/amxmodx/scripting/amxxpc.exe"
& $compiler "$PSScriptRoot/addon_floating_damage.sma" `
    "-i$root/.tools/amxx-1.10.0.5487/addons/amxmodx/scripting/include/" `
    "-i$root/.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include/" `
    "-i$PSScriptRoot/" "-o$output/addon_floating_damage.amxx" '-d2' '-E'
if ($LASTEXITCODE) { throw "Floating Damage compile failed ($LASTEXITCODE)." }
