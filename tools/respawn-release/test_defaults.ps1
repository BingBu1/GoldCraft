param([string]$Root,[string]$Output)
$ErrorActionPreference='Stop'
$checks=New-Object 'Collections.Generic.List[object]'
$folders=@('scenario-update','fastdl-update','release-package','scenario-respawn-update','respawn-release','update-package')
foreach($folder in $folders){
    $scriptName=if($folder -eq 'update-package'){'Update-GoldCraft.ps1'}else{'Update-Server.ps1'}
    $file=Join-Path $Root ('tools/'+$folder+'/'+$scriptName)
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($file,[ref]$tokens,[ref]$errors)
    if($errors.Count){throw ($errors|Out-String)}
    $source=$ast.Extent.Text
    if($folder -eq 'update-package'){
        $function=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Read-UpdateTarget'},$true)
        $body=$function.Extent.Text+"`nRead-UpdateTarget"
    }else{
        $start=$source.IndexOf('    if([string]::IsNullOrWhiteSpace($TargetDirectory))')
        if($start -lt 0){$start=$source.IndexOf('    if ([string]::IsNullOrWhiteSpace($TargetDirectory))')}
        $end=$source.IndexOf('    $target', $start)
        if($start -lt 0 -or $end -lt 0){throw 'Input validation boundary changed'}
        $lineEnd=$source.IndexOf("`n",$end)
        $body=$source.Substring($start,$lineEnd-$start)
    }
    $helpers=@'
function Plain([string]$Path){throw ('RESOLVED:'+ $Path)}
function Assert-PlainPath([string]$Path){throw ('RESOLVED:'+ $Path)}
function Read-Host {throw 'PROMPT'}
'@
    $probe=[scriptblock]::Create($ast.ParamBlock.Extent.Text+"`n"+$helpers+"`n"+$body)
    $base=@{};if($folder -eq 'update-package'){$base.Role='server'}
    $cases=@(
        @{name='default';args=@{};expect='RESOLVED:E:\cs_MineCraft\GoldCraft-Servers'},
        @{name='plan default';args=@{Plan=$true};expect='RESOLVED:E:\cs_MineCraft\GoldCraft-Servers'},
        @{name='explicit';args=@{TargetDirectory='D:\server with spaces'};expect='RESOLVED:D:\server with spaces'},
        @{name='UNC';args=@{TargetDirectory='\\test-host\servers\CS'};expect='RESOLVED:\\test-host\servers\CS'},
        @{name='quoted';args=@{TargetDirectory=' "D:\server" '};expect='RESOLVED:D:\server'},
        @{name='empty';args=@{TargetDirectory=''};expect='REJECT'},
        @{name='null';args=@{TargetDirectory=$null};expect='REJECT'},
        @{name='whitespace';args=@{TargetDirectory='   '};expect='REJECT'},
        @{name='quoted whitespace';args=@{TargetDirectory='"   "'};expect='REJECT'},
        @{name='relative';args=@{TargetDirectory='server'};expect='REJECT'})
    if($folder -ne 'update-package'){$cases+=@{name='restore default';args=@{Restore=$true};expect='RESOLVED:E:\cs_MineCraft\GoldCraft-Servers'}}
    else{
        foreach($role in @('cs','minecraft')){
            $cases+=@{name=($role+' prompt preserved');args=@{Role=$role};expect='PROMPT'}
            $cases+=@{name=($role+' explicit preserved');args=@{Role=$role;TargetDirectory='D:\client'};expect='RESOLVED:D:\client'}
        }
    }
    foreach($case in $cases){
        $params=@{};foreach($key in $base.Keys){$params[$key]=$base[$key]};foreach($key in $case.args.Keys){$params[$key]=$case.args[$key]}
        $message=$null;try{& $probe @params|Out-Null}catch{$message=$_.Exception.Message}
        if($case.expect -eq 'REJECT'){$ok=$message -and $message -notlike 'RESOLVED:*' -and $message -ne 'PROMPT'}
        else{$ok=$message -ceq $case.expect}
        if(!$ok){throw ($folder+' '+$case.name+' failed: '+$message)}
        $checks.Add([pscustomobject]@{entry=$folder;check=$case.name;passed=$true})
    }
}
[pscustomobject]@{passed=$true;checks=$checks;version=$PSVersionTable.PSVersion.ToString();productionPathAccessed=$false;scope='Actual parameter AST and path validation; resolver replaced with a throwing sentinel before filesystem access'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $Output -Encoding UTF8
Write-Output ('PASS '+$checks.Count+' parameter checks')
