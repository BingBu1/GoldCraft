#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Ship reviewed crash/download fixes over the issued Nightfall baseline."""
import argparse
import base64
import copy
import gzip
import hashlib
import json
import re
import runpy
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RELEASE = '20261115-Nightfall-Hotfix1'
SEQUENCE = 2026111502
CONFIG = 'cs/cstrike/configs/guest/cof/scenario_damage.json'
MODE_PLUGIN = 'cs/cstrike/addons/amxmodx/plugins/goldcraft_game_mode.amxx'


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
    if not re.fullmatch(r'[0-9]{8}-[A-Za-z0-9-]+', args.release):
        raise ValueError('Invalid release name')
    out, baseline = plain(args.evidence), plain(args.baseline)
    if out.exists() or not out.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh evidence directory')
    prior = read(baseline/'update-manifest.json')
    if prior['release'] != args.baseline_release:
        raise ValueError('Wrong issued baseline')
    envelope_path = baseline/'payload/cs/launcher-public/manifest.json'
    release = json.loads(base64.b64decode(read(envelope_path)['payload']))
    if release['sequence'] != args.baseline_sequence or release['schema'] != 2 or args.sequence <= release['sequence']:
        raise ValueError('Wrong signed client baseline')
    release = copy.deepcopy(release)
    release.update(sequence=args.sequence, version=args.version)
    replacements = {
        'cstrike/metahook/plugins/GoldCraft.dll': plain(args.goldcraft),
        'cstrike/metahook/plugins/ParallelDownload.dll': plain(args.parallel),
    }
    core_baseline = plain(args.core_baseline) if args.core_baseline else None
    sources, audit = [], [identity(envelope_path), identity(baseline/'update-manifest.json')]
    for row in release['files']:
        source = replacements.get(row['path'])
        if source is None:
            candidates = [baseline/'payload/cs/launcher-public/blobs'/row['sha256']/Path(row['path']).name,
                          ROOT/'sandbox/cs-server/Half-Life/launcher-public/blobs'/row['sha256']/Path(row['path']).name,
                          ROOT/'sandbox/cs-client-b/Half-Life'/row['path']]
            if core_baseline:
                candidates.append(core_baseline/'blobs'/row['sha256']/Path(row['path']).name)
            source = next((p for p in candidates if p.is_file() and digest(p) == row['sha256']), None)
            if source is None:
                raise ValueError('Missing sealed client core: '+row['path'])
        actual = identity(source)
        row.update(bytes=actual['bytes'], sha256=actual['sha256'])
        sources.append(dict(row, source=actual['source']))
        audit.append(actual)
    indexed = {row['path']: row for row in release['resources']}
    catalogs = [('goldcraft-precache', plain(args.gamedata))] if args.gamedata else []
    if getattr(args, 'client_gamedata', None):
        catalogs.append(('goldcraft-cs', plain(args.client_gamedata)))
    if getattr(args, 'parallel_gamedata', None):
        catalogs.append(('parallel-download', plain(args.parallel_gamedata)))
    for prefix, directory in catalogs:
        for source in sorted(directory.glob('*.json')):
            name = 'cstrike/metahook/gamedata/'+prefix+'/'+source.name
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
    config_input = identity(config)
    previous_config = next((row for row in prior['files'] + prior['requiredBaseline'] if row['path'] == CONFIG), None)
    if not previous_config or previous_config['sha256'] != config_input['sha256']:
        table = read(config)
        if table['version'] != 2 or not {'glock', 'p345', 'revolver', 'vp70', 'tmp'} <= table['firearms'].keys():
            raise ValueError('Missing guest firearm bootstrap config')
    audit.extend([config_input, identity(args.csx)])
    server_dll = identity(args.server_dll) if args.server_dll else None
    if server_dll:
        audit.append(server_dll)
    mode_plugin = identity(args.mode_plugin) if getattr(args, 'mode_plugin', None) else None
    out.mkdir(parents=True)
    plan = dict(release=release, sources=sources, auditFiles=audit)
    (out/'client-plan.json').write_text(json.dumps(plan, indent=2)+'\n', 'utf-8')
    (out/'inputs.json').write_text(json.dumps(dict(baseline=str(baseline), csx=identity(args.csx),
        config=config_input, release=args.release, sequence=args.sequence,
        serverDll=server_dll, modePlugin=mode_plugin,
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
        text=True).strip()), indent=2)+'\n', 'utf-8')
    print('Prepared reviewed client release and guest bootstrap closure', flush=True)


