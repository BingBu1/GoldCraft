"""Package the deployed Nightfall runtime against issued, hash-checked releases.

This tool never builds gameplay code or writes the development installation.
Private input provenance stays in build; the delivery contains public identities.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('resource_package', ROOT/'tools/client-resources/package.py')
resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resources)
plain, sha = resources.plain, resources.sha
RELEASE = '20261115-Nightfall'
PINS = {
    'cs/swds.dll': '162ad0164a9d1b0ca8b480c671b7f69221462ea3d8342952164f145ea017c73c',
    'cs/filesystem_stdio.dll': '14bedc219a27811576faacddc316ae522e566c802f9c3852be1896277a7ac553',
    'cs/cstrike/dlls/mp.dll': '60ba4f3f929a4afc725fde721577a08ebb4ef086f8248fdf53dce73515dd891f',
}
CLIENT_PINS = {
    'cstrike/metahook/plugins/GoldCraft.dll': 'da264b158e2624dfa4d140044ec23b1e817bd011e64bcc8deee6b24aa9720d9b',
    'cstrike/metahook/plugins/CSOLSupport.dll': '2aac598552ed6ebb4eebcdfc207bc9caac32df0011d9898a5954e6b15a36694d',
    'cstrike/metahook/plugins/ParallelDownload.dll': '00affa3610e18656e92a087b687c3a8407d893a1e091a9a0e8a9246358aa1184',
}


def identity(path):
    path = plain(path)
    before = path.stat()
    digest = sha(path)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
        raise ValueError('Input changed while hashing: '+str(path))
    return dict(source=str(path), bytes=after.st_size, sha256=digest)


def public(row):
    return {k: row[k] for k in ('path', 'bytes', 'sha256', 'priorSha256', 'baselineRelease') if k in row}


def prepare(publication, evidence):
    publication, evidence = plain(publication), plain(evidence)
    if not evidence.is_relative_to(ROOT/'build') or evidence.exists():
        raise ValueError('Use a fresh build evidence directory')
    envelope = json.loads((publication/'manifest.json').read_text('utf-8-sig'))
    release = json.loads(base64.b64decode(envelope['payload']))
    if release['schema'] != 2 or release['sequence'] != 2026111501:
        raise ValueError('Wrong managed release')
    cores = {r['path']: r for r in release['files']}
    if any(cores.get(p, {}).get('sha256') != h for p, h in CLIENT_PINS.items()):
        raise ValueError('Unreviewed client core')
    history, archives = {}, []
    for archive in sorted((ROOT/'server-packages').glob('*/*.zip')):
        with zipfile.ZipFile(archive) as z:
            names = z.namelist()
            manifests = [n for n in names if n.endswith('/update-manifest.json')]
            publications = [n for n in names if n.endswith('/launcher-public/manifest.json')]
            if not manifests and not publications:
                continue
            archives.append(identity(archive))
            for name in manifests:
                manifest = json.loads(z.read(name))
                root = name.removesuffix('update-manifest.json')
                for row in manifest['files'] + manifest.get('requiredBaseline', []):
                    rel = row['path'].removeprefix('payload/')
                    entry = dict(path=rel, bytes=row['bytes'], sha256=row['sha256'],
                                 release=manifest.get('release', str(manifest.get('sequence'))))
                    member = root+'payload/'+rel
                    if member in names:
                        entry.update(archive=str(archive), member=member)
                    history.setdefault(rel.lower(), {})[row['sha256']] = entry
            for name in publications:
                old = json.loads(base64.b64decode(json.loads(z.read(name))['payload']))
                raw = z.read(name)
                entry = dict(path='cs/launcher-public/manifest.json', bytes=len(raw),
                             sha256=hashlib.sha256(raw).hexdigest(), archive=str(archive), member=name,
                             release=str(old['sequence']))
                history.setdefault(entry['path'], {})[entry['sha256']] = entry
                for row in old['files']:
                    rel = 'cs/launcher-public/blobs/'+row['sha256']+'/'+Path(row['path']).name
                    member = name.removesuffix('manifest.json')+'blobs/'+row['sha256']+'/'+Path(row['path']).name
                    entry = dict(path=rel, bytes=row['bytes'], sha256=row['sha256'], release=str(old['sequence']))
                    if member in names:
                        entry.update(archive=str(archive), member=member)
                    history.setdefault(rel.lower(), {})[row['sha256']] = entry
    server = ROOT/'sandbox/cs-server/Half-Life'
    game = server/'cstrike'
    files = {}

    def add(name, source):
        row = dict(path=name, **identity(source))
        key = name.lower()
        if key in files:
            if (files[key]['sha256'], files[key]['bytes']) != (row['sha256'], row['bytes']):
                raise ValueError('Conflicting package path: '+name)
            return
        if row['bytes'] <= 0:
            raise ValueError('Empty runtime resource: '+name)
        files[key] = row

    for folder, dirs, names in os.walk(game, followlinks=False):
        folder = plain(folder)
        if folder == game:
            dirs[:] = [d for d in dirs if d.lower() in resources.DIRECTORIES]
        for d in dirs:
            plain(folder/d)
        for name in names:
            path = folder/name
            rel = path.relative_to(game).as_posix()
            if resources.selected(rel) and rel != 'models/x_re/floating_damage.mdl':
                add('cs/cstrike/'+rel, path)
    for name, digest in PINS.items():
        add(name, server/name.removeprefix('cs/'))
        if files[name.lower()]['sha256'] != digest:
            raise ValueError('Deployed server differs from reviewed runtime: '+name)
    for rel in ('cstrike/delta.lst', 'cstrike/addons/sypb/sypb.dll',
                'cstrike/addons/metamod/metamod.dll', 'cstrike/addons/amxmodx/dlls/amxmodx_mm.dll'):
        add('cs/'+rel, server/rel)
    for directory, pattern in (('maps', '*.nav'), ('classes', '*.res'), ('classes2', '*.csv'), ('csol/weapons', '*.json'),
                               ('addons/amxmodx/modules', '*.dll'), ('addons/amxmodx/data/lang', '*.txt')):
        for path in sorted((game/directory).glob(pattern)):
            if path.name != 'floating_damage.txt':
                add('cs/cstrike/'+path.relative_to(game).as_posix(), path)
    # Preserve the issued production plugin inventory; never discover test plugins
    # from the live development server's temporary activation lists.
    plugins = sorted({r['path'].rsplit('/', 1)[1]
                      for versions in history.values() for r in versions.values()
                      if r['path'].startswith('cs/cstrike/addons/amxmodx/plugins/')
                      and r['path'].endswith('.amxx')})
    if not {'goldcraft_game_mode.amxx', 'goldcraft_scenario_admin.amxx'} <= set(plugins):
        raise ValueError('Issued production plugin baseline is incomplete')
    for name in plugins:
        if re.search(r'(test|probe|fixture|floating_damage)', name, re.I):
            raise ValueError('Non-production plugin')
        add('cs/cstrike/addons/amxmodx/plugins/'+name, game/'addons/amxmodx/plugins'/name)
    add('scripts/Start-Scenario.ps1', ROOT/'tools/scenario-update/Start-Scenario.ps1')
    for row in release['files']:
        rel = 'cs/launcher-public/blobs/'+row['sha256']+'/'+Path(row['path']).name
        add(rel, publication/rel.removeprefix('cs/launcher-public/'))
        if (files[rel.lower()]['sha256'], files[rel.lower()]['bytes']) != (row['sha256'], row['bytes']):
            raise ValueError('Signed blob mismatch')
    add('cs/launcher-public/manifest.json', publication/'manifest.json')
    payload, omitted = [], []
    for key, row in sorted(files.items()):
        previous = history.get(key, {})
        row['priorSha256'] = sorted(previous)
        same = previous.get(row['sha256'])
        if same and row['path'] not in PINS and row['path'] != 'cs/launcher-public/manifest.json':
            omitted.append(dict(row, baselineRelease=same['release']))
        else:
            payload.append(row)
    defaults = json.loads((game/'addons/goldcraft/config/gameplay.json').read_text('utf-8-sig'))
    defaults['features']['time_cycle']['enabled'] = True
    defaults['features']['cof_sprint']['comment'] = '历史键名保留兼容；完整CSOL绝命档案疾跑尚未交付，保持关闭。'
    if defaults['features']['time_cycle']['values']['midday_seconds']['value'] != 180 or defaults['features']['time_cycle']['values']['dusk_seconds']['value'] != 120:
        raise ValueError('Temporary stage timing must not enter the release')
    if any(value['enabled'] for key, value in defaults['features'].items() if key not in ('kick', 'time_cycle')):
        raise ValueError('Unfinished feature enabled')
    evidence.mkdir(parents=True)
    plan = dict(schema=1, release=RELEASE, scope='remote-deployed-delta',
                revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                pins=PINS, clientPins=CLIENT_PINS, files=payload, requiredBaseline=omitted,
                history=history, archives=archives, plugins=plugins, clientRelease=release,
                defaults=defaults, publication=str(publication), remoteStateInspected=False)
    (evidence/'plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    print(json.dumps(dict(payloadFiles=len(payload), payloadBytes=sum(r['bytes'] for r in payload),
                         omittedFiles=len(omitted), omittedBytes=sum(r['bytes'] for r in omitted))), flush=True)


def build(plan_path, output, refresh=False):
    plan_path, output = plain(plan_path), plain(output)
    if (output.exists() and not refresh) or not output.is_relative_to(ROOT/'server-packages'):
        raise ValueError('Use a fresh server-packages output')
    plan = json.loads(plan_path.read_text('utf-8'))
    package = output/('GoldCraft-Server-Update-'+RELEASE)
    package.mkdir(parents=True, exist_ok=refresh)
    linked = 0
    for i, row in enumerate(plan['files'], 1):
        source = plain(row['source'])
        if not source.is_relative_to(ROOT) or identity(source)['sha256'] != row['sha256']:
            raise ValueError('Input changed or escaped workspace: '+row['path'])
        dest = plain(package/'payload'/row['path'])
        dest.parent.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            if not refresh or sha(dest) != row['sha256']:
                raise ValueError('Existing staged payload differs; preserve for investigation')
            if os.path.samestat(source.stat(), dest.stat()):
                linked += row['bytes']
            continue
        immutable = source.suffix.lower() in {'.mdl', '.spr', '.seq', '.wav', '.mp3', '.tga', '.bmp', '.png', '.wad', '.bsp', '.nav', '.ttf', '.otf', '.cso'}
        if immutable and source.stat().st_nlink == 1:
            os.link(source, dest)
            if not os.path.samestat(source.stat(), dest.stat()):
                raise ValueError('Hard link identity mismatch')
            linked += row['bytes']
        else:
            shutil.copy2(source, dest)
        if sha(dest) != row['sha256']:
            raise ValueError('Staging mismatch')
        if i % 250 == 0:
            print(json.dumps(dict(phase='stage', files=i, total=len(plan['files']))), flush=True)
    release = plan['clientRelease']
    manifest = dict(schema=1, release=RELEASE, scope=plan['scope'], sourceRevision=plan['revision'],
                    files=[public(r) for r in plan['files']], requiredBaseline=[public(r) for r in plan['requiredBaseline']],
                    requiredPlugins=plan['plugins'], managedSequence=release['sequence'], bundleId=release['bundleId'])
    (package/'update-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    (package/'gameplay-defaults.json').write_text(json.dumps(plan['defaults'], ensure_ascii=False, indent=2)+'\n', 'utf-8')
    scripts = {
        'Update-Server.ps1': ROOT/'tools/current-server-update/Update-Server.ps1',
        'Files.ps1': ROOT/'tools/scenario-update/Files.ps1',
        'Scenario.ps1': ROOT/'tools/scenario-update/Configure.ps1',
        'Configuration.ps1': ROOT/'tools/current-server-update/Configuration.ps1',
        'Publication.ps1': HERE/'Publication.ps1',
        'Nightfall.ps1': HERE/'Configuration.ps1',
        'Hash.ps1': HERE/'Hash.ps1',
        'Update-Server.cmd': ROOT/'tools/release-package/Update-Server.cmd',
        'Restore-Server.cmd': ROOT/'tools/release-package/Restore-Server.cmd',
        'README.md': HERE/'README.md',
        'release-public-key.xml': ROOT/'tools/respawn-release/release-public-key.xml',
    }
    for name, source in scripts.items():
        text = source.read_text('utf-8-sig')
        if name == 'Update-Server.ps1':
            for old, new in [('20261107-Deployed', RELEASE), ('.bingsang-current-update/20261107', '.bingsang-current-update/'+RELEASE),
                             ("'5f869363a5e116ea01eaee924bd332f4521fba725e742569b7ce70c03c339549'", repr(PINS['cs/swds.dll'])),
                             ("'481b52d78644e9a89ec52b343abff70b0fb292f82f2e28f6aa6d2d3b640aeefd'", repr(PINS['cs/cstrike/dlls/mp.dll'])),
                             ("$Name -cin @('server-settings.json'", "$Name -cin @('cs/cstrike/addons/goldcraft/config/gameplay.json','server-settings.json'"),
                             (". (Join-Path $PSScriptRoot 'Publication.ps1')", ". (Join-Path $PSScriptRoot 'Publication.ps1')\n. (Join-Path $PSScriptRoot 'Nightfall.ps1')\n. (Join-Path $PSScriptRoot 'Hash.ps1')")]:
                if old not in text:
                    raise ValueError('Updater template changed: '+old)
                text = text.replace(old, new)
        if name == 'Configuration.ps1':
            old = '    return $changes'
            if text.count(old) != 1:
                raise ValueError('Configuration template changed')
            text = text.replace(old, '    Add-NightfallConfiguration $Root $changes\n'+old)
        (package/name).write_text(text, 'utf-8-sig' if name.endswith('.ps1') else 'utf-8', newline='\r\n')
    members = sorted(p for p in package.rglob('*') if p.is_file())
    sums = {p.relative_to(package).as_posix(): sha(p) for p in members}
    (package/'SHA256SUMS.txt').write_text(''.join(h+'  '+n+'\n' for n, h in sums.items()), 'utf-8')
    receipt = dict(package=str(package), plan=identity(plan_path), files=len(plan['files']),
                   requiredBaseline=len(plan['requiredBaseline']), stagedLinkedBytes=linked,
                   sourceInputs=[identity(p) for p in [HERE/'package.py', *scripts.values()]], sealed=False)
    (output/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    print(json.dumps(dict(package=str(package), files=receipt['files'], baseline=receipt['requiredBaseline'])), flush=True)


def prepare_archive(output):
    output = plain(output)
    receipt = json.loads((output/'receipt.json').read_text('utf-8'))
    package = plain(receipt['package'])
    expected = dict(line.split('  ', 1)[::-1] for line in (package/'SHA256SUMS.txt').read_text().splitlines())
    expected['SHA256SUMS.txt'] = sha(package/'SHA256SUMS.txt')
    actual = {p.relative_to(package).as_posix(): p for p in package.rglob('*') if p.is_file()}
    if set(actual) != set(expected):
        raise ValueError('Unexpected or missing package member')
    temp = output/(package.name+'.zip.partial')
    with zipfile.ZipFile(temp, 'x', zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as z:
        for i, (n, p) in enumerate(actual.items(), 1):
            if sha(p) != expected[n]:
                raise ValueError('Staged package changed: '+n)
            z.write(p, package.name+'/'+n)
            if i % 500 == 0:
                print(json.dumps(dict(phase='zip', files=i, total=len(actual))), flush=True)
    print(json.dumps(dict(partial=str(temp), sealed=False)), flush=True)


def seal(output, installer_receipt, client_receipt, hud_receipt):
    output = plain(output)
    receipt = json.loads((output/'receipt.json').read_text('utf-8'))
    package = plain(receipt['package'])
    installer = json.loads(Path(installer_receipt).read_text('utf-8-sig'))
    client = json.loads(Path(client_receipt).read_text('utf-8-sig'))
    hud = json.loads(Path(hud_receipt).read_text('utf-8-sig'))
    if not installer.get('passed') or 'Hash.ps1' not in installer.get('packageInputs', {}):
        raise ValueError('Final installer validation is missing')
    if any(sha(package/name) != h for name, h in installer['packageInputs'].items()):
        raise ValueError('Installer changed after validation')
    if not client.get('passed') or client.get('manifestSha256') != sha(package/'payload/cs/launcher-public/manifest.json'):
        raise ValueError('Real launcher upgrade validation is missing or stale')
    if not hud.get('success') or hud.get('resources') != 312:
        raise ValueError('Authoritative HUD closure validation is missing')
    expected = dict(line.split('  ', 1)[::-1] for line in (package/'SHA256SUMS.txt').read_text().splitlines())
    expected['SHA256SUMS.txt'] = sha(package/'SHA256SUMS.txt')
    actual = {p.relative_to(package).as_posix(): p for p in package.rglob('*') if p.is_file()}
    if set(actual) != set(expected):
        raise ValueError('Unexpected or missing package member')
    for n, p in actual.items():
        if sha(p) != expected[n]:
            raise ValueError('Package changed after validation: '+n)
    archive = output/(package.name+'.zip')
    temp = archive.with_suffix('.zip.partial')
    if not temp.exists():
        prepare_archive(output)
    with zipfile.ZipFile(temp) as z:
        if len(z.namelist()) != len(actual):
            raise ValueError('Duplicate archive member')
        for n, h in expected.items():
            with z.open(package.name+'/'+n) as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != h:
                    raise ValueError('Archive mismatch: '+n)
    os.replace(temp, archive)
    receipt.update(sealed=True, archive=identity(archive), allZipHashesVerified=True,
                   remoteDeployed=False, sourceRevision=json.loads(Path(receipt['plan']['source']).read_text('utf-8'))['revision'],
                   validations=[identity(p) for p in (installer_receipt, client_receipt, hud_receipt)])
    (output/'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', 'utf-8')
    print(json.dumps(receipt['archive']), flush=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    a = sub.add_parser('prepare')
    a.add_argument('--publication', type=Path, required=True)
    a.add_argument('--evidence', type=Path, required=True)
    b = sub.add_parser('build')
    b.add_argument('--plan', type=Path, required=True)
    b.add_argument('--output', type=Path, required=True)
    b.add_argument('--refresh', action='store_true')
    c = sub.add_parser('seal')
    c.add_argument('--output', type=Path, required=True)
    for name in ('installer-receipt', 'client-receipt', 'hud-receipt'):
        c.add_argument('--'+name, type=Path, required=True)
    d = sub.add_parser('prepare-archive')
    d.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    if args.action == 'prepare': prepare(args.publication, args.evidence)
    elif args.action == 'build': build(args.plan, args.output, args.refresh)
    elif args.action == 'prepare-archive': prepare_archive(args.output)
    else: seal(args.output, args.installer_receipt, args.client_receipt, args.hud_receipt)
