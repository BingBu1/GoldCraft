Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:GoldCraftRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$settingsPath=Join-Path $script:GoldCraftRoot 'settings.local.json'
$script:GoldCraftOriginal=$env:GOLDCRAFT_ORIGINAL_GAME
if(-not $script:GoldCraftOriginal -and (Test-Path -LiteralPath $settingsPath)){
    $script:GoldCraftOriginal=(Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json).originalGame
}
if(-not $script:GoldCraftOriginal){
    $script:GoldCraftOriginal=Join-Path ${env:ProgramFiles(x86)} 'Steam/steamapps/common/Half-Life'
}
$script:GoldCraftOriginal=[IO.Path]::GetFullPath($script:GoldCraftOriginal).TrimEnd('\')

function Assert-NoReparsePath([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Refusing reparse point: $current"
            }
        }
        $parent = [IO.Directory]::GetParent($current)
        $current = if ($null -eq $parent) { $null } else { $parent.FullName }
    }
}

function Assert-WorkspacePath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if (-not $full.StartsWith($script:GoldCraftRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Write/launch destination must be below workspace: $full"
    }
    Assert-NoReparsePath $full
    return $full
}

function Assert-SandboxPath([string]$Path) {
    $full = Assert-WorkspacePath $Path
    $base = Join-Path $script:GoldCraftRoot 'sandbox'
    if (-not $full.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Runtime destination must be below sandbox: $full"
    }
    return $full
}

function Get-MetaHookSourceRoot {
    $lock = Get-Content -LiteralPath (Join-Path $script:GoldCraftRoot 'sources.lock.json') -Raw | ConvertFrom-Json
    return Assert-WorkspacePath (Join-Path $script:GoldCraftRoot $lock.sources.MetaHookSv.path)
}

function Get-OriginalFiles {
    Assert-NoReparsePath $script:GoldCraftOriginal
    # Enumerate one directory at a time so no directory link is traversed.
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue($script:GoldCraftOriginal)
    while ($queue.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Original contains a reparse point; independent copy required: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $queue.Enqueue($item.FullName) }
            else { $item }
        }
    }
}
