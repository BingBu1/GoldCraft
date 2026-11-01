"""Package the frozen free revival pair with its signed launcher publication."""
from pathlib import Path
import argparse
import base64
import hashlib
import importlib.util
import json

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('respawn_package', ROOT/'tools/scenario-respawn-update/package.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)


def build(output, release):
    output, release = base.plain(output), base.plain(release)
    if output.exists() or not output.is_relative_to(ROOT/'build/respawn-unified-20261101'):
        raise ValueError('Use a fresh owned output directory')
    plan_path = ROOT/'build/csol-respawn-remote-closure-20261101/plan-03/plan.json'
    if base.digest(plan_path) != base.PLAN:
        raise ValueError('Resource plan changed')
    plan = json.loads(plan_path.read_text('utf-8-sig'))
    envelope = json.loads((release/'manifest.json').read_text('utf-8-sig'))
    signed = json.loads(base64.b64decode(envelope['payload']))
    previous = json.loads(base64.b64decode(json.loads((ROOT/'build/remote-scenario-package-20261101/launcher-public/manifest.json').read_text())['payload']))
    assert signed['sequence'] == 2026110105 and previous['sequence'] == 2026110104
    old = {row['path']:row for row in previous['files']}
    changed = [row for row in signed['files'] if row != old[row['path']]]
    assert len(signed['files']) == 19 and len(changed) == 1
    assert changed[0]['path'] == 'cstrike/metahook/plugins/CSOLSupport.dll'
    assert changed[0]['sha256'] == base.CLIENT
    assert len(plan['files']) == 77 and len(plan['requiredBaseline']) == 1429
    package = output/'冰桑-免费复活配套更新'
    package.mkdir(parents=True)
    rows = []

    def payload(name, source, sha):
        dest = package/'payload'/base.relative(name)
        base.copy_verified(source, dest, sha)
        rows.append(dict(path=name, bytes=dest.stat().st_size, sha256=sha))

    for row in plan['files']:
        payload(row['path'], row['source'], row['sha256'])
    payload('cs/cstrike/dlls/mp.dll', ROOT/'build/csol-respawn-integration-20261101/server-02/output/server/bin/mp.dll', base.NEW_MP)
    blob = 'blobs/'+base.CLIENT+'/CSOLSupport.dll'
    payload('cs/launcher-public/'+blob, release/blob, base.CLIENT)
    payload('cs/launcher-public/manifest.json', release/'manifest.json', base.digest(release/'manifest.json'))
    manifest = dict(schema=2, scope='remote-respawn-signed', release='20261101-Respawn-Signed-v2',
                    baselineMpSha256=base.OLD_MP, engineSha256=base.ENGINE,
                    files=sorted(rows,key=lambda row:row['path'].lower()),
                    requiredBaseline=[{k:row[k] for k in ('path','bytes','sha256')} for row in plan['requiredBaseline']])
    (package/'update-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n','utf-8')
    scripts = {'Update-Server.ps1':ROOT/'tools/respawn-release/Update-Server.ps1',
               'Publication.ps1':ROOT/'tools/respawn-release/Publication.ps1',
               'Files.ps1':ROOT/'tools/scenario-update/Files.ps1',
               'Update-Server.cmd':ROOT/'tools/release-package/Update-Server.cmd',
               'Restore-Server.cmd':ROOT/'tools/release-package/Restore-Server.cmd',
               '使用说明.md':ROOT/'tools/respawn-release/README.md',
               'release-public-key.xml':ROOT/'tools/respawn-release/release-public-key.xml'}
    for name, source in scripts.items():
        base.copy_verified(source, package/name, base.digest(source))
    sums=''.join(base.digest(p)+'  '+p.relative_to(package).as_posix()+'\n' for p in sorted(package.rglob('*')) if p.is_file())
    (package/'SHA256SUMS.txt').write_text(sums,'utf-8')
    zipped=base.archive(package,output/'冰桑-免费复活配套更新-20261101-v2.zip')
    receipt=dict(package=str(package),archive=zipped,payloadFiles=len(rows),payloadBytes=sum(x['bytes'] for x in rows),
                 baselineFiles=1429,unchangedClientCores=18,sequence=signed['sequence'],
                 manifest=base.identity(package/'update-manifest.json'),scripts=[base.identity(x) for x in scripts.values()],
                 deployed=False,testsPending=True)
    (output/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({k:receipt[k] for k in ('archive','payloadFiles','payloadBytes','sequence')},ensure_ascii=False))


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--release',type=Path,required=True)
    args=parser.parse_args()
    build(args.output,args.release)