def stage(args):
    evidence, publication, output = map(plain, (args.evidence, args.publication, args.output))
    inputs = read(evidence/'inputs.json')
    baseline = plain(inputs['baseline'])
    old = read(baseline/'update-manifest.json')
    release_name, sequence = inputs.get('release', RELEASE), inputs.get('sequence', SEQUENCE)
    rows = {row['path']: row for row in old['files']+old['requiredBaseline']}
    if len(rows) != len(old['files']) + len(old['requiredBaseline']):
        raise ValueError('Duplicate baseline member')
    package = output/('GoldCraft-Server-Update-'+release_name)
    plain(package)
    if not package.is_relative_to(output):
        raise ValueError('Release escapes output directory')
    if package.exists():
        raise ValueError('Output already exists')
    package.mkdir(parents=True)
    changes = {}

    def add(name, source, expected=None):
        actual = identity(source)
        if expected and (actual['sha256'], actual['bytes']) != (expected['sha256'], expected['bytes']):
            raise ValueError('Reviewed payload changed before staging: '+name)
        before = rows.get(name)
        row = dict(path=name, bytes=actual['bytes'], sha256=actual['sha256'],
                   priorSha256=[before['sha256']] if before else [])
        if before and row['sha256'] == before['sha256']:
            return
        dest = package/'payload'/name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)
        if digest(dest) != row['sha256']:
            raise ValueError('Changed staging source')
        changes[name] = row

    add('cs/cstrike/addons/amxmodx/modules/csx_amxx.dll', inputs['csx']['source'], inputs['csx'])
    if plugin := inputs.get('modePlugin'):
        if MODE_PLUGIN not in rows:
            raise ValueError('Missing map-mode plugin in server baseline')
        add(MODE_PLUGIN, plugin['source'], plugin)
    if inputs.get('serverDll'):
        add('cs/cstrike/dlls/mp.dll', inputs['serverDll']['source'], inputs['serverDll'])
    add(CONFIG, inputs['config']['source'], inputs['config'])
    release = json.loads(base64.b64decode(read(publication/'manifest.json')['payload']))
    if release['sequence'] != sequence:
        raise ValueError('Wrong signed release sequence')
    for row in release['files']:
        add('cs/launcher-public/blobs/'+row['sha256']+'/'+Path(row['path']).name,
            publication/'blobs'/row['sha256']/Path(row['path']).name, row)
    add('cs/launcher-public/manifest.json', publication/'manifest.json')
    if args.guest_bootstrap:
        guest = plain(args.guest_bootstrap)
        guest_manifest = read(guest/'update-manifest.json')
        allowed = {'cs/cstrike/aurora/guest/cof/gunsmoke.aur'} | {
            'cs/cstrike/inventoryitems/guest/cof/weapons/weapon_'+name+'.txt'
            for name in ('glock', 'p345', 'revolver', 'vp70', 'tmp')}
        if guest_manifest['release'] != '20261115-Nightfall-GuestBootstrap' or {
                row['path'] for row in guest_manifest['files']} != allowed or len(guest_manifest['files']) != 6:
            raise ValueError('Wrong sealed guest bootstrap')
        for row in guest_manifest['files']:
            source = guest/'payload'/row['path']
            if source.stat().st_size != row['bytes'] or digest(source) != row['sha256']:
                raise ValueError('Sealed guest script changed')
            add(row['path'], source)
    compatible = []
    for directory in args.compatible_baseline:
        prior_manifest = plain(directory) / 'update-manifest.json'
        prior = read(prior_manifest)
        if prior.get('schema') != 1 or prior.get('scope') != old['scope'] or prior.get('release') == release_name:
            raise ValueError('Invalid compatible issued baseline')
        compatible.append(identity(prior_manifest))
        for row in prior['files']:
            target = changes.get(row['path'])
            if target and row['sha256'] != target['sha256'] and row['sha256'] not in target['priorSha256']:
                target['priorSha256'].append(row['sha256'])
    omitted = [copy.deepcopy(row) for name, row in rows.items() if name not in changes]
    manifest = dict(schema=1, release=release_name, scope=old['scope'], sourceRevision=inputs['revision'],
        files=list(changes.values()), requiredBaseline=omitted, requiredPlugins=old['requiredPlugins'],
        managedSequence=sequence, bundleId=release['bundleId'])
    (package/'update-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', 'utf-8')
    for source in baseline.iterdir():
        if source.is_file() and source.name != 'update-manifest.json':
            shutil.copy2(source, package/source.name)
    updater = package/'Update-Server.ps1'
    text = updater.read_text('utf-8-sig').replace(old['release'], release_name)
    if inputs.get('serverDll'):
        pin = "'cs/cstrike/dlls/mp.dll'='" + rows['cs/cstrike/dlls/mp.dll']['sha256'] + "'"
        if text.count(pin) != 1:
            raise ValueError('Baseline updater MP pin changed')
        text = text.replace(pin, "'cs/cstrike/dlls/mp.dll'='" + inputs['serverDll']['sha256'] + "'", 1)
    marker = "    if($Name -cin @('scripts/Start-Scenario.ps1'"
    if marker not in text:
        raise ValueError('Updater payload scope changed')
    config_scope = "    if($Name -ceq '"+CONFIG+"'){return $true}"
    if config_scope not in text:
        text = text.replace(marker, config_scope+"\n"+marker, 1)
    if args.guest_bootstrap:
        marker = 'function Payload-Path([string]$Name){'
        if text.count(marker) != 1:
            raise ValueError('Unexpected guest payload scope')
        text = text.replace(marker, marker+"\n    if($Name -cmatch '^cs/cstrike/(?:aurora/guest/cof/[A-Za-z0-9_/-]+\\.aur|inventoryitems/guest/cof/[A-Za-z0-9_/-]+\\.txt)$'){return $true}", 1)
    configuration = '$script:configuration=Get-ConfigurationChanges $target $manifest'
    if configuration not in text and '$script:configuration=@{}' not in text:
        raise ValueError('Updater configuration contract changed')
    # The issued Nightfall baseline already owns mode and gameplay setup.
    # This repair changes only manifest payloads, preserving operator settings.
    text = text.replace(configuration, '$script:configuration=@{}', 1)
    updater.write_text(text, 'utf-8-sig')
    readme = (
        '# 永夜紧急修复包\n\n适用已安装 20261115-Nightfall 的服务器。默认目录 '
        '`E:\\cs_MineCraft\\GoldCraft-Servers`。\n\n解压到服务器目录外，双击 '
        '`Update-Server.cmd`；预检通过后自动保存停服、安装并恢复原先运行的服务，原本已停服则保持停止。玩家用原受管启动器启动即可更新 DLL 和符号目录。\n\n'
        '修复 CSX 将怪物拥有者当玩家造成的崩溃，补齐导致整组客串武器拒购的服务端配置；'
        '资源修复升级为最多8路协程／128MiB总缓冲，后台校验落盘，暂停加载时维持原生连接，显示资源进度。\n\n'
        '只包含变化文件；省略的基线逐 SHA 检查。保留服务器端口、HTTP、管理员与数据库设置。'
        '未知旧文件会在覆盖前停止。恢复使用 `Restore-Server.cmd`。\n\n'
        '满体力连跳晚一拍仍待定位，本包不宣称已修复。正式服与多人尚未验证。\n')
    if args.readme:
        readme = plain(args.readme).read_text('utf-8-sig')
    (package/'README.md').write_text(readme, 'utf-8')
    # Share the graceful lifecycle before hashing or sealing new packages.
    runpy.run_path(str(ROOT/'tools/server-update-lifecycle/package.py'))['enable'](package, refresh_sums=True)
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
                   baseline=len(omitted), compatibleIssuedManifests=compatible,
                   remoteDeployed=False, sourceCopies=0)
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', 'utf-8')
    (output/'SHA256SUMS.txt').write_text(digest(archive)+'  '+archive.name+'\n', 'utf-8')
    print(json.dumps(receipt), flush=True)


