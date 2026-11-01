"""Build a small, hash-based remote update from explicitly frozen local inputs.

The prior ZIP must be the user's confirmed remote installation baseline. Files
excluded against that baseline remain mandatory hash checks in the updater.
Local absolute source paths stay in the build receipt, never in the ZIP.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import stat
import zipfile


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path):
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        if part.exists() and part.lstat().st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError('Reparse point: ' + str(part))
    return path


def relative(value):
    if not value or ':' in value or '\\' in value or any(x in ('', '.', '..') for x in value.split('/')):
        raise ValueError('Invalid package relative path')
    return value


def write_archive(package, archive_path, changed):
    # Keep manifest spelling even on case-insensitive Windows filesystems.
    # Directory enumeration can otherwise turn resource/Hud into resource/hud.
    with zipfile.ZipFile(archive_path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for row in changed:
            archive.write(package / 'payload' / row['path'], package.name + '/payload/' + row['path'])
        for path in sorted(package.iterdir()):
            if path.is_file():
                archive.write(path, package.name + '/' + path.name)
    with zipfile.ZipFile(archive_path) as archive:
        assert archive.testzip() is None
        for row in changed:
            data = archive.read(package.name + '/payload/' + row['path'])
            assert len(data) == row['bytes'] and hashlib.sha256(data).hexdigest() == row['sha256']


def build(inputs, baseline, output):
    inputs, baseline, output = map(plain, (inputs, baseline, output))
    if output.exists():
        raise ValueError('Use a new output directory')
    spec = json.loads(inputs.read_text('utf-8-sig'))
    if sha(baseline) != spec['baselineSha256']:
        raise ValueError('Confirmed remote baseline ZIP changed')
    with zipfile.ZipFile(baseline) as archive:
        name = next(n for n in archive.namelist() if n.endswith('/update-manifest.json'))
        prior = json.loads(archive.read(name))
    base = {r['path'].removeprefix('payload/'): r for r in prior['files'] if r['path'].startswith('payload/')}
    package = output / '冰桑-远程灾变更新'
    frozen = output / 'frozen'
    package.mkdir(parents=True)
    required, changed, originals = [], [], []
    seen = set()
    for row in spec['files']:
        path = relative(row['path'])
        if path.lower() in seen:
            raise ValueError('Duplicate destination: ' + path)
        seen.add(path.lower())
        source = plain(Path(row['source']))
        if source.stat().st_size != row['bytes'] or sha(source) != row['sha256']:
            raise ValueError('Source changed: ' + path)
        public = {k: row[k] for k in ('path', 'bytes', 'sha256')}
        old = base.get(path)
        originals.append(row)
        if old and old['bytes'] == row['bytes'] and old['sha256'] == row['sha256']:
            required.append(public)
            continue
        snapshot = frozen / path
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, snapshot)  # detach snapshots from live/updateable sources
        if sha(snapshot) != row['sha256'] or sha(source) != row['sha256']:
            raise ValueError('Source changed during freeze')
        target = package / 'payload' / path
        target.parent.mkdir(parents=True, exist_ok=True)
        # Only immutable resource snapshots use hard links. DLLs and config-like
        # files remain independent because deployment/build tools can rewrite them.
        immutable = target.suffix.lower() in {'.wav', '.mp3', '.mdl', '.spr', '.bsp', '.nav', '.wad', '.tga', '.ttf', '.cso'}
        if immutable:
            os.link(snapshot, target)
            assert os.path.samestat(snapshot.stat(), target.stat())
        else:
            shutil.copy2(snapshot, target)
            assert target.stat().st_nlink == 1
        if sha(target) != row['sha256']:
            raise ValueError('Staged file mismatch')
        changed.append(public)
    manifest = dict(schema=1, scope='remote-scenario-delta', release=spec['release'],
                    baseline=prior['release'], baselineSha256=spec['baselineSha256'],
                    map=spec['map'], files=changed, requiredBaseline=required)
    (package / 'update-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    for name in ('Update-Server.cmd', 'Restore-Server.cmd', 'Update-Server.ps1', 'Configure.ps1', 'Files.ps1', 'README.md'):
        source = Path(__file__).parent / name
        # Windows PowerShell 5.1 needs BOM to read Chinese literals correctly.
        text = source.read_text('utf-8-sig')
        (package / name).write_text(text, 'utf-8-sig' if name.endswith('.ps1') else 'utf-8', newline='\r\n')
    archive_path = output / '冰桑-远程灾变更新.zip'
    write_archive(package, archive_path, changed)
    report = dict(archive=str(archive_path), bytes=archive_path.stat().st_size, sha256=sha(archive_path),
                  payloadFiles=len(changed), payloadBytes=sum(x['bytes'] for x in changed),
                  omittedFiles=len(required), omittedBytes=sum(x['bytes'] for x in required),
                  baseline=prior['release'], inputs=originals, remoteApplied=False, realRemoteJoin=False)
    (output / 'receipt.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    print(json.dumps({k: v for k, v in report.items() if k != 'inputs'}, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--inputs', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.inputs, args.baseline, args.output)
