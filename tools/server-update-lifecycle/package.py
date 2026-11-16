"""Add automatic graceful stop/restart to current and inherited server patches.

Issued ZIPs remain immutable. Call enable() on a newly generated package before
hashing or sealing it. Exact template checks reject unknown installer layouts.
"""
from pathlib import Path
import argparse
import hashlib
import os
import shutil

ROOT=Path(__file__).resolve().parents[2]
MARKER='# Automatic server lifecycle v1'


def upgrade(text):
    if MARKER in text:
        return text
    replacements=[
        ("[switch]$Plan,[switch]$Restore)","[switch]$Plan,[switch]$Restore,[switch]$NoRestart)"),
        ("$lock=$null\n$scope=",MARKER+"\n. (Join-Path $PSScriptRoot 'Lifecycle.ps1')\n$lifecycle=$null;$safeToStart=$false\n$lock=$null\n$scope="),
        ("        Stopped $target;Unlocked $journal.files",
         "        $lifecycle=Begin-ServerUpdate $target -NoRestart:$NoRestart\n        $safeToStart=$true\n        Stopped $target;Unlocked $journal.files"),
        ("        if($journal.status -ne 'restored'){Restore-Files $target $state $journal}",
         "        $safeToStart=$false\n        if($journal.status -ne 'restored'){Restore-Files $target $state $journal}\n        $safeToStart=$true"),
        ("    if(!$Plan){Stopped $target;Unlocked @($pins.Keys|ForEach-Object {[pscustomobject]@{path=$_}})}\n",''),
        ("    # Check occupancy before the larger omitted-resource scan.\n    if(!$Plan){Stopped $target;Unlocked $rows}\n",''),
        ("    [IO.Directory]::CreateDirectory($state)|Out-Null",
         "    $lifecycle=Begin-ServerUpdate $target -NoRestart:$NoRestart\n    $safeToStart=$true\n    Stopped $target;Unlocked $rows\n    [IO.Directory]::CreateDirectory($state)|Out-Null"),
        ("    if($journal -and $journal.status -eq 'prepared'){Restore-Files $target $state $journal}",
         "    if($journal -and $journal.status -eq 'prepared'){\n        $safeToStart=$false;Restore-Files $target $state $journal;$safeToStart=$true\n    }"),
        ("        foreach($row in @($rows|Sort-Object", "        $safeToStart=$false\n        foreach($row in @($rows|Sort-Object"),
        ("        $journal.status='installed';Json $journalPath $journal",
         "        $journal.status='installed';Json $journalPath $journal\n        $safeToStart=$true"),
        ("    }catch{Restore-Files $target $state $journal;throw}",
         "    }catch{Restore-Files $target $state $journal;$safeToStart=$true;throw}"),
        ("}finally{if($lock){$lock.Dispose()}}",
         "}finally{\n    if($lock){$lock.Dispose()}\n    try{End-ServerUpdate $lifecycle $safeToStart}\n    catch{Write-Error $_ -ErrorAction Continue;exit 1}\n}"),
        ("Previous files and configuration restored. Use the original Start.cmd.",
         "Previous files and configuration restored. Previously running servers will restart automatically."),
        ("Restart with the original Start.cmd. Players update the two core DLLs with Bingsang Launcher.",
         "Previously running servers will restart automatically. Players update through Bingsang Launcher."),
    ]
    for old,new in replacements:
        if text.count(old)!=1:
            raise ValueError('Unsupported server updater lifecycle boundary: '+old[:100])
        text=text.replace(old,new,1)
    return text


def plain(path):
    path=Path(os.path.abspath(path))
    if not path.is_relative_to(ROOT) or any(p.is_symlink() or p.is_junction() for p in (path,*path.parents)):
        raise ValueError('Use a plain workspace package directory')
    return path


def enable(package, refresh_sums=False):
    package=plain(package)
    updater=plain(package/'Update-Server.ps1')
    text=upgrade(updater.read_text('utf-8-sig'))
    temporary=updater.with_name(updater.name+'.lifecycle.tmp')
    temporary.write_text(text,'utf-8-sig',newline='\r\n')
    os.replace(temporary,updater)
    target=plain(package/'Lifecycle.ps1');temporary=target.with_name(target.name+'.lifecycle.tmp')
    source=plain(Path(__file__).with_name('Lifecycle.ps1'))
    shutil.copy2(source,temporary)
    if source.read_bytes()!=temporary.read_bytes():
        raise ValueError('Lifecycle helper changed while copying')
    os.replace(temporary,target)
    if refresh_sums:
        # Baseline-derived hotfixes must not retain the older package's checksum
        # list after replacing the updater and adding its lifecycle helper.
        rows=[]
        for path in sorted(package.rglob('*')):
            plain(path)
            if path.is_file() and path!=package/'SHA256SUMS.txt':
                with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
                rows.append(digest+'  '+path.relative_to(package).as_posix()+'\n')
        sums=package/'SHA256SUMS.txt';temporary=sums.with_name(sums.name+'.lifecycle.tmp')
        temporary.write_text(''.join(rows),'utf-8',newline='\n');os.replace(temporary,sums)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('package',type=Path)
    p.add_argument('--refresh-sums',action='store_true');a=p.parse_args()
    enable(a.package,a.refresh_sums)
