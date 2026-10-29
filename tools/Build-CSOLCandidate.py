"""Build receipts and apply a reviewed CSOL candidate inside the workspace."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import urllib.parse
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parent.parent
SOURCE_DIRS = ('native/csol', 'native/engine', 'native/include', 'native/server', 'native/common',
               'native/amxx', 'external/ReHLDS/rehlds', 'external/ReHLDS/dep',
               'external/ReGameDLL_CS/regamedll', 'external/ReGameDLL_CS/dep',
               'external/MetaHookSv-20261007/MetaHook/include',
               'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include',
               'external/MetaHookSv-20261007/thirdparty/glew_fork', 'external/amxmodx/public',
               'amxx/game_modes', 'amxx/scenario', 'cmake')
SOURCE_EXTENSIONS = {'.cpp', '.c', '.h', '.hpp', '.inc', '.sma', '.cmake', '.targets', '.vcxproj'}


def safe(path, base=ROOT):
    path = Path(os.path.abspath(path))
    base = Path(os.path.abspath(base))
    if path == base or not path.is_relative_to(base):
        raise ValueError(f'Path must be beneath {base}: {path}')
    for node in (path, *path.parents):
        if os.path.lexists(node) and (node.is_symlink() or
                getattr(node.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT):
            raise ValueError(f'Reparse point refused: {node}')
    if not path.resolve().is_relative_to(base.resolve()):
        raise ValueError(f'Resolved path escaped root: {path}')
    return path


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def source_path(path, resource_root=None):
    path = Path(os.path.abspath(path))
    return safe(path, resource_root) if resource_root and not path.is_relative_to(ROOT) else safe(path)


def identity(path, resource_root=None):
    path = source_path(path, resource_root)
    stat = path.stat()
    return dict(path=str(path), bytes=stat.st_size, sha256=sha(path), nlink=stat.st_nlink)


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_new(path, value):
    path = safe(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def source_files():
    paths = {ROOT / 'CMakeLists.txt', ROOT / 'amxbuild.yml', ROOT / 'sources.lock.json',
             ROOT / 'tools/ClangToolchain.ps1', ROOT / 'tools/SandboxPaths.ps1',
             ROOT / 'tools/Inspect-EngineIdentity.py', ROOT / 'native/csol/sequence_engine_10210.json',
             ROOT / 'native/client/precache_10210.json', ROOT / 'native/client/userinfo_10210.json'}
    paths.update((ROOT / 'tools').glob('Build-*'))
    for directory in SOURCE_DIRS:
        paths.update(path for path in (ROOT / directory).rglob('*') if path.is_file()
                     and (path.suffix.lower() in SOURCE_EXTENSIONS or path.name == 'CMakeLists.txt'))
    return sorted(paths)


def begin(output):
    if output.exists():
        raise ValueError('Fresh output directory required')
    rows = [identity(path) for path in source_files()]
    if any(row['nlink'] != 1 for row in rows):
        raise ValueError('Build source must be independent, not hard linked')
    write_new(output / 'source-inputs.json', rows)
    print(json.dumps(dict(prepared=True, sourceFiles=len(rows), sourceCopies=0)))


def check_sources(output):
    rows = load(output / 'source-inputs.json')
    if {str(path) for path in source_files()} != {row['path'] for row in rows}:
        raise ValueError('Build source file set changed')
    for row in rows:
        if identity(Path(row['path'])) != row:
            raise ValueError(f'Build source changed: {row["path"]}')


def pe_x86(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'Not a PE module: {path}')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H', data, offset+4)[0] != 0x14c or struct.unpack_from('<H', data, offset+24)[0] != 0x10b:
        raise ValueError(f'Expected Windows x86 PE32: {path}')


def seal(output):
    check_sources(output)
    modules = ['engine/swds.dll', 'engine/hlds.exe', 'engine/filesystem_stdio.dll',
               'server/regamedll/mp.dll', 'server/common/goldcraft_amxx.dll', 'client/CSOLSupport.dll']
    artifacts = []
    for relative in modules:
        path = safe(output / relative, output)
        pe_x86(path)
        row = identity(path)
        if row['nlink'] != 1:
            raise ValueError(f'Build artifact is shared: {path}')
        artifacts.append(row)
    for name in ('goldcraft_game_mode', 'goldcraft_scenario_admin'):
        path = output / 'amxx/plugins' / (name + '.amxx')
        if not path.stat().st_size:
            raise ValueError(f'Empty Pawn bytecode: {path}')
        artifacts.append(identity(path))
    for name in ('goldcraft-precache', 'goldcraft-cs'):
        index = output / 'gamedata' / name / 'index.json'
        payload = load(index)
        if payload['schemaVersion'] != 4 or len(payload['versions']) != 1:
            raise ValueError(f'Unexpected catalog index: {index}')
        entry = payload['versions'][0]
        snapshot = safe(index.parent / entry['url'], index.parent)
        if snapshot.stat().st_size != entry['size'] or sha(snapshot) != entry['sha256']:
            raise ValueError(f'Catalog index mismatch: {snapshot}')
        records = load(snapshot)['records']
        if len(records) != entry['fileCount'] or len({row['symbolName'] for row in records}) != len(records):
            raise ValueError(f'Duplicate/incomplete catalog: {snapshot}')
        artifacts += [identity(index), identity(snapshot)]
    write_new(output / 'candidate.json', dict(schemaVersion=1, sourceIdentity=identity(output / 'source-inputs.json'),
              artifacts=artifacts, validation='Fresh full compilation/link and artifact checks only; no deployment or live gameplay.'))
    print(json.dumps(dict(candidate=True, artifacts=len(artifacts), modules=len(modules), deployed=False)))


def relative_resource(value):
    path = Path(value)
    if path.is_absolute() or path.drive or '..' in path.parts or ':' in value or '\\' in value:
        raise ValueError(f'Invalid resource path: {value}')
    if not path.parts or path.parts[0].lower() not in ('models', 'sprites', 'sound', 'materials', 'resource', 'maps', 'events', 'gfx'):
        raise ValueError(f'Resource destination outside game resource directories: {value}')
    return path


def plan(output, resource_plan, server, client, http_url, http_port, resource_root=None):
    candidate = load(output / 'candidate.json')
    for row in candidate['artifacts']:
        if identity(Path(row['path'])) != row:
            raise ValueError(f'Candidate artifact changed: {row["path"]}')
    server, client = safe(server, ROOT / 'sandbox'), safe(client, ROOT / 'sandbox')
    if server == client or not (server / 'hlds.exe').is_file() or not (client / 'hw.dll').is_file():
        raise ValueError('Expected distinct initialized dedicated server and client roots')
    if not re.fullmatch(r'http://[A-Za-z0-9.-]+(?::[0-9]{1,5})?/goldcraft/', http_url) or len(http_url) > 127:
        raise ValueError('Expected the actual HTTP /goldcraft/ URL')
    port = urllib.parse.urlsplit(http_url).port
    if port is not None and not 1 <= port <= 65535:
        raise ValueError('Invalid public HTTP port')
    resources = load(safe(resource_plan))
    if resources.get('blocked'):
        raise ValueError('Resource plan still contains blocked resources')
    rows, destinations = [], set()

    def add(source, target, category):
        source = source_path(source, resource_root if category == 'resource' else None)
        target = safe(target, ROOT / 'sandbox')
        key = str(target).lower()
        if key in destinations:
            raise ValueError(f'Duplicate deployment destination: {target}')
        destinations.add(key)
        rows.append(dict(source=identity(source, resource_root if category == 'resource' else None), target=str(target), category=category,
                         previous=identity(target) if target.exists() else None))

    runtime = {'engine/swds.dll': server / 'swds.dll', 'engine/hlds.exe': server / 'hlds.exe',
        'engine/filesystem_stdio.dll': server / 'filesystem_stdio.dll',
        'server/regamedll/mp.dll': server / 'cstrike/dlls/mp.dll',
        'server/common/goldcraft_amxx.dll': server / 'cstrike/addons/amxmodx/modules/goldcraft_amxx.dll',
        'client/CSOLSupport.dll': client / 'cstrike/metahook/plugins/CSOLSupport.dll'}
    for relative, target in runtime.items():
        add(output / relative, target, 'native')
        symbols = (output / relative).with_suffix('.pdb')
        if symbols.exists():
            add(symbols, target.with_suffix('.pdb'), 'symbols')
    for name in ('goldcraft_game_mode', 'goldcraft_scenario_admin'):
        add(output / 'amxx/plugins' / (name + '.amxx'), server / 'cstrike/addons/amxmodx/plugins' / (name + '.amxx'), 'pawn')
    for path in sorted((output / 'gamedata').rglob('*.json')):
        add(path, client / 'cstrike/metahook/gamedata' / path.relative_to(output / 'gamedata'), 'catalog')
    for row in resources['files']:
        source = source_path(row['source'], resource_root)
        if source.stat().st_size != row['bytes'] or sha(source) != row['sha256']:
            raise ValueError(f'Resource input changed: {source}')
        add(source, server / 'cstrike' / relative_resource(row['destination']), 'resource')
    game_modes = ROOT / 'amxx/game_modes/configs/game_modes.ini'
    add(game_modes, server / 'cstrike/addons/amxmodx/configs/goldcraft/game_modes.ini', 'configuration')
    add(ROOT / 'amxx/scenario/configs/scenario_admin.cfg', server / 'cstrike/addons/amxmodx/configs/scenario_admin.cfg', 'configuration')

    configs = server / 'cstrike/addons/amxmodx/configs'
    active = [path for path in configs.glob('plugins*.ini') if path.name == 'plugins.ini' or path.name.startswith('plugins-')]
    admin = re.compile(rb'^\s*goldcraft_scenario_admin\.amxx(?:[ \t]|$)', re.MULTILINE)
    registrations = [path for path in active if admin.search(path.read_bytes())]
    if len(registrations) > 1 or any(re.search(rb'^\s*goldcraft_scenario_admin\.amxx\s+disabled\b', path.read_bytes(), re.MULTILINE) for path in registrations):
        raise ValueError('Scenario admin is duplicate or explicitly disabled in active plugin lists')
    if not registrations:
        main = configs / 'plugins.ini'
        text = main.read_bytes()
        match = re.search(rb'^\s*goldcraft_game_mode\.amxx[^\r\n]*(?:\r?\n|$)', text, re.MULTILINE)
        if not match:
            raise ValueError('Mode gate must be first registered before adding scenario admin')
        newline = b'\r\n' if b'\r\n' in text else b'\n'
        insertion = (b'' if match[0].endswith(b'\n') else newline) + b'goldcraft_scenario_admin.amxx debug' + newline
        staged = output / 'configuration/plugins.ini'
        staged.parent.mkdir(exist_ok=True)
        with staged.open('xb') as stream:
            stream.write(text[:match.end()] + insertion + text[match.end():])
        add(staged, main, 'configuration')

    # The standard launcher validates this receipt before starting hlds.
    receipt_path = server.parent / 'rehlds-deployment.json'
    receipt = load(receipt_path)
    mapping = {str(Path(row['target'])).lower(): row['source'] for row in rows}
    for row in receipt['files']:
        target = safe(server / row['name'], server)
        current = mapping.get(str(target).lower(), identity(target))
        row.update(bytes=current['bytes'], sha256=current['sha256'])
    receipt['candidateSourceIdentity'] = candidate['sourceIdentity']['sha256']
    staged = output / 'configuration/rehlds-deployment.json'
    write_new(staged, receipt)
    add(staged, receipt_path, 'launcher-receipt')
    write_new(output / 'deployment-plan.json', dict(schemaVersion=1, candidate=identity(output / 'candidate.json'),
        resourcePlan=identity(resource_plan), resourceRoot=str(Path(resource_root).resolve()) if resource_root else None,
        server=str(server), client=str(client), files=rows,
        http=dict(url=http_url, port=http_port, bind='127.0.0.1', builtIn=True),
        note='Resources go only to server cstrike. Actual HTTP registration/downloads require the running dedicated server.'))
    write_new(output / 'launch-environment.json', dict(GOLDCRAFT_HTTP_ENABLED='1', GOLDCRAFT_HTTP_BIND='127.0.0.1',
        GOLDCRAFT_HTTP_PORT=str(http_port), GOLDCRAFT_HTTP_PUBLIC_URL=http_url))
    print(json.dumps(dict(planned=len(rows), resourceFiles=len(resources['files']), deployed=False)))


def atomic_copy(source, target, resource_root=None):
    source, target = source_path(source, resource_root), safe(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and target.stat().st_nlink > 1:
        detach = ROOT / 'build/hardlinks-20261027/deduplicate.py'
        subprocess.run([sys.executable, '-B', str(detach), 'detach', str(target)], check=True, capture_output=True)
        if target.stat().st_nlink != 1:
            raise ValueError(f'Detach did not produce independent destination: {target}')
    temporary = target.with_name(target.name + '.' + uuid.uuid4().hex + '.candidate')
    try:
        shutil.copy2(source, temporary)
        if temporary.stat().st_nlink != 1 or sha(temporary) != sha(source):
            raise ValueError(f'Independent temporary copy verification failed: {target}')
        if target.exists() and os.name == 'nt':
            # ReplaceFile preserves the existing destination DACL. The old
            # shared inode has already been detached without altering its peers.
            replace = ctypes.windll.kernel32.ReplaceFileW
            replace.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p]
            replace.restype = ctypes.c_int
            if not replace(str(target), str(temporary), None, 0, None, None):
                raise ctypes.WinError()
        else:
            os.replace(temporary, target)
    finally:
        if temporary.exists():
            temporary.unlink()


def apply(output):
    plan = load(output / 'deployment-plan.json')
    command = 'Get-CimInstance Win32_Process | Select-Object Name,ExecutablePath | ConvertTo-Json -Compress'
    processes = json.loads(subprocess.check_output(['pwsh', '-NoProfile', '-Command', command], text=True, encoding='utf-8-sig'))
    roots = [str(safe(plan[key], ROOT / 'sandbox')).lower() + os.sep for key in ('server', 'client')]
    if any(row.get('ExecutablePath') and any(row['ExecutablePath'].lower().startswith(root) for root in roots) for row in processes):
        raise ValueError('Stop both target runtimes before applying this candidate')
    if (output / 'deployment.json').exists() or (output / 'rollback').exists():
        raise ValueError('This deployment was already attempted; inspect its receipt before a new plan')
    for row in plan['files']:
        target = safe(row['target'], ROOT / 'sandbox')
        resource_root = plan.get('resourceRoot') if row['category'] == 'resource' else None
        if identity(Path(row['source']['path']), resource_root) != row['source']:
            raise ValueError(f'Source changed: {row["source"]["path"]}')
        if (identity(target) if target.exists() else None) != row['previous']:
            raise ValueError(f'Destination changed since planning: {target}')
    changed = []
    try:
        for index, row in enumerate(plan['files']):
            source, target = Path(row['source']['path']), Path(row['target'])
            if row['previous'] and row['previous']['sha256'] == row['source']['sha256']:
                continue
            backup = output / 'rollback' / str(index)
            if target.exists():
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, backup)
                if sha(backup) != row['previous']['sha256'] or backup.stat().st_nlink != 1:
                    raise ValueError(f'Backup failed: {target}')
            changed.append((row, backup))
            atomic_copy(source, target, plan.get('resourceRoot') if row['category'] == 'resource' else None)
            if sha(target) != row['source']['sha256'] or target.stat().st_nlink != 1:
                raise ValueError(f'Deployed content mismatch: {target}')
    except Exception:
        for row, backup in reversed(changed):
            target = safe(row['target'], ROOT / 'sandbox')
            if backup.exists():
                atomic_copy(backup, target)
            elif target.exists():
                target.unlink()
        raise
    write_new(output / 'deployment.json', dict(changed=len(changed), verified=len(plan['files']),
        files=[identity(Path(row['target'])) for row in plan['files']],
        note='Files applied with target runtimes stopped; no process started and no gameplay validation.'))
    print(json.dumps(dict(deployed=True, changed=len(changed), started=False)))


def verify_http(output, paths_file):
    plan = load(output / 'deployment-plan.json')
    root = Path(plan['server']) / 'cstrike'
    requested = load(safe(paths_file))
    if not isinstance(requested, list) or not requested or not all(isinstance(path, str) for path in requested) or len(set(requested)) != len(requested):
        raise ValueError('HTTP paths must be a nonempty unique JSON array from the actual server resource list')
    available = {Path(row['target']).relative_to(root).as_posix(): row for row in plan['files'] if row['category'] == 'resource'}
    if not set(requested) <= available.keys():
        raise ValueError('Requested HTTP resource is absent from this deployment plan')
    results = []
    for relative in requested:
        row = available[relative]
        url = plan['http']['url'] + urllib.parse.quote(relative, safe='/')
        digest, size = hashlib.sha256(), 0
        with urllib.request.urlopen(url, timeout=15) as response:
            if response.status != 200:
                raise ValueError(f'HTTP resource unavailable: {relative}: {response.status}')
            for data in iter(lambda: response.read(1024 * 1024), b''):
                digest.update(data)
                size += len(data)
        if size != row['source']['bytes'] or digest.hexdigest() != row['source']['sha256']:
            raise ValueError(f'HTTP bytes differ: {relative}')
        results.append(dict(path=relative, bytes=size, sha256=digest.hexdigest()))
    write_new(output / 'http-verification.json', dict(files=results,
        note='Actual HTTP download byte checks only; ordinary CS cold-download joining remains a separate live test.'))
    print(json.dumps(dict(httpFiles=len(results), passed=True)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('begin', 'seal', 'plan', 'apply', 'verify-http'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resources', type=Path)
    parser.add_argument('--resource-root', type=Path, help='Explicit read-only common root for manifest sources outside the workspace')
    parser.add_argument('--http-paths', type=Path, help='JSON array of this candidate resources actually registered by the running server; excludes server-only tables')
    parser.add_argument('--server', type=Path, default=ROOT / 'sandbox/cs-server/Half-Life')
    parser.add_argument('--client', type=Path, default=ROOT / 'sandbox/cs-client-b/Half-Life')
    parser.add_argument('--http-url', default='http://127.0.0.1:27080/goldcraft/')
    parser.add_argument('--http-port', type=int, default=27080)
    args = parser.parse_args()
    output = safe(args.output)
    if args.command == 'plan':
        if not args.resources or not 1 <= args.http_port <= 65535:
            parser.error('plan requires --resources and a valid --http-port')
        plan(output, args.resources, args.server, args.client, args.http_url, args.http_port, args.resource_root)
    elif args.command == 'verify-http':
        if not args.http_paths:
            parser.error('verify-http requires --http-paths from the actual running server resource list')
        verify_http(output, args.http_paths)
    else:
        globals()[args.command.replace('-', '_')](output)


if __name__ == '__main__':
    main()
