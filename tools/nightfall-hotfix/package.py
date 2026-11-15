#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ship reviewed crash/download fixes over the issued Nightfall baseline."""
import argparse
import base64
import copy
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RELEASE = '20261115-Nightfall-Hotfix1'
SEQUENCE = 2026111502
CONFIG = 'cs/cstrike/configs/guest/cof/scenario_damage.json'


def digest(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()


def plain(path):
    path = Path(path).absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError('Linked path: '+str(path))
    if not path.is_relative_to(ROOT):
        raise ValueError('Use workspace paths')
    return path


def identity(path):
    path = plain(path)
    before = path.stat()
    row = dict(source=str(path), bytes=before.st_size, sha256=digest(path))
    after = path.stat()
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
        raise ValueError('Input changed while hashing')
    return row


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def prepare(args):
    out, baseline = plain(args.evidence), plain(args.baseline)
    if out.exists() or not out.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh evidence directory')
    prior = read(baseline/'update-manifest.json')
    if prior['release'] != '20261115-Nightfall':
        raise ValueError('Wrong issued baseline')
    envelope_path = baseline/'payload/cs/launcher-public/manifest.json'
    release = json.loads(base64.b64decode(read(envelope_path)['payload']))
    if release['sequence'] != 2026111501 or release['schema'] != 2:
        raise ValueError('Wrong signed client baseline')
    release = copy.deepcopy(release)
    release.update(sequence=SEQUENCE, version='2026.11.15 Nightfall Hotfix 1')
    replacements = {
        'cstrike/metahook/plugins/GoldCraft.dll': plain(args.goldcraft),
        'cstrike/metahook/plugins/ParallelDownload.dll': plain(args.parallel),
    }
    sources, audit = [], [identity(envelope_path), identity(baseline/'update-manifest.json')]
    for row in release['files']:
        source = replacements.get(row['path'])
        if source is None:
            candidates = [baseline/'payload/cs/launcher-public/blobs'/row['sha256']/Path(row['path']).name,
                          ROOT/'sandbox/cs-server/Half-Life/launcher-public/blobs'/row['sha256']/Path(row['path']).name,
                          ROOT/'sandbox/cs-client-b/Half-Life'/row['path']]
            source = next((p for p in candidates if p.is_file() and digest(p) == row['sha256']), None)
            if source is None:
                raise ValueError('Missing sealed client core: '+row['path'])
        actual = identity(source)
        row.update(bytes=actual['bytes'], sha256=actual['sha256'])
        sources.append(dict(row, source=actual['source']))
        audit.append(actual)
    indexed = {row['path']: row for row in release['resources']}
    for source in sorted(plain(args.gamedata).glob('*.json')):
        name = 'cstrike/metahook/gamedata/goldcraft-precache/'+source.name
        if name not in indexed:
            raise ValueError('Unexpected new catalog path')
        actual = identity(source)
        indexed[name].update(bytes=actual['bytes'], sha256=actual['sha256'],
                             gzip=base64.b64encode(gzip.compress(source.read_bytes(), mtime=0)).decode())
        audit.append(actual)
    for row in release['resources']:
        data = gzip.decompress(base64.b64decode(row['gzip']))
        if len(data) != row['bytes'] or hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Invalid embedded client resource')
        if row['path'].endswith('/index.json'):
            for link in json.loads(data)['versions']:
                target = indexed[str(Path(row['path']).parent/link['url']).replace('\\', '/')]
                if (link['size'], link['sha256']) != (target['bytes'], target['sha256']):
                    raise ValueError('Mismatched symbol catalog')
    config = plain(args.config)
    table = read(config)
    if table['version'] != 2 or not {'glock', 'p345', 'revolver', 'vp70', 'tmp'} <= table['firearms'].keys():
        raise ValueError('Missing guest firearm bootstrap config')
    audit.extend([identity(config), identity(args.csx)])
    out.mkdir(parents=True)
    plan = dict(release=release, sources=sources, auditFiles=audit)
    (out/'client-plan.json').write_text(json.dumps(plan, indent=2)+'\n', 'utf-8')
    (out/'inputs.json').write_text(json.dumps(dict(baseline=str(baseline), csx=identity(args.csx),
        config=identity(config), revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
        text=True).strip()), indent=2)+'\n', 'utf-8')
    print('Prepared reviewed client release and guest bootstrap closure', flush=True)


