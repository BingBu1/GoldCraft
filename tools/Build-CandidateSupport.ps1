# Shared isolation for coordinated native candidates; no game is launched here.
function Initialize-GoldCraftBuildDirectory([string]$Output, [bool]$Fresh) {
    $path=if([IO.Path]::IsPathRooted($Output)){$Output}else{Join-Path $script:GoldCraftRoot $Output}
    $destination=Assert-WorkspacePath $path
    if($Fresh -and (Test-Path -LiteralPath $destination)) { throw "Fresh build requires a new directory: $destination" }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    return $destination
}

function New-GoldCraftCandidateTargets([string]$Destination, [int]$MaxJobs) {
    if($MaxJobs -lt 1 -or $MaxJobs -gt 2){throw 'Candidate builds allow one or two compiler processes.'}
    $destination=Assert-WorkspacePath $Destination
    $import=[Security.SecurityElement]::Escape((Join-Path $script:GoldCraftRoot 'cmake/GoldCraftClang.targets'))
    $objects=[Security.SecurityElement]::Escape((Join-Path $destination 'obj'))
    $targets=Join-Path $destination 'Build-Isolated.targets'
    # The late import supersedes GoldCraftClang's shared IntDir. Per-project
    # directories also keep dependency libraries and their PCH files separate.
    $text=@'
<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Import Project="__IMPORT__" />
  <PropertyGroup>
    <IntDir>__OBJECTS__\$(MSBuildProjectName)\$(Configuration)\</IntDir>
    <MultiProcessorCompilation>false</MultiProcessorCompilation>
    <UseMultiToolTask>true</UseMultiToolTask>
    <EnforceProcessCountAcrossBuilds>true</EnforceProcessCountAcrossBuilds>
    <MultiProcMaxCount>__JOBS__</MultiProcMaxCount>
    <CL_MPCount>__JOBS__</CL_MPCount>
  </PropertyGroup>
  <PropertyGroup Condition="'$(MSBuildProjectName)' == 'version'">
    <NMakeBuildCommandLine>rem Retain the source version header for this coordinated build.</NMakeBuildCommandLine>
    <NMakeReBuildCommandLine>rem Retain the source version header for this coordinated build.</NMakeReBuildCommandLine>
    <NMakeCleanCommandLine>rem Retain the source version header for this coordinated build.</NMakeCleanCommandLine>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <Link><AdditionalOptions>%(AdditionalOptions) /threads:__JOBS__</AdditionalOptions></Link>
  </ItemDefinitionGroup>
</Project>
'@
    $text=$text.Replace('__IMPORT__',$import).Replace('__OBJECTS__',$objects).Replace('__JOBS__',"$MaxJobs")
    if(-not(Test-Path -LiteralPath $targets) -or [IO.File]::ReadAllText($targets) -cne $text){
        [IO.File]::WriteAllText($targets,$text,[Text.UTF8Encoding]::new($false))
    }
    return $targets
}

function Get-GoldCraftCandidateMSBuildArguments($Compiler,[string]$Destination,[int]$MaxJobs) {
    $arguments=@(Get-GoldCraftClangMSBuildArguments $Compiler | Where-Object {$_ -notlike '/p:ForceImportAfterCppTargets=*'})
    $targets=New-GoldCraftCandidateTargets $Destination $MaxJobs
    return $arguments+@("/p:ForceImportAfterCppTargets=$targets",'/p:PreBuildEventUseInBuild=false',
        '/p:PostBuildEventUseInBuild=false','/p:MultiProcessorCompilation=false','/p:UseMultiToolTask=true',
        '/p:EnforceProcessCountAcrossBuilds=true',"/p:MultiProcMaxCount=$MaxJobs","/p:CL_MPCount=$MaxJobs")
}
