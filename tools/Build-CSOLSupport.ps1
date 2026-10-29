param([string]$Output='build/csol-runtime-20261021/client',
      [ValidateRange(1,2)][int]$MaxJobs=2,[switch]$Fresh,[switch]$LibraryOnly,[string]$CharacterFont='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot/SandboxPaths.ps1"
. "$PSScriptRoot/ClangToolchain.ps1"
. "$PSScriptRoot/Build-CandidateSupport.ps1"
$compiler=Initialize-GoldCraftCompiler
$destination=Initialize-GoldCraftBuildDirectory $Output $Fresh.IsPresent
$arguments=Get-GoldCraftClangCMakeArguments $compiler
if($CharacterFont){$arguments+="-DCSOL_CHARACTER_FONT_FILE=$(Assert-WorkspacePath $CharacterFont)"}
& cmake -S "$script:GoldCraftRoot/native/csol" -B $destination -G Ninja "-DCMAKE_MAKE_PROGRAM=$($compiler.Ninja)" -DCMAKE_BUILD_TYPE=Release @arguments
if($LASTEXITCODE){throw 'CSOLSupport configure failed'}
$buildArguments=@('--build',$destination,'--parallel',"$MaxJobs")
if($LibraryOnly){$buildArguments+=@('--target','CSOLSupport')}
& cmake @buildArguments
if($LASTEXITCODE){throw 'CSOLSupport build failed'}
