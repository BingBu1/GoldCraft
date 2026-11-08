"""Exercise a real signed delta with WinPS5.1; missing baseline must fail before writes."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
PS=Path('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe')


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def run(package,baseline,output):
    package,baseline,output=(x.resolve() for x in (package,baseline,output))
    if output.exists() or not output.is_relative_to(ROOT/'build'):raise ValueError('Use fresh fixture output')
    output.mkdir(parents=True);target=output/'server';(target/'cs').mkdir(parents=True)
    for rel,data in [('Start.cmd',b'fixture; never execute'),('cs/hlds.exe',b'fixture; never execute'),('cs/server.cfg',b'unchanged settings')]:
        (target/rel).write_bytes(data)
    preserved={p.relative_to(target).as_posix():sha(p) for p in target.rglob('*') if p.is_file()}
    checks=[];pub=target/'cs/launcher-public';pub.mkdir();manifest=pub/'manifest.json'
    new=package/'payload/cs/launcher-public/manifest.json';old=baseline/'manifest.json'
    release=json.loads(base64.b64decode(json.loads(new.read_text('utf-8'))['payload']))
    prior=json.loads(base64.b64decode(json.loads(old.read_text('utf-8'))['payload']))
    prior_files={r['path']:r for r in prior['files']};new_files=[r for r in release['files'] if prior_files.get(r['path'])!=r]
    assert len(new_files)==1 and len(prior_files)==19 and len(release['resources'])==266
    state=target/('.bingsang-launcher-update/'+str(release['sequence']))
    def call(name,*args,ok=True):
        result=subprocess.run([str(PS),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/'Update-Server.ps1'),'-TargetDirectory',str(target),*args],capture_output=True,timeout=60)
        (output/(f'{len(checks):02d}.log')).write_bytes(result.stdout+result.stderr)
        if (result.returncode==0)!=ok:raise AssertionError((name,result.returncode,result.stdout,result.stderr))
        checks.append(dict(name=name,passed=True,exitCode=result.returncode))
    def install_baseline():
        shutil.copy2(old,manifest)
        for row in prior['files']:
            rel=Path('blobs')/row['sha256']/Path(row['path']).name;dest=pub/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(baseline/rel,dest)
    def path(row):return pub/'blobs'/row['sha256']/Path(row['path']).name
    call('empty publication rejected before writes',ok=False);assert not state.exists() and not manifest.exists()
    shutil.copy2(old,manifest)
    call('baseline manifest without blobs rejected before writes',ok=False);assert sha(manifest)==sha(old) and not state.exists()
    install_baseline();missing=path(prior['files'][0]);good=missing.read_bytes();missing.unlink()
    call('one missing unchanged blob rejected',ok=False);assert sha(manifest)==sha(old) and not path(new_files[0]).exists()
    missing.write_bytes(b'corrupt');call('one corrupt unchanged blob rejected',ok=False);assert not state.exists()
    missing.write_bytes(good);call('valid delta plan reads all 19 baseline DLLs','-Plan');assert not state.exists() and not path(new_files[0]).exists()
    call('delta installs one DLL and complete signed resource contract');assert sha(manifest)==sha(new)
    for row in release['files']:assert sha(path(row))==row['sha256']
    assert len(list((package/'payload/cs/launcher-public/blobs').rglob('*.dll')))==1
    call('repeated delta installation is idempotent')
    call('restore original signed baseline','-Restore');assert sha(manifest)==sha(old)
    call('reapply delta after restore');assert sha(manifest)==sha(new)
    record=state/'transaction.json';j=json.loads(record.read_text('utf-8-sig'));j['status']='prepared';record.write_text(json.dumps(j),'utf-8');shutil.copy2(old,manifest)
    call('interrupted delta safely resumes');assert sha(manifest)==sha(new)
    backup=state/'manifest.before.json';backup.write_bytes(b'corrupt');call('corrupt backup refused','-Restore',ok=False);assert sha(manifest)==sha(new);shutil.copy2(old,backup)
    manifest.write_bytes(b'unknown');call('unknown publication refused',ok=False);assert manifest.read_bytes()==b'unknown';shutil.copy2(new,manifest)
    # Current release can restore its changed blob without manufacturing a preimage.
    shutil.move(str(state),str(output/'saved-state'));path(new_files[0]).unlink()
    call('current release repairs the supplied new blob without a journal');assert sha(path(new_files[0]))==new_files[0]['sha256'] and not (state/'transaction.json').exists()
    call('restore without original backup refused','-Restore',ok=False);assert sha(manifest)==sha(new)
    missing.unlink();call('current release missing an omitted baseline blob refused',ok=False);assert sha(manifest)==sha(new);missing.write_bytes(good)
    for rel,digest in preserved.items():assert sha(target/rel)==digest
    (output/'receipt.json').write_text(json.dumps(dict(passed=True,checks=checks,packagedDLLs=1,reusedDLLs=19,resources=266,configurationUnchanged=True,serverExecuted=False,installerSha256=sha(package/'Update-Server.ps1')),indent=2)+'\n','utf-8')
    print(len(checks),'incremental WinPS checks passed; one new DLL plus 19 verified baseline DLLs.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('package','baseline','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();run(a.package,a.baseline,a.output)
