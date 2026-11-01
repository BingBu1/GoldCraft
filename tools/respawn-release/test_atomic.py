"""Compare real Windows replacement under a briefly held sharing lock."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import json
import subprocess
import time


def run(old, new, output):
    output.mkdir(parents=True,exist_ok=False)
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    runner=output/'replace.ps1'
    runner.write_text("param($Helper,$Target,$Source)\n. $Helper\nWrite-Output 'READY'\ntry{Atomic $Target $Source $null;exit 0}catch{Write-Output $_.Exception.GetBaseException().HResult;exit 1}\n",'utf-8')
    results=[]
    for label,helper in [('old',old),('new',new)]:
        target=output/(label+'-target.txt');source=output/(label+'-source.txt')
        target.write_bytes(b'old independent content');source.write_bytes(b'new independent content')
        handle=kernel.CreateFileW(str(target),0x80000000,1,None,3,0,None)
        if handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
        proc=None
        try:
            proc=subprocess.Popen(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(runner),str(helper),str(target),str(source)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
            assert proc.stdout.readline().strip()==b'READY'
            time.sleep(0.35)
        finally:kernel.CloseHandle(handle)
        stdout,stderr=proc.communicate(timeout=15)
        (output/(label+'.log')).write_bytes(stdout+stderr)
        assert (proc.returncode==0)==(label=='new')
        assert target.read_bytes()==(source.read_bytes() if label=='new' else b'old independent content')
        assert not list(output.glob('*.tmp'))
        results.append(dict(helper=label,exitCode=proc.returncode,expected=True))
    (output/'result.json').write_text(json.dumps({'passed':True,'results':results,'scope':'Real transient sharing lock; old implementation fails, new preserves identities and succeeds after release'},indent=2),'utf-8')
    print('PASS actual transient lock: old failure, new recovery; no temporary files left')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('old','new','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();run(a.old.resolve(),a.new.resolve(),a.output.resolve())