def sign(args):
    evidence, output, bundle = map(plain, (args.evidence, args.publication, args.bundle))
    key = plain(args.key)
    if output.exists() or key.is_relative_to(output):
        raise ValueError('Use a new publication and an external local signing key')
    plan = read(evidence/'client-plan.json')
    if read(bundle)['bundleId'] != plan['release']['bundleId']:
        raise ValueError('Signing bundle differs from issued client baseline')
    validators = runpy.run_path(str(ROOT/'tools/client-launcher/managed_release.py'))
    validators['verify_renderer_shader'](plan['release'], plan['sources'])
    validators['verify_catalogs'](plan['release'], plan['sources'])
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
    for name in ('baseline', 'evidence', 'goldcraft', 'parallel', 'csx', 'config'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--gamedata', type=Path, help='Replacement GoldCraft precache catalogs')
    p.add_argument('--baseline-release', default='20261115-Nightfall')
    p.add_argument('--core-baseline', type=Path, help='Issued publication containing omitted immutable core blobs')
    p.add_argument('--baseline-sequence', type=int, default=2026111501)
    p.add_argument('--release', default=RELEASE)
    p.add_argument('--sequence', type=int, default=SEQUENCE)
    p.add_argument('--version', default='2026.11.15 Nightfall Hotfix 1')
    p.add_argument('--server-dll', type=Path, help='Optional reviewed authoritative snapshot hotfix')
    p.add_argument('--mode-plugin', type=Path, help='Compiled replacement for the existing map-mode plugin')
    p.add_argument('--parallel-gamedata', type=Path, help='Matching exact-engine downloader catalogs')
    p.add_argument('--client-gamedata', type=Path, help='Matching CS client symbol catalog and index')
    p = sub.add_parser('stage')
    for name in ('evidence', 'publication', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--guest-bootstrap', type=Path)
    p.add_argument('--readme', type=Path)
    p.add_argument('--compatible-baseline', type=Path, action='append', default=[],
                   help='Issued patch directory whose changed-file hashes may already be installed')
    p = sub.add_parser('sign')
    for name in ('evidence', 'publication', 'bundle', 'key'):
        p.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    {'prepare': prepare, 'sign': sign, 'stage': stage}[args.action](args)
