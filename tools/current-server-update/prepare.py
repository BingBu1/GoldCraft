"""Inventory a deployed server delta against previously issued package manifests.

Reads deployed resources directly. It neither builds native code nor copies a
source tree. Local paths and archived-package provenance stay outside the ZIP.
"""
from pathlib import Path
import argparse
import base64
import importlib.util
import json
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('resource_package', ROOT/'tools/client-resources/package.py')
resources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(resources)
sha, plain = resources.sha, resources.plain
PACKAGES = ('update-20261024', 'update-20261101-Scenario',
            'update-20261101-FastDL', 'update-20261101-Scenario-Resources-v3',
            'update-20261101-Respawn-v2', 'update-20261103-Engine')
PINS = {
    'cs/swds.dll': '5f869363a5e116ea01eaee924bd332f4521fba725e742569b7ce70c03c339549',
    'cs/filesystem_stdio.dll': '14bedc219a27811576faacddc316ae522e566c802f9c3852be1896277a7ac553',
    'cs/cstrike/dlls/mp.dll': '481b52d78644e9a89ec52b343abff70b0fb292f82f2e28f6aa6d2d3b640aeefd',
}
CLIENT_PINS = {
    'cstrike/metahook/plugins/CSOLSupport.dll': 'd585de84ff94d5e1b9ea561ef3586c801c83f6a0e1b7d0a2b6e635525691fe6c',
    'cstrike/metahook/plugins/GoldCraft.dll': 'daaf84f876fd57382e54f4eb09f2e4d6982dd43ed8e9b9e3e6d709bb618df16a',
}


def identity(path):
    path = plain(path)
    before = path.stat()
    digest = sha(path)
    after = path.stat()
    if (before.st_size, before.st_ino, before.st_mtime_ns) != (after.st_size, after.st_ino, after.st_mtime_ns):
        raise ValueError('Input changed while reading: ' + str(path))
    return dict(source=str(path), bytes=after.st_size, sha256=digest)


def x86(path):
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Expected x86 DLL')
    off = struct.unpack_from('<I', data, 60)[0]
    if off + 24 > len(data) or data[off:off+4] != b'PE\0\0' or struct.unpack_from('<H', data, off+4)[0] != 0x14c:
        raise ValueError('Expected x86 DLL')


