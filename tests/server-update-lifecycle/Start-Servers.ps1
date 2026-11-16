param([switch]$Hidden)
. (Join-Path $PSScriptRoot 'Common.ps1')
if(Test-Path -LiteralPath (Join-Path $State 'fail-start')){throw 'fixture startup failed'}
$gate=[IO.File]::Open((Join-Path $State 'startup.lock'),'OpenOrCreate','ReadWrite','None')
try {
    foreach($role in @('cs','minecraft')){
        if(Get-RoleProcess $role){throw 'fixture role already running'}
        $file=if($role -eq 'cs'){Join-Path $PackageRoot 'cs/hlds.exe'}else{Join-Path $PackageRoot 'runtime/java/bin/java.exe'}
        $process=Start-Process -FilePath $file -ArgumentList ('"'+$PackageRoot+'" '+$role) -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput (Join-Path $State ($role+'.out.log')) -RedirectStandardError (Join-Path $State ($role+'.err.log'))
        $limit=[DateTime]::UtcNow.AddSeconds(5)
        while(!(Get-RoleProcess $role)){
            if($process.HasExited -or [DateTime]::UtcNow -gt $limit){throw 'fixture role did not start'}
            Start-Sleep -Milliseconds 50
        }
        $process.Dispose()
    }
}finally{$gate.Dispose()}
