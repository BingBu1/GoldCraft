"""Assemble the audited deployed delta and verify every ZIP payload byte."""
from pathlib import Path
import argparse
import base64
import importlib.util
import json
import os
import re
import shutil
import zipfile
import hashlib

spec = importlib.util.spec_from_file_location('current_prepare', Path(__file__).with_name('prepare.py'))
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
ROOT, sha, plain, identity = prepare.ROOT, prepare.sha, prepare.plain, prepare.identity


def build(plan_path, publication, output, refresh=False, stage_only=False):
    plan_path, publication, output = map(plain, (plan_path, publication, output))
    if (output.exists() and not refresh) or not output.is_relative_to(ROOT/'server-packages'):
        raise ValueError('Use a fresh server-packages output')
    spec = json.loads(plan_path.read_text('utf-8'))
    changed, required = list(spec['files']), list(spec['requiredBaseline'])
    envelope = json.loads((publication/'manifest.json').read_text('utf-8-sig'))
    release = json.loads(base64.b64decode(envelope['payload']))
    if release['sequence'] != 2026110701:
        raise ValueError('Wrong paired signed release')
    prior_cores = {r['path']:r for r in spec['clientBaseline']['files']}
    for row in release['files']:
        path = 'cs/launcher-public/blobs/'+row['sha256']+'/'+Path(row['path']).name
        source = publication/path.removeprefix('cs/launcher-public/')
        if source.is_file():
            actual = identity(source)
            if (actual['bytes'],actual['sha256']) != (row['bytes'],row['sha256']):
                raise ValueError('Client core changed')
            changed.append(dict(path=path, **actual, priorSha256=[]))
        else:
            if row != prior_cores[row['path']]:
                raise ValueError('Unpublished core change')
            # The server package carries the complete launcher publication,
            # including unchanged core blobs, so operators need no older blobs.
            actual=identity(ROOT/'sandbox/cs-client-b/Half-Life'/row['path'])
            if (actual['bytes'],actual['sha256'])!=(row['bytes'],row['sha256']):
                raise ValueError('Unchanged launcher core differs from signed baseline')
            prepare.x86(actual['source'])
            changed.append(dict(path=path,**actual,priorSha256=[row['sha256']]))
    name='cs/launcher-public/manifest.json'
    changed.append(dict(path=name,**identity(publication/'manifest.json'),priorSha256=sorted(spec['history'][name.lower()])))
    package=output/'BingSang-Server-Update-20261107'
    package.mkdir(parents=True,exist_ok=refresh)
    linked_bytes=0
    for index,row in enumerate(changed,1):
        source=plain(row['source'])
        if not source.is_relative_to(ROOT):
            raise ValueError('Payload source outside workspace')
        if source.stat().st_size!=row['bytes'] or sha(source)!=row['sha256']:
            raise ValueError('Planned source changed: '+row['path'])
        target=plain(package/'payload'/row['path']);target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists():
            if not refresh or sha(target)!=row['sha256']:
                raise ValueError('Existing payload differs; preserve it for investigation')
            if os.path.samestat(source.stat(),target.stat()):linked_bytes+=row['bytes']
            continue
        immutable=source.suffix.lower() in {'.mdl','.spr','.seq','.wav','.mp3','.tga','.bmp','.png','.wad','.bsp','.nav','.ttf','.otf','.cso'}
        # Do not extend a link group whose other owners are unknown. The source
        # and new payload are both recorded; update tools replace paths atomically.
        if immutable and source.stat().st_nlink==1:
            os.link(source,target)
            if not os.path.samestat(source.stat(),target.stat()):
                raise ValueError('Hard-link identity mismatch')
            linked_bytes+=row['bytes']
        else:
            shutil.copy2(source,target)
            if target.stat().st_nlink!=1:
                raise ValueError('Expected independent mutable payload')
        if sha(target)!=row['sha256']:
            raise ValueError('Payload identity mismatch')
        if index%250==0:
            print(json.dumps({'phase':'stage','files':index,'total':len(changed)}),flush=True)
    active=set()
    for config in (ROOT/'sandbox/cs-server/Half-Life/cstrike/addons/amxmodx/configs').glob('plugins*.ini'):
        if config.name=='plugins.stock.ini' or re.search(r'(test|probe|live)',config.name,re.I):
            continue
        for line in config.read_text('utf-8-sig').splitlines():
            match=re.match(r'^\s*([\w.-]+\.amxx)\b([^;]*)',line)
            if match and match[1] in spec['plugins'] and not re.search(r'\bdisabled\b',match[2],re.I):
                active.add(match[1])
    def public(row):
        return {k:row[k] for k in ('path','bytes','sha256','priorSha256','baselineRelease') if k in row}
    manifest=dict(schema=1,scope=spec['scope'],release=spec['release'],sourceRevision=spec['revision'],
                  baselineBasis='previously issued package manifests; remote state not inspected',
                  issuedPackages=[dict(release=r['release'],file=Path(r['source']).name,sha256=r['sha256']) for r in spec['archives']],
                  files=[public(r) for r in changed],requiredBaseline=[public(r) for r in required],
                  requiredPlugins=sorted(active))
    (package/'update-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n','utf-8')
    scripts={
        'Update-Server.ps1':ROOT/'tools/current-server-update/Update-Server.ps1',
        'Configuration.ps1':ROOT/'tools/current-server-update/Configuration.ps1',
        'Publication.ps1':ROOT/'tools/current-server-update/Publication.ps1',
        'Scenario.ps1':ROOT/'tools/scenario-update/Configure.ps1',
        'Files.ps1':ROOT/'tools/scenario-update/Files.ps1',
        'Update-Server.cmd':ROOT/'tools/release-package/Update-Server.cmd',
        'Restore-Server.cmd':ROOT/'tools/release-package/Restore-Server.cmd',
        'README.md':ROOT/'tools/current-server-update/README.md',
        'release-public-key.xml':ROOT/'tools/respawn-release/release-public-key.xml',
    }
    for name,source in scripts.items():
        dest=package/name;temp=dest.with_name(dest.name+'.refresh.tmp')
        if name.endswith('.ps1'):
            temp.write_text(source.read_text('utf-8-sig'),'utf-8-sig',newline='\r\n')
        else:
            shutil.copy2(source,temp)
        os.replace(temp,dest)
    expected_payload={r['path'].lower() for r in changed}
    actual_payload={p.relative_to(package/'payload').as_posix().lower() for p in (package/'payload').rglob('*') if p.is_file()}
    if actual_payload!=expected_payload:raise ValueError('Unexpected staged payload files')
    members=[(package/'payload'/r['path'],'payload/'+r['path'],r['sha256']) for r in changed]
    members.extend((p,p.name,sha(p)) for p in sorted(package.iterdir()) if p.is_file() and p.name!='SHA256SUMS.txt')
    (package/'SHA256SUMS.txt').write_text(''.join(d+'  '+n+'\n' for p,n,d in members),'utf-8')
    members.append((package/'SHA256SUMS.txt','SHA256SUMS.txt',sha(package/'SHA256SUMS.txt')))
    if stage_only:
        print(json.dumps({'package':str(package),'payloadFiles':len(changed),'omittedFiles':len(required),
                          'archiveUpdated':False}),flush=True)
        return
    archive_path=output/'BingSang-Server-Update-20261107.zip'
    partial=archive_path.with_suffix('.zip.partial')
    with zipfile.ZipFile(partial,'x',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as archive:
        for index,(source,name,digest) in enumerate(members,1):
            archive.write(source,package.name+'/'+name)
            if index%500==0:
                print(json.dumps({'phase':'zip','files':index,'total':len(members)}),flush=True)
    with zipfile.ZipFile(partial) as archive:
        expected={package.name+'/'+name:(source,digest) for source,name,digest in members}
        if len(archive.namelist())!=len(expected) or set(archive.namelist())!=set(expected):
            raise ValueError('Archive inventory mismatch')
        for name,(source,digest) in expected.items():
            with archive.open(name) as stream:
                actual=hashlib.file_digest(stream,'sha256').hexdigest()
            if actual!=digest or sha(source)!=digest:
                raise ValueError('Archive or staged input changed: '+name)
    os.replace(partial,archive_path)
    receipt=dict(archive=identity(archive_path),package=str(package),payloadFiles=len(changed),
                 payloadBytes=sum(r['bytes'] for r in changed),omittedFiles=len(required),
                 omittedBytes=sum(r['bytes'] for r in required),signedSequence=release['sequence'],
                 hardLinkedPayloadBytes=linked_bytes,allZipHashesVerified=True,
                 remoteStateInspected=False,remoteDeployed=False,installerTestsPending=True,
                 files=changed,requiredBaseline=required,scriptInputs=[identity(p) for p in scripts.values()])
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({k:v for k,v in receipt.items() if k not in ('files','requiredBaseline','scriptInputs')},ensure_ascii=False),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,required=True)
    parser.add_argument('--publication',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--refresh',action='store_true')
    parser.add_argument('--stage-only',action='store_true')
    args=parser.parse_args()
    build(args.plan,args.publication,args.output,args.refresh,args.stage_only)