def prepare(output, resource_plan):
    output, resource_plan = plain(output), plain(resource_plan)
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh workspace build evidence directory')
    output.mkdir(parents=True)
    history, archives, latest = {}, [], {}
    baseline_envelope = None
    for directory in PACKAGES:
        found = 0
        for archive_path in (ROOT/'server-packages'/directory).glob('*.zip'):
            with zipfile.ZipFile(archive_path) as archive:
                names = [n for n in archive.namelist() if n.endswith('update-manifest.json')]
                if len(names) != 1:
                    continue
                found += 1
                manifest = json.loads(archive.read(names[0]))
                root = names[0].removesuffix('update-manifest.json')
                archive_id = identity(archive_path)
                archives.append(dict(**archive_id, release=manifest['release']))
                for row in manifest['files']:
                    name = row['path'].removeprefix('payload/')
                    if not name.startswith(('cs/', 'scripts/')):
                        continue
                    entry = dict(path=name, bytes=row['bytes'], sha256=row['sha256'],
                                 archive=str(archive_path), member=root+'payload/'+name,
                                 release=manifest['release'])
                    if entry['member'] not in archive.namelist():
                        raise ValueError('Missing issued payload member: '+name)
                    history.setdefault(name.lower(), {})[row['sha256']] = entry
                    latest[name.lower()] = entry
                if directory == 'update-20261101-Respawn-v2':
                    baseline_envelope = archive.read(root+'payload/cs/launcher-public/manifest.json')
        if found != 1:
            raise ValueError('Expected one official server manifest in '+directory)
    (output/'baseline-publication.json').write_bytes(baseline_envelope)
    prior = json.loads(base64.b64decode(json.loads(baseline_envelope)['payload']))
    if prior['sequence'] != 2026110105 or len(prior['files']) != 19:
        raise ValueError('Unexpected signed baseline')
    client_root = ROOT/'sandbox/cs-client-b/Half-Life'
    client_changes = []
    for row in prior['files']:
        actual = identity(client_root/row['path'])
        expected = CLIENT_PINS.get(row['path'], row['sha256'])
        if actual['sha256'] != expected:
            raise ValueError('Unapproved client core difference: '+row['path'])
        x86(actual['source'])
        if row['path'] in CLIENT_PINS:
            client_changes.append(dict(path=row['path'], **actual))
    source_root = ROOT/'sandbox/cs-server/Half-Life'
    files = {}

    def add(name, source, expected=None):
        if name.lower() in files:
            raise ValueError('Duplicate server input: '+name)
        row = identity(source)
        if expected and row['sha256'] != expected:
            raise ValueError('Deployment pin changed: '+name)
        if name.endswith('.dll'):
            x86(source)
        files[name.lower()] = dict(path=name, **row)

    plan = json.loads(resource_plan.read_text('utf-8'))
    for row in plan['files']:
        if not resources.selected(row['path']) or row['path'].lower() == 'models/x_re/floating_damage.mdl':
            continue
        add('cs/cstrike/'+row['path'], row['source'], row['sha256'])
    for name, digest in PINS.items():
        add(name, source_root/name.removeprefix('cs/'), digest)
    for name in ('cstrike/delta.lst', 'cstrike/addons/sypb/sypb.dll'):
        add('cs/'+name, source_root/name)
    for directory, pattern in (('cstrike/classes', '*.res'), ('cstrike/classes2', '*.csv'),
                               ('cstrike/csol/weapons', '*.json'), ('cstrike/maps', '*.nav'),
                               ('cstrike/addons/amxmodx/modules', '*.dll'),
                               ('cstrike/addons/amxmodx/data/lang', '*.txt')):
        for path in sorted((source_root/directory).glob(pattern)):
            if path.name.lower() == 'floating_damage.txt':
                continue
            add('cs/'+path.relative_to(source_root).as_posix(), path)
    for name in ('cstrike/addons/metamod/metamod.dll', 'cstrike/addons/amxmodx/dlls/amxmodx_mm.dll'):
        add('cs/'+name, source_root/name)
    plugins = set()
    for config in (source_root/'cstrike/addons/amxmodx/configs').glob('plugins*.ini'):
        if re.search(r'(test|probe|live)', config.name, re.I):
            continue
        for line in config.read_text('utf-8-sig').splitlines():
            match = re.match(r'^\s*([\w.-]+\.amxx)\b([^;]*)', line)
            if not match or re.search(r'\bdisabled\b', match[2], re.I):
                continue
            name = match[1]
            if name == 'addon_floating_damage.amxx' or re.search(r'(test|probe|live|fixture)', name, re.I):
                continue
            plugins.add(name)
    for name in sorted(plugins):
        add('cs/cstrike/addons/amxmodx/plugins/'+name, source_root/'cstrike/addons/amxmodx/plugins'/name)
    # The scenario bootstrap is already issued and contains no local credentials.
    add('scripts/Start-Scenario.ps1', ROOT/'tools/scenario-update/Start-Scenario.ps1')
    payload, required = [], []
    for key, row in sorted(files.items()):
        previous = history.get(key, {})
        match = previous.get(row['sha256'])
        row['priorSha256'] = sorted(previous)
        if match and row['path'] not in PINS:
            required.append(dict(**row, baselineRelease=match['release']))
        else:
            payload.append(row)
    cycle=latest['cs/cstrike/bingsang_scenario_cycle.txt']
    required.append(dict(path=cycle['path'],bytes=cycle['bytes'],sha256=cycle['sha256'],
                         priorSha256=[cycle['sha256']],baselineRelease=cycle['release'],
                         archive=cycle['archive'],member=cycle['member']))
    result = dict(release='20261107-Deployed', scope='remote-deployed-delta', schema=1,
                  revision=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),
                  files=payload, requiredBaseline=required, archives=archives,
                  history=history, latest=latest, clientChanges=client_changes,
                  clientBaseline=prior, plugins=sorted(plugins), resourcePlan=identity(resource_plan),
                  remoteStateInspected=False)
    (output/'plan.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({'payload':len(payload),'payloadBytes':sum(r['bytes'] for r in payload),
                      'omitted':len(required),'omittedBytes':sum(r['bytes'] for r in required),
                      'plugins':len(plugins),'clientChanges':[r['path'] for r in client_changes]},ensure_ascii=False),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--resource-plan',type=Path,required=True)
    args = parser.parse_args()
    prepare(args.output,args.resource_plan)
