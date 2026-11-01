"""Build the exact reviewed revival update and a separate client publication ZIP."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[2]
OLD_MP = 'a3e32ab1fe182525c01129adbf455f2a423bde44d6a5e24bf9e53862f0258ec5'
NEW_MP = 'cfd2e0968a7e8f303bd1ee8d037a2ba58fd8cbd7f0fed381c682fba200c827b5'
ENGINE = '6839933a4dfe5dc8b36185406eddaa15e4957b57008120844886de6ba00a3e3f'
CLIENT = '6881e8b9ce604b3e529253eb5730fe6983e4dde4058960a91880ed5da167e187'
PLAN = 'dfa3371066527ca19bdba9a393511d56cd82def24f79d2211adb909f62684ef9'


def plain(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.is_symlink() or item.is_junction():
            raise ValueError('Reparse path: ' + str(item))
    return path


def digest(path):
    with plain(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def identity(path):
    path = plain(path)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest(path))


def relative(value):
    if (not value or any(c in value for c in '\\:<>"|?*') or
            any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in value.split('/'))):
        raise ValueError('Invalid payload path')
    return value


def copy_verified(source, target, expected):
    source, target = plain(source), plain(target)
    if digest(source) != expected:
        raise ValueError('Input changed: ' + str(source))
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    if digest(target) != expected or target.stat().st_nlink != 1 or source.samefile(target):
        raise ValueError('Independent staging failed: ' + str(target))


def archive(package, target):
    files = sorted(p for p in package.rglob('*') if p.is_file())
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for path in files:
            output.write(path, package.name + '/' + path.relative_to(package).as_posix())
    with zipfile.ZipFile(target) as output:
        expected = {package.name + '/' + p.relative_to(package).as_posix(): p for p in files}
        if set(output.namelist()) != set(expected):
            raise ValueError('Unlisted ZIP member')
        for name, path in expected.items():
            with output.open(name) as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            if actual != digest(path):
                raise ValueError('ZIP member differs: ' + name)
    return dict(identity(target), members=len(files))


def build(plan_path, artifacts_path, output):
    plan_path, artifacts_path, output = map(plain, (plan_path, artifacts_path, output))
    if output.exists() or not output.is_relative_to(ROOT / 'build/csol-respawn-delivery-20261101'):
        raise ValueError('Use a fresh owned build output directory')
    if digest(plan_path) != PLAN:
        raise ValueError('Not the reviewed cumulative resource plan')
    plan = json.loads(plan_path.read_text('utf-8-sig'))
    artifacts = json.loads(artifacts_path.read_text('utf-8-sig'))
    if artifacts['resourcePlan']['sha256'] != PLAN:
        raise ValueError('Products do not bind the reviewed resource plan')
    products = {row['role']: row for row in artifacts['artifacts']}
    for role, expected in [('server', NEW_MP), ('client', CLIENT)]:
        product = products[role]['source']
        if product['sha256'] != expected or digest(product['path']) != expected:
            raise ValueError('Wrong final product: ' + role)
        data = Path(product['path']).read_bytes()
        offset = struct.unpack_from('<I', data, 0x3c)[0]
        if data[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H', data, offset+4)[0] != 0x14c:
            raise ValueError('Expected real x86 PE DLL')
    if len(plan['files']) != 77 or len(plan['requiredBaseline']) != 1429 or plan['replacements']:
        raise ValueError('Unexpected resource scope')
    output.mkdir(parents=True)
    package = output / '冰桑-灾变复活累计更新'
    files = []
    for row in plan['files']:
        name = relative(row['path'])
        if not name.startswith('cs/cstrike/') or Path(name).suffix.lower() not in ('.tga', '.wav', '.spr'):
            raise ValueError('Non-resource payload in resource plan')
        target = package / 'payload' / name
        copy_verified(row['source'], target, row['sha256'])
        if target.stat().st_size != row['bytes']:
            raise ValueError('Input size changed')
        files.append({key: row[key] for key in ('path', 'bytes', 'sha256')})
    target = package / 'payload/cs/cstrike/dlls/mp.dll'
    copy_verified(products['server']['source']['path'], target, NEW_MP)
    files.append(dict(path='cs/cstrike/dlls/mp.dll', bytes=target.stat().st_size, sha256=NEW_MP))
    manifest = dict(schema=1, scope='remote-scenario-respawn', release='20261101-Scenario-Respawn',
                    baselineMpSha256=OLD_MP, engineSha256=ENGINE,
                    files=sorted(files, key=lambda row: row['path'].lower()),
                    requiredBaseline=[{key: row[key] for key in ('path', 'bytes', 'sha256')}
                                      for row in plan['requiredBaseline']])
    (package / 'update-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    scripts = {'Update-Server.ps1': ROOT/'tools/scenario-respawn-update/Update-Server.ps1',
               'Update-Server.cmd': ROOT/'tools/release-package/Update-Server.cmd',
               'Restore-Server.cmd': ROOT/'tools/release-package/Restore-Server.cmd',
               'Files.ps1': ROOT/'tools/scenario-update/Files.ps1',
               '使用说明.md': ROOT/'tools/scenario-respawn-update/README.md'}
    for name, source in scripts.items():
        copy_verified(source, package/name, digest(source))
    sums = ''.join(digest(path)+'  '+path.relative_to(package).as_posix()+'\n'
                   for path in sorted(package.rglob('*')) if path.is_file())
    (package/'SHA256SUMS.txt').write_text(sums, 'utf-8')
    server_zip = archive(package, output/'冰桑-灾变复活累计更新-20261101.zip')

    client = output/'冰桑-复活HUD配套客户端发布文件'
    copy_verified(products['client']['source']['path'], client/'cstrike/metahook/plugins/CSOLSupport.dll', CLIENT)
    copy_verified(ROOT/'tools/scenario-respawn-update/CLIENT.md', client/'使用说明.md', digest(ROOT/'tools/scenario-respawn-update/CLIENT.md'))
    client_sums = ''.join(digest(path)+'  '+path.relative_to(client).as_posix()+'\n'
                          for path in sorted(client.rglob('*')) if path.is_file())
    (client/'SHA256SUMS.txt').write_text(client_sums, 'utf-8')
    client_zip = archive(client, output/'冰桑-复活HUD配套客户端发布文件-20261101.zip')
    receipt = dict(plan=identity(plan_path), artifacts=identity(artifacts_path),
                   package=str(package), manifest=identity(package/'update-manifest.json'),
                   sourceScripts=[identity(path) for path in scripts.values()],
                   archives={'server': server_zip, 'client': client_zip},
                   files=files, payloadFiles=len(files), payloadBytes=sum(row['bytes'] for row in files),
                   requiredBaselineFiles=1429, sourceHardLinksUsed=False,
                   clientSignatureChanged=False, remoteDeployed=False, installerTestsPending=True)
    (output/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    print(json.dumps(dict(payloadFiles=len(files), payloadBytes=receipt['payloadBytes'],
                         serverZip=server_zip, clientZip=client_zip), ensure_ascii=True))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('plan', 'artifacts', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    build(args.plan, args.artifacts, args.output)
