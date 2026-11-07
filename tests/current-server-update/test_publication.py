"""Install the complete launcher publication when its server directory is absent.

Reuses the restored integration fixture. Previously tested non-publication
payloads are brought current before this narrowly scoped integration test.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import shutil
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
STATE='.bingsang-current-update'


def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def main(package,target,prior_receipt,output):
    package,target,prior_receipt,output=map(lambda p:p.resolve(),(package,target,prior_receipt,output))
    if output.exists() or not target.is_relative_to(ROOT/'build') or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use the restored owned fixture and a fresh evidence directory')
    for path in (target,*target.parents,package,*package.parents):
        if path.is_symlink() or path.is_junction():raise ValueError('Linked path refused')
    prior=json.loads(prior_receipt.read_text('utf-8'))
    if not prior['passed']:raise ValueError('Prior full install/recovery test has not passed')
    output.mkdir(parents=True)
    manifest=json.loads((package/'update-manifest.json').read_text('utf-8'))
    publication=[r for r in manifest['files'] if r['path'].startswith('cs/launcher-public/')]
    if len(publication)!=20:raise ValueError('Expected signed manifest and nineteen complete core blobs')
    report=dict(passed=False,checks=[],calls=[],priorReceiptSha256=sha(prior_receipt),
                finalManifestSha256=sha(package/'update-manifest.json'),realRuntimeModified=False)

    def check(condition,label):
        if not condition:raise AssertionError(label)
        report['checks'].append(label);print('PASS '+label,flush=True)

    def snapshot():
        return {p.relative_to(target).as_posix():sha(p) for p in target.rglob('*')
                if p.is_file() and STATE not in p.parts}

    def run(*args):
        env=dict(os.environ);env.pop('PSModulePath',None)
        start=time.monotonic()
        result=subprocess.run(['powershell.exe','-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',
                               str(package/'Update-Server.ps1'),'-TargetDirectory',str(target),*args],
                              capture_output=True,env=env,timeout=600,creationflags=subprocess.CREATE_NO_WINDOW)
        log=output/f'call-{len(report["calls"])+1:02}.log';log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(args=args,exitCode=result.returncode,seconds=round(time.monotonic()-start,3),log=log.name))
        print(json.dumps(report['calls'][-1]),flush=True)
        if result.returncode:raise AssertionError(log.read_text(errors='replace')[-2500:])

    try:
        check(subprocess.check_output(['powershell.exe','-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()']).startswith(b'5.1.'),'Actual WinPS 5.1')
        state=target/STATE
        if state.exists():
            journal=json.loads((state/'20261107/transaction.json').read_text('utf-8-sig'))
            if journal['status']!='restored':raise ValueError('Previous test transaction not restored')
            os.rename(state,output/'previous-restored-transaction')
        for row in manifest['files']:
            if row in publication:continue
            dest=target/row['path'];source=package/'payload'/row['path']
            if dest.is_file() and sha(dest)==row['sha256']:continue
            if sha(source)!=row['sha256']:raise ValueError('Package changed during fixture setup')
            dest.parent.mkdir(parents=True,exist_ok=True);temp=dest.with_name(dest.name+'.fixture.tmp')
            immutable=source.suffix.lower() in {'.mdl','.spr','.seq','.wav','.mp3','.tga','.bmp','.png','.wad','.bsp','.nav','.ttf','.otf','.cso'}
            if immutable:
                os.link(source,temp)
                if not os.path.samestat(source.stat(),temp.stat()):raise ValueError('Fixture hard-link identity mismatch')
            else:shutil.copy2(source,temp)
            os.replace(temp,dest)
        existing=target/'cs/launcher-public'
        if existing.exists():os.rename(existing,output/'previous-launcher-public')
        initial=snapshot()
        check(not existing.exists(),'Launcher publication starts completely absent')
        run()
        check(all(sha(target/r['path'])==r['sha256'] for r in manifest['files']),'Final manifest payload is fully present with matching SHA')
        envelope=json.loads((existing/'manifest.json').read_text('utf-8-sig'))
        release=json.loads(base64.b64decode(envelope['payload']))
        check(release['sequence']==2026110701 and len(release['files'])==19,'Correct signed sequence and nineteen cores installed')
        check(all(sha(existing/'blobs'/r['sha256']/Path(r['path']).name)==r['sha256'] for r in release['files']),'All nineteen signed blobs are locally downloadable without an older publication')
        changes=json.loads((target/STATE/'20261107/transaction.json').read_text('utf-8-sig'))['files']
        ordered=[r['path'] for r in changes if r['path'].startswith('cs/launcher-public/')]
        check(len(ordered)==20,'Transaction covers manifest plus all nineteen missing core blobs')
        run('-Restore')
        check(snapshot()==initial,'Rollback removes the complete new publication and restores configuration')
        report['passed']=True
    finally:
        (output/'receipt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({'passed':report['passed'],'checks':len(report['checks'])}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('package','fixture','prior-receipt','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();main(a.package,a.fixture,a.prior_receipt,a.output)
