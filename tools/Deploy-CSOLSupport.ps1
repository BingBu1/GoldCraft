param(
    [ValidateSet('cs-client-a','cs-client-b')][string]$Instance='cs-client-b',
    [Parameter(Mandatory)][string]$VerifiedClient,
    [switch]$ValidateOnly
)
. (Join-Path $PSScriptRoot 'SandboxPaths.ps1')
$reference=Assert-SandboxPath $VerifiedClient
$game=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/Half-Life")
if($reference -eq $game){throw 'Use a separately verified client as the deployment source.'}
$plugin='cstrike/metahook/plugins/CSOLSupport.dll'
$list='cstrike/metahook/configs/plugins.lst'
$source=Assert-SandboxPath (Join-Path $reference $plugin)
$target=Assert-SandboxPath (Join-Path $game $plugin)
$listPath=Assert-SandboxPath (Join-Path $game $list)
$text=[IO.File]::ReadAllText($listPath)
$names=@($text -split '\r?\n' | ForEach-Object {($_ -split '//',2)[0].Trim()} | Where-Object {$_ -and -not $_.StartsWith('#')})
$referenceNames=@([IO.File]::ReadAllLines((Join-Path $reference $list)) | ForEach-Object {($_ -split '//',2)[0].Trim()} | Where-Object {$_ -and -not $_.StartsWith('#')})
$withoutCsol=@($names | Where-Object {$_ -ine 'CSOLSupport.dll'})
$referenceWithoutCsol=@($referenceNames | Where-Object {$_ -ine 'CSOLSupport.dll'})
if(@($names | Where-Object {$_ -ieq 'CSOLSupport.dll'}).Count -gt 1 -or @($referenceNames | Where-Object {$_ -ieq 'CSOLSupport.dll'}).Count -ne 1){
    throw 'CSOLSupport.dll must appear exactly once in the verified plugin chain.'
}
if(($withoutCsol -join "`n") -cne ($referenceWithoutCsol -join "`n") -or $referenceNames[-1] -ine 'CSOLSupport.dll'){
    throw 'The verified client must use the same plugin order, followed by CSOLSupport.dll.'
}
if($names -contains 'CSOLSupport.dll' -and $names[-1] -ine 'CSOLSupport.dll'){throw 'Existing CSOLSupport plugin order differs from the verified client.'}
$identity=@('MetaHook.exe','hw.dll','FileSystem_Stdio.dll','cstrike/cl_dlls/client.dll')
$identity+=@($withoutCsol | ForEach-Object {"cstrike/metahook/plugins/$_"})
foreach($directory in @('cstrike/metahook/gamedata','cstrike/metahook/dlls')){
    $base=Assert-SandboxPath (Join-Path $game $directory)
    if(Test-Path -LiteralPath $base){
        Assert-NoReparsePath $base
        $identity+=@(Get-ChildItem -LiteralPath $base -File -Recurse | ForEach-Object {$_.FullName.Substring($game.Length+1)})
    }
}
$verified=foreach($relative in $identity){
    $local=Assert-SandboxPath (Join-Path $game $relative)
    $tested=Assert-SandboxPath (Join-Path $reference $relative)
    $sha=(Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash
    if($sha -ne (Get-FileHash -LiteralPath $tested -Algorithm SHA256).Hash){throw "Runtime differs from the verified client: $relative"}
    @{path=$relative;sha256=$sha}
}
$sha=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$newText=$text
if($names -notcontains 'CSOLSupport.dll'){
    $newline=if($text.Contains("`r`n")){"`r`n"}else{"`n"}
    if($newText -and -not $newText.EndsWith("`n")){$newText+=$newline}
    $newText+='CSOLSupport.dll'+$newline
}
$same=(Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $sha -and $newText -ceq $text
if($ValidateOnly -or $same){@{validated=$true;unchanged=$same;runtimeFiles=$verified.Count;sha256=$sha}|ConvertTo-Json;return}
if(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($game+'\',[StringComparison]::OrdinalIgnoreCase)}){
    throw "Stop $Instance before deploying its plugin."
}
$backup=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/backups/csolsupport-$(Get-Date -Format yyyyMMdd-HHmmss)")
New-Item -ItemType Directory -Path $backup | Out-Null
Copy-Item -LiteralPath $listPath -Destination (Join-Path $backup 'plugins.lst')
$hadPlugin=Test-Path -LiteralPath $target
if($hadPlugin){Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'CSOLSupport.dll')}
try{
    Copy-Item -LiteralPath $source -Destination $target -Force
    if((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $sha){throw 'Deployed DLL hash mismatch.'}
    [IO.File]::WriteAllText($listPath,$newText,[Text.UTF8Encoding]::new($false))
}catch{
    Copy-Item -LiteralPath (Join-Path $backup 'plugins.lst') -Destination $listPath -Force
    if($hadPlugin){Copy-Item -LiteralPath (Join-Path $backup 'CSOLSupport.dll') -Destination $target -Force}
    elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target}
    throw
}
$receipt=@{deployedUtc=[DateTime]::UtcNow.ToString('o');plugin=$plugin;sha256=$sha;backup=$backup;verifiedRuntime=$verified}
$receiptPath=Assert-SandboxPath (Join-Path $script:GoldCraftRoot "sandbox/$Instance/csol-support-deployment.json")
$receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $receiptPath -Encoding utf8
@{deployed=$true;runtimeFiles=$verified.Count;sha256=$sha;receipt=$receiptPath}|ConvertTo-Json
