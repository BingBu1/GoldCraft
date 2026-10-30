param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$SessionSource='native/csol/client/session.cpp',
    [string]$Python='python',
    [switch]$ExpectFailure,
    [switch]$FullClient
)
$ErrorActionPreference='Stop'
$script:GoldCraftRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $script:GoldCraftRoot
. "$PSScriptRoot/ClangToolchain.ps1"
$compiler=Initialize-GoldCraftCompiler
$clang=Join-Path $compiler.Root 'bin/clang-cl.exe'
$arguments=@('-B','-X','utf8',"$PSScriptRoot/Test-CSOLSessionClock.py",'--clang',$clang,'--output',$OutputDirectory,'--session',$SessionSource)
if($ExpectFailure){$arguments+='--expect-failure'}
if($FullClient){$arguments+='--full-client'}
& $Python @arguments
if($LASTEXITCODE -ne 0){throw 'Session clock checks failed'}
