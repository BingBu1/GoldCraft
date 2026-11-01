"""Freeze a resource-only repair against an ordered set of verified server ZIPs."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import shutil
import zipfile
from package import plain, relative, sha, write_archive


def index(archives):
    merged = {}
    for row in archives:
        path = plain(row['path'])
        if path.stat().st_size != row['bytes'] or sha(path) != row['sha256']:
            raise ValueError('Baseline ZIP identity changed')
        with zipfile.ZipFile(path) as archive:
            names = archive.namelist()
            manifest_name, = [n for n in names if n.endswith('/update-manifest.json')]
            manifest = json.loads(archive.read(manifest_name))
            prefix = manifest_name.removesuffix('update-manifest.json')
            legacy = any(x['path'].startswith('payload/') for x in manifest['files'])
            seen = set()
            for item in manifest['files']:
                if legacy and not item['path'].startswith('payload/'):
                    continue
                name = relative(item['path'].removeprefix('payload/'))
                if name.lower() in seen:
                    raise ValueError('Duplicate ZIP destination')
                seen.add(name.lower())
                member = prefix + 'payload/' + name
                if member not in names:
                    raise ValueError('ZIP member missing')
                merged[name.lower()] = dict(path=name, bytes=item['bytes'], sha256=item['sha256'],
                                            archive=str(path), member=member)
    return merged


def build(inputs, closure_path, output):
    inputs, closure_path, output = map(plain, (inputs, closure_path, output))
    if output.exists():
        raise ValueError('Use a new output directory')
    spec = json.loads(inputs.read_text('utf-8-sig'))
    closure = json.loads(closure_path.read_text('utf-8-sig'))
    if spec['files'] != closure['files']:
        raise ValueError('Final additions differ from audited closure')
    merged = index(closure['archiveEvidence'])
    changed, required, frozen_rows, previous = [], [], [], []
    package = output / '冰桑-灾变资源修复'
    package.mkdir(parents=True)
    seen = set()
    for row in spec['files']:
        name = relative(row['path'])
        if name.lower() in seen or not name.startswith('cs/cstrike/') or Path(name).suffix.lower() not in {
                '.json', '.csv', '.cso', '.jsoo', '.txt', '.sc', '.tga', '.bmp', '.mdl', '.wav', '.spr'}:
            raise ValueError('Invalid resource destination')
        seen.add(name.lower())
        source = plain(row['source'])
        if source.stat().st_size != row['bytes'] or sha(source) != row['sha256']:
            raise ValueError('Input changed: ' + name)
        old = merged.get(name.lower())
        if old and old['bytes'] == row['bytes'] and old['sha256'] == row['sha256']:
            raise ValueError('Unnecessary payload already present: ' + name)
        if old:
            previous.append(old)
        frozen = output / 'frozen' / name
        frozen.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, frozen)
        target = package / 'payload' / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.suffix.lower() in {'.tga', '.bmp', '.mdl', '.wav', '.spr', '.cso'}:
            os.link(frozen, target)
            assert os.path.samestat(frozen.stat(), target.stat())
        else:
            shutil.copy2(frozen, target)
        if sha(source) != row['sha256'] or sha(target) != row['sha256']:
            raise ValueError('Input changed during freeze')
        changed.append({k: row[k] for k in ('path', 'bytes', 'sha256')})
        frozen_rows.append(dict(changed[-1], source=str(frozen)))
    # These stable modules/startup were delivered by package-02; never replace
    # them with current development outputs while repairing resource coverage.
    dependencies = list(closure['unchanged'])
    for name in ['cs/cstrike/dlls/mp.dll', 'cs/swds.dll', 'scripts/Start-Scenario.ps1', 'cs/launcher-public/manifest.json']:
        dependencies.append(merged[name.lower()])
    publication=merged['cs/launcher-public/manifest.json']
    with zipfile.ZipFile(publication['archive']) as archive:
        envelope=json.loads(archive.read(publication['member']))
    release=json.loads(base64.b64decode(envelope['payload']))
    client,=[r for r in release['files'] if r['path'].endswith('/CSOLSupport.dll')]
    client_path='cs/launcher-public/blobs/'+client['sha256']+'/CSOLSupport.dll'
    dependencies.append(merged[client_path.lower()])
    opened = {}
    try:
        for row in dependencies + previous:
            old = merged[row['path'].lower()]
            if (row['bytes'], row['sha256']) != (old['bytes'], old['sha256']):
                raise ValueError('Composite baseline mismatch')
            archive = opened.setdefault(old['archive'], None)
            if archive is None:
                archive = opened[old['archive']] = zipfile.ZipFile(old['archive'])
            with archive.open(old['member']) as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != old['sha256']:
                    raise ValueError('Baseline ZIP bytes mismatch')
        required = [{k: r[k] for k in ('path', 'bytes', 'sha256')} for r in dependencies]
    finally:
        for archive in opened.values():
            if archive:
                archive.close()
    manifest = dict(schema=1, scope='remote-scenario-resources', release='20261101-Scenario-Resources-v2',
                    files=changed, requiredBaseline=required,
                    baseline=[{k: r[k] for k in ('bytes', 'sha256')} for r in closure['archiveEvidence']])
    (package/'update-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    source_dir = Path(__file__).parent
    for name in ['Update-Server.cmd', 'Restore-Server.cmd', 'Update-Server.ps1', 'Configure.ps1', 'Files.ps1']:
        (package/name).write_text((source_dir/name).read_text('utf-8-sig'),
                                 'utf-8-sig' if name.endswith('.ps1') else 'utf-8', newline='\r\n')
    shutil.copy2(source_dir/'RESOURCE_REPAIR.md', package/'README.md')
    archive = output/'冰桑-灾变资源修复.zip'
    write_archive(package, archive, changed)
    receipt = dict(archive=str(archive), bytes=archive.stat().st_size, sha256=sha(archive),
                   payloadFiles=len(changed), payloadBytes=sum(r['bytes'] for r in changed),
                   replacedFiles=len(previous), newFiles=len(changed)-len(previous),
                   requiredBaselineFiles=len(required), omittedResourceFiles=len(closure['unchanged']),
                   omittedResourceBytes=sum(r['bytes'] for r in closure['unchanged']),
                   files=frozen_rows, previous=previous, archives=closure['archiveEvidence'],
                   remoteApplied=False, realRemoteJoin=False)
    (output/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    print(json.dumps({k: v for k, v in receipt.items() if k not in ['files', 'previous', 'archives']}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    for name in ['inputs', 'closure', 'output']:
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    build(a.inputs, a.closure, a.output)
