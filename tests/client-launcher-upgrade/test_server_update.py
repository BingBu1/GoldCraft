"""Run the real shipped installer in Windows PowerShell 5.1 against isolated files."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[2]
PS=Path('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe')


def sha(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def run(package,old_publication,output,managed=False):
    package,old_publication,output=(x.resolve() for x in (package,old_publication,output))
    if output.exists() or not output.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh build fixture')
    output.mkdir(parents=True);checks=[]
    target=output/'server';(target/'cs').mkdir(parents=True)
    for rel,data in {'Start.cmd':b'fixture; never execute','cs/hlds.exe':b'fixture; never execute',
                     'server-settings.json':b'{"port":27015}', 'cs/cstrike/server.cfg':b'unchanged configuration'}.items():
        dest=target/rel;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
    protected={p.relative_to(target).as_posix():sha(p) for p in target.rglob('*') if p.is_file()}
    publication=target/'cs/launcher-public';publication.mkdir()
    manifest=publication/'manifest.json';old=old_publication/'manifest.json'
    shutil.copy2(old,manifest);old_hash=sha(old)
    new=package/'payload/cs/launcher-public/manifest.json';new_hash=sha(new)
    if managed:
        import base64
        release=json.loads(base64.b64decode(json.loads(new.read_text('utf-8-sig'))['payload']))
        state=target/('.bingsang-launcher-update/'+str(release['sequence']))
    else:state=target/'.bingsang-launcher-update/20261108'
    script='Update-Server.ps1' if managed else 'Update-Launcher.ps1'
    blobs=list((package/'payload/cs/launcher-public/blobs').rglob('*.dll'))

    def call(name,*args,ok=True):
        result=subprocess.run([str(PS),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/script),
                               '-TargetDirectory',str(target),*args],capture_output=True,timeout=60)
        (output/(f'{len(checks):02d}.log')).write_bytes(result.stdout+result.stderr)
        if (result.returncode==0)!=ok:raise AssertionError((name,result.returncode,result.stdout,result.stderr))
        checks.append(dict(name=name,exitCode=result.returncode,passed=True))

    call('plan validates full package without writes','-Plan')
    assert not state.exists() and sha(manifest)==old_hash and not (publication/'blobs').exists()
    call('empty blob directory receives all 19 cores')
    assert sha(manifest)==new_hash
    for source in blobs:
        dest=publication/source.relative_to(package/'payload/cs/launcher-public')
        assert sha(dest)==sha(source)
    journal=(state/'transaction.json').read_bytes()
    call('repeated update is idempotent');assert (state/'transaction.json').read_bytes()==journal
    call('restore previous signed manifest','-Restore');assert sha(manifest)==old_hash
    call('reapply after restore');assert sha(manifest)==new_hash
    # Simulate interruption after writing the prepared journal but before manifest commit.
    record=json.loads((state/'transaction.json').read_text('utf-8-sig'));record['status']='prepared'
    (state/'transaction.json').write_text(json.dumps(record),'utf-8');shutil.copy2(old,manifest)
    call('prepared transaction safely resumes');assert sha(manifest)==new_hash
    saved=state/'manifest.before.json';saved.write_bytes(b'corrupt backup')
    call('corrupt recovery backup rejected','-Restore',ok=False);assert sha(manifest)==new_hash
    shutil.copy2(old,saved)
    manifest.write_bytes(b'unknown publication')
    call('unknown publication rejected',ok=False);assert manifest.read_bytes()==b'unknown publication'
    shutil.copy2(new,manifest)
    dest_blob=publication/blobs[0].relative_to(package/'payload/cs/launcher-public');good=dest_blob.read_bytes()
    dest_blob.write_bytes(b'corrupt blob');call('changed immutable target blob rejected',ok=False);assert sha(manifest)==new_hash
    dest_blob.write_bytes(good)
    # Existing current publication, no journal, a missing blob: repair without inventing a backup.
    shutil.move(str(state),str(output/'saved-journal'));dest_blob.unlink()
    call('current publication without journal repairs a missing blob')
    assert sha(dest_blob)==sha(blobs[0]) and not (state/'transaction.json').exists()
    call('repair without journal remains idempotent')
    call('restore without an original backup refuses safely','-Restore',ok=False);assert sha(manifest)==new_hash
    # Fresh launcher publication is supported without old blob files or a manifest.
    manifest.unlink()
    call('absent publication installs completely');assert sha(manifest)==new_hash
    call('restore absent original removes only manifest','-Restore');assert not manifest.exists() and dest_blob.exists()
    call('fresh publication can be reapplied');assert sha(manifest)==new_hash
    for rel,digest in protected.items():assert sha(target/rel)==digest
    (output/'receipt.json').write_text(json.dumps(dict(passed=True,checks=checks,coreFiles=len(blobs),
        configurationUnchanged=True,serverExecuted=False,installerSha256=sha(package/script)),indent=2)+'\n','utf-8')
    print(f'{len(checks)} Windows PowerShell installer checks passed; all 19 cores and preserved configuration verified.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('package','old-publication','output'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--managed',action='store_true')
    a=p.parse_args();run(a.package,a.old_publication,a.output,a.managed)