def stage(args):
    evidence, publication, output = map(plain, (args.evidence, args.publication, args.output))
    inputs = read(evidence/'inputs.json')
    baseline = plain(inputs['baseline'])
    old = read(baseline/'update-manifest.json')
    rows = {row['path']: row for row in old['files']+old['requiredBaseline']}
    package = output/('GoldCraft-Server-Update-'+RELEASE)
    if package.exists():
        raise ValueError('Output already exists')
    package.mkdir(parents=True)
    changes = {}

    def add(name, source, prior=None):
        actual = identity(source)
        before = rows.get(name)
        row = dict(path=name, bytes=actual['bytes'], sha256=actual['sha256'],
                   priorSha256=[before['sha256']] if before else prior or [])
        if before and row['sha256'] == before['sha256']:
            return
        dest = package/'payload'/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        if digest(dest) != row['sha256']:
            raise ValueError('Changed staging source')
        changes[name] = row

    add('cs/cstrike/addons/amxmodx/modules/csx_amxx.dll', inputs['csx']['source'])
    add(CONFIG, inputs['config']['source'])
    release = json.loads(base64.b64decode(read(publication/'manifest.json')['payload']))
    if release['sequence'] != SEQUENCE:
        raise ValueError('Wrong signed release sequence')
    for row in release['files']:
        add('cs/launcher-public/blobs/'+row['sha256']+'/'+Path(row['path']).name,
            publication/'blobs'/row['sha256']/Path(row['path']).name)
    add('cs/launcher-public/manifest.json', publication/'manifest.json')
    omitted = [copy.deepcopy(row) for name, row in rows.items() if name not in changes]
    manifest = dict(schema=1, release=RELEASE, scope=old['scope'], sourceRevision=inputs['revision'],
        files=list(changes.values()), requiredBaseline=omitted, requiredPlugins=old['requiredPlugins'],
        managedSequence=SEQUENCE, bundleId=release['bundleId'])
    (package/'update-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
    for source in baseline.iterdir():
        if source.is_file() and source.name != 'update-manifest.json':
            shutil.copy2(source, package/source.name)
    updater = package/'Update-Server.ps1'
    text = updater.read_text('utf-8-sig').replace('20261115-Nightfall', RELEASE)
    marker = "    if($Name -cin @('scripts/Start-Scenario.ps1'"
    if marker not in text:
        raise ValueError('Updater payload scope changed')
    text = text.replace(marker, "    if($Name -ceq '"+CONFIG+"'){return $true}\n"+marker, 1)
    configuration = '$script:configuration=Get-ConfigurationChanges $target $manifest'
    if configuration not in text:
        raise ValueError('Updater configuration contract changed')
    # The issued Nightfall baseline already owns mode and gameplay setup.
    # This repair changes only manifest payloads, preserving operator settings.
    text = text.replace(configuration, '$script:configuration=@{}', 1)
    updater.write_text(text, 'utf-8-sig')
    (package/'README.md').write_text(
        '# 永夜紧急修复包\n\n适用已安装 20261115-Nightfall 的服务器。默认目录 '
        '`E:\\cs_MineCraft\\GoldCraft-Servers`。\n\n停服后解压到服务器目录外，双击 '
        '`Update-Server.cmd`，成功后用原 `Start.cmd` 启动。玩家用原受管启动器启动即可更新 DLL 和符号目录。\n\n'
        '修复 CSX 将怪物拥有者当玩家造成的崩溃，补齐导致整组客串武器拒购的服务端配置；'
        '资源修复升级为最多8路协程／128MiB总缓冲，后台校验落盘，暂停加载时维持原生连接，显示资源进度。\n\n'
        '只包含变化文件；省略的基线逐 SHA 检查。保留服务器端口、HTTP、管理员与数据库设置。'
        '未知旧文件会在覆盖前停止。恢复使用 `Restore-Server.cmd`。\n\n'
        '满体力连跳晚一拍仍待定位，本包不宣称已修复。正式服与多人尚未验证。\n', 'utf-8')
    archive = output/(package.name+'.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for source in sorted(package.rglob('*')):
            if source.is_file():
                z.write(source, package.name+'/'+source.relative_to(package).as_posix())
    with zipfile.ZipFile(archive) as z:
        for source in package.rglob('*'):
            if source.is_file() and hashlib.sha256(z.read(package.name+'/'+source.relative_to(package).as_posix())).hexdigest() != digest(source):
                raise ValueError('Archive byte mismatch')
    receipt = dict(package=str(package), archive=identity(archive), files=len(changes),
                   baseline=len(omitted), remoteDeployed=False, sourceCopies=0)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', 'utf-8')
    (output/'SHA256SUMS.txt').write_text(digest(archive)+'  '+archive.name+'\n', 'utf-8')
    print(json.dumps(receipt), flush=True)


def sign(args):
    evidence, output, bundle = map(plain, (args.evidence, args.publication, args.bundle))
    key = plain(args.key)
    if output.exists() or key.is_relative_to(output):
        raise ValueError('Use a new publication and an external local signing key')
    plan = read(evidence/'client-plan.json')
    for row in plan['auditFiles']:
        if digest(row['source']) != row['sha256']:
            raise ValueError('Prepared publication input changed')
    output.mkdir(parents=True)
    payload = json.dumps(plan['release'], separators=(',', ':'), ensure_ascii=False).encode()
    unsigned = evidence/'payload-to-sign.json'
    signature = evidence/'payload.signature'
    public = evidence/'release-public.pem'
    unsigned.write_bytes(payload)
    public.write_text(read(bundle)['publicKeyPem'], 'utf-8')
    subprocess.run(['openssl', 'dgst', '-sha256', '-sign', str(key), '-out', str(signature), str(unsigned)], check=True)
    subprocess.run(['openssl', 'dgst', '-sha256', '-verify', str(public), '-signature', str(signature), str(unsigned)], check=True)
    for row in plan['sources']:
        if digest(row['source']) != row['sha256']:
            raise ValueError('Signed core changed')
        dest = output/'blobs'/row['sha256']/Path(row['path']).name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(row['source'], dest)
        if digest(dest) != row['sha256']:
            raise ValueError('Signed core staging mismatch')
    envelope = dict(payload=base64.b64encode(payload).decode(), signature=base64.b64encode(signature.read_bytes()).decode())
    data = json.dumps(envelope, separators=(',', ':')).encode()
    if len(data) > 1024*1024:
        raise ValueError('Signed publication exceeds HTTP route limit')
    for row in plan['auditFiles']:
        if digest(row['source']) != row['sha256']:
            raise ValueError('Input changed before signed commit')
    (output/'manifest.json').write_bytes(data)
    print('Signed managed release; key remains outside publication', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('prepare')
    for name in ('baseline', 'evidence', 'goldcraft', 'parallel', 'csx', 'gamedata', 'config'):
        p.add_argument('--'+name, type=Path, required=True)
    p = sub.add_parser('stage')
    for name in ('evidence', 'publication', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p = sub.add_parser('sign')
    for name in ('evidence', 'publication', 'bundle', 'key'):
        p.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    {'prepare': prepare, 'sign': sign, 'stage': stage}[args.action](args)
