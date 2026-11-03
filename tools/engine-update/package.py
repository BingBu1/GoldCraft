"""Build the two-DLL server delta from a verified domain handoff."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
NAMES = {'swds.dll', 'filesystem_stdio.dll'}


def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def plain(path):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError('Reparse path: ' + str(part))
    return path.resolve()


def identity(path):
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path))


def verify(row):
    path = plain(row['path'])
    if path.stat().st_size != row['bytes'] or sha(path) != row['sha256']:
        raise ValueError('Input identity mismatch: ' + str(path))
    return path


def build(handoff, output, revision):
    handoff, output = plain(handoff), plain(output)
    if not handoff.is_relative_to(ROOT) or not output.is_relative_to(ROOT) or output.exists():
        raise ValueError('Use a workspace handoff and a fresh workspace output')
    contract = json.loads(handoff.read_text('utf-8-sig'))
    if (contract['sourceCommit'] != revision or not contract['localDeploymentVerified'] or
            {Path(row['path']).name for row in contract['payload']} != NAMES or
            len(contract['payload']) != 2):
        raise ValueError('Wrong two-DLL handoff')
    for row in contract['evidence']:
        verify(row)
    sources = []
    for row in contract['payload']:
        path = verify(row)
        if not path.is_relative_to(ROOT):
            raise ValueError('Only workspace DLLs may be packaged')
        data = path.read_bytes()
        offset = struct.unpack_from('<I', data, 60)[0]
        if (data[:2] != b'MZ' or data[offset:offset+4] != b'PE\0\0' or
                struct.unpack_from('<H', data, offset+4)[0] != 0x14c or
                not struct.unpack_from('<H', data, offset+22)[0] & 0x2000):
            raise ValueError('Expected x86 DLL: ' + path.name)
        sources.append((path, row))
    package = output/'冰桑-服务器换图加速补丁'
    package.mkdir(parents=True)
    files = []
    for path, row in sources:
        target = package/'payload/cs'/path.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
        if sha(target) != row['sha256'] or target.stat().st_nlink != 1:
            raise ValueError('Independent payload copy failed')
        files.append(dict(path='cs/'+path.name, bytes=row['bytes'], sha256=row['sha256']))
    scripts = {
        'Update-Server.ps1': ROOT/'tools/engine-update/Update-Server.ps1',
        'Files.ps1': ROOT/'tools/scenario-update/Files.ps1',
        'Update-Server.cmd': ROOT/'tools/release-package/Update-Server.cmd',
        'Restore-Server.cmd': ROOT/'tools/release-package/Restore-Server.cmd',
        '使用说明.md': ROOT/'tools/engine-update/README.md',
    }
    for name, source in scripts.items():
        shutil.copy2(plain(source), package/name)
    manifest = dict(schema=1, scope='remote-engine-filesystem', release='20261103-DependencyCache',
                    sourceRevision=revision, files=sorted(files, key=lambda row: row['path']))
    (package/'update-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
    members = sorted(p for p in package.rglob('*') if p.is_file())
    (package/'SHA256SUMS.txt').write_text(''.join(
        sha(p)+'  '+p.relative_to(package).as_posix()+'\n' for p in members), 'utf-8')
    members.append(package/'SHA256SUMS.txt')
    archive = output/'冰桑-服务器换图加速补丁-20261103.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in members:
            z.write(path, package.name+'/'+path.relative_to(package).as_posix())
    with zipfile.ZipFile(archive) as z:
        if len(z.namelist()) != len(members):
            raise ValueError('Archive file count mismatch')
        for path in members:
            blob = z.read(package.name+'/'+path.relative_to(package).as_posix())
            if hashlib.sha256(blob).hexdigest() != sha(path):
                raise ValueError('Archive bytes mismatch')
    receipt = dict(archive=identity(archive), handoff=identity(handoff), payload=files,
                   payloadBytes=sum(r['bytes'] for r in files), archiveMembers=len(members),
                   scripts=[identity(p) for p in scripts.values()], package=str(package),
                   archiveContentVerified=True, remoteDeployed=False, installerTestsPending=True)
    (output/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    print(json.dumps({'archive': identity(archive), 'payloadFiles': len(files)}, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--handoff', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    args = parser.parse_args()
    build(args.handoff, args.output, args.revision)
