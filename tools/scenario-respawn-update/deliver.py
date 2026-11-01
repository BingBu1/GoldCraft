"""Publish verified local ZIP files only after installer and dedicated tests pass."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile

ROOT=Path(__file__).resolve().parents[2]


def identity(path):
    path=Path(path).absolute()
    for part in (path,*path.parents):
        if part.is_symlink() or part.is_junction():raise ValueError('Reparse path')
    with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)


def read(path):return json.loads(Path(path).read_text('utf-8-sig'))


def main(candidate, tests, live, output):
    if output.exists() or output!=ROOT/'server-packages/update-20261101-Respawn':
        raise ValueError('Use the authorized fresh delivery directory')
    receipt=read(candidate/'receipt.json');checks=read(tests/'result.json');game=read(live)
    if not checks['passed'] or len(checks['checks'])!=31 or len(checks['calls'])!=26:
        raise ValueError('Complete installer checks required')
    if not game['success'] or game['fixtureFailures'] or game['fixtureChecks']!=37 or not game['protectedCoreUnchanged']:
        raise ValueError('Dedicated lifecycle checks required')
    package=Path(receipt['package'])
    if identity(package/'update-manifest.json')['sha256']!=checks['productionManifestSha256']:
        raise ValueError('Tests did not verify this production manifest')
    if identity(package/'Update-Server.ps1')['sha256']!=checks['installerSha256']:
        raise ValueError('Installer changed after tests')
    for source in receipt['sourceScripts']:
        if identity(source['path'])['sha256']!=source['sha256']:raise ValueError('Source script changed')
    manifest=read(package/'update-manifest.json')
    if len(manifest['files'])!=78 or len(manifest['requiredBaseline'])!=1429:
        raise ValueError('Wrong full production scope')
    verified=[]
    for role,archive in receipt['archives'].items():
        actual=identity(archive['path'])
        if actual['sha256']!=archive['sha256'] or actual['bytes']!=archive['bytes']:
            raise ValueError('Candidate archive changed')
        with zipfile.ZipFile(archive['path']) as source_zip:
            names=source_zip.namelist()
            sums_name=next(name for name in names if name.endswith('/SHA256SUMS.txt'))
            prefix=sums_name.removesuffix('SHA256SUMS.txt')
            declared={}
            for line in source_zip.read(sums_name).decode('utf-8-sig').splitlines():
                digest,name=line.split('  ',1)
                if name in declared:raise ValueError('Duplicate checksum path')
                declared[name]=digest
            if set(names)!={prefix+name for name in declared}|{sums_name}:
                raise ValueError('Unlisted ZIP member')
            for name,digest in declared.items():
                with source_zip.open(prefix+name) as stream:
                    if hashlib.file_digest(stream,'sha256').hexdigest()!=digest:
                        raise ValueError('ZIP member checksum mismatch')
            if role=='server':
                if source_zip.read(prefix+'update-manifest.json')!=(package/'update-manifest.json').read_bytes():
                    raise ValueError('Archive manifest differs from tested manifest')
                for row in manifest['files']:
                    member=prefix+'payload/'+row['path'];info=source_zip.getinfo(member)
                    with source_zip.open(member) as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
                    if sha!=row['sha256'] or info.file_size!=row['bytes']:raise ValueError('Payload mismatch')
            elif set(declared)!={'cstrike/metahook/plugins/CSOLSupport.dll','使用说明.md'}:
                raise ValueError('Unexpected independent client payload')
        verified.append((role,actual))
    output.mkdir(parents=True)
    deliveries={}
    for role,source in verified:
        destination=output/Path(source['path']).name
        shutil.copy2(source['path'],destination)
        copied=identity(destination)
        if copied['sha256']!=source['sha256'] or destination.stat().st_nlink!=1:
            raise ValueError('Independent delivery copy failed')
        deliveries[role]={key:copied[key] for key in ('bytes','sha256')}|{'name':destination.name}
    note=(ROOT/'tools/scenario-respawn-update/README.md').read_text('utf-8-sig')
    note+='\n## 配套客户端文件\n\n'+(ROOT/'tools/scenario-respawn-update/CLIENT.md').read_text('utf-8-sig').split('\n',2)[2]
    (output/'使用说明.md').write_text(note,'utf-8')
    (output/'SHA256SUMS.txt').write_text(''.join(row['sha256']+'  '+row['name']+'\n' for row in deliveries.values()),'utf-8')
    public=dict(release=manifest['release'],archives=deliveries,payloadFiles=78,payloadBytes=6445590,
                newResources=77,omittedSameShaResources=1429,baselineMpSha256=manifest['baselineMpSha256'],
                engineSha256=manifest['engineSha256'],installerChecks=31,installerCalls=26,
                installationFixtureBaselineResources=3,fullProductionManifestApplied=False,
                dedicatedChecks=37,dedicatedFailures=0,clientSignatureChanged=False,
                remoteDeployed=False,realRemoteJoin=False,temporaryPawnIncluded=False)
    (output/'release-receipt.json').write_text(json.dumps(public,ensure_ascii=False,indent=2)+'\n','utf-8')
    local=dict(delivery=public,output=str(output),candidateReceipt=identity(candidate/'receipt.json'),
               installerResult=identity(tests/'result.json'),liveHandoff=identity(live),
               deliveredFiles=[identity(path) for path in output.iterdir() if path.is_file()])
    (candidate.parent/'delivery-final.json').write_text(json.dumps(local,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({'output':str(output),'archives':deliveries},ensure_ascii=True))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('candidate','tests','live','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    main(args.candidate.resolve(),args.tests.resolve(),args.live.resolve(),args.output.resolve())
