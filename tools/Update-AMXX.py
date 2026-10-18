"""Upgrade stopped workspace servers to pinned AMXX, preserving local settings.

Official DLLs, matching gamedata and unmodified official Pawn plugins are
updated. Editable GoldCraft/ZP plugins are handled by Build-AMXX.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parent.parent
VERSION = '1.10.0.5487'
PACKAGES = {
    'amxmodx-1.10.0-git5487-base-windows.zip': 'ca13b474741f5f0b035507969cb853b7eac9052614402114801e21c4dc2f2ebd',
    'amxmodx-1.10.0-git5487-cstrike-windows.zip': '47f358b4dd40c0259576bbde9790442dee4b642fe31bfed0421180b81429d4aa',
}
PREVIOUS = {
    'amxmodx-1.9.0-git5303-base-windows.zip': 'dd5c0f64b3974ce60e9a35d5bec957e8e2db95ae6fa12e45f24563373f550364',
    'amxmodx-1.9.0-git5303-cstrike-windows.zip': '54c83a9c632c86ff4dcf90793b47950c7308ab1d96f57052dbcaa203bd2fef35',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe(path, parent):
    path = path.absolute()
    if not path.resolve().is_relative_to(parent.resolve()):
        raise ValueError('Path outside the allowed workspace directory')
    for part in [path, *path.parents]:
        if part.is_symlink() or part.is_junction():
            raise ValueError('Linked paths are not supported')
        if part == ROOT:
            break
    return path


def payload(packages):
    result = {}
    for name, digest in packages.items():
        archive = safe(ROOT / '.tools/downloads' / name, ROOT)
        if sha(archive.read_bytes()) != digest:
            raise ValueError('Official archive hash mismatch: ' + name)
        with zipfile.ZipFile(archive) as bundle:
            prefix = 'addons/amxmodx/'
            for name in bundle.namelist():
                if name.startswith(prefix) and not name.endswith('/'):
                    rel = name[len(prefix):]
                    if Path(rel).is_absolute() or '..' in Path(rel).parts:
                        raise ValueError('Unsafe archive path')
                    result[rel] = bundle.read(name)
    return result


def processes():
    command = 'Get-CimInstance Win32_Process | Where-Object {$_.Name -eq "hlds.exe"} | Select-Object ExecutablePath | ConvertTo-Json -Compress'
    output = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', command],
                                     creationflags=subprocess.CREATE_NO_WINDOW, text=True).strip()
    rows = json.loads(output) if output else []
    return rows if isinstance(rows, list) else [rows]


def update(targets, check_only=False):
    current, old = payload(PACKAGES), payload(PREVIOUS)
    roots = [safe(ROOT / target / 'cstrike/addons/amxmodx', ROOT / 'sandbox') for target in targets]
    if len(set(roots)) != len(roots):
        raise ValueError('Duplicate server target')
    running = processes()
    changes, preserved = {}, {}
    for target in roots:
        if not (target / 'dlls/amxmodx_mm.dll').is_file():
            raise ValueError('Initialize the server before upgrading AMXX')
        executable = str(target.parents[2] / 'hlds.exe').lower()
        if any(str(row.get('ExecutablePath') or '').lower() == executable for row in running):
            raise RuntimeError('Stop the target server before upgrading AMXX')
        selected = {}
        for rel, data in current.items():
            path = safe(target / rel, target)
            if rel.startswith(('dlls/', 'modules/')) and path.suffix.lower() == '.dll':
                selected[path] = data
            elif rel.startswith('data/gamedata/'):
                # 1.10 Ham Sandwich uses common.games instead of hamdata.ini.
                # Merge official additions, preserving separate custom overrides.
                if path.exists() and path.read_bytes() not in (data, old.get(rel)):
                    raise ValueError('Custom official gamedata needs reconciliation: ' + rel)
                selected[path] = data
            elif rel.startswith('plugins/') and path.is_file() and path.read_bytes() == old.get(rel):
                selected[path] = data
        for path in target.rglob('*'):
            safe(path, target)
            if path.is_file() and path not in selected:
                preserved[path] = sha(path.read_bytes())
        changes.update({path: data for path, data in selected.items() if not path.exists() or path.read_bytes() != data})
    report = dict(version=VERSION, targets=targets, checkOnly=check_only,
                  changes=[p.relative_to(ROOT).as_posix() for p in changes], preservedFiles=len(preserved))
    if check_only:
        return report
    backup = safe(ROOT / 'build/amxx-upgrades' / str(time.time_ns()), ROOT / 'build')
    backup.mkdir(parents=True)
    before = {p: p.read_bytes() if p.exists() else None for p in changes}
    records = []
    for path, data in before.items():
        rel = path.relative_to(ROOT)
        if data is not None:
            copy = backup / 'files' / rel
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_bytes(data)
        records.append(dict(path=rel.as_posix(), before=sha(data) if data is not None else None,
                            after=sha(changes[path])))
    (backup / 'manifest.json').write_text(json.dumps(dict(version=VERSION, files=records), indent=2) + '\n')
    applied = []
    try:
        for path, data in changes.items():
            applied.append(path)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            if path.read_bytes() != data:
                raise RuntimeError('AMXX upgrade write verification failed')
        if any(sha(p.read_bytes()) != digest for p, digest in preserved.items()):
            raise RuntimeError('A preserved server file changed during the upgrade')
    except BaseException:
        for path in reversed(applied):
            if before[path] is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(before[path])
        raise
    report.update(backup=backup.relative_to(ROOT).as_posix(), files=records, verified=True)
    (backup / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', action='append', help='Workspace-relative Half-Life server directory')
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    print(json.dumps(update(args.target or ['sandbox/cs-server/Half-Life', 'sandbox/headless-combat/Half-Life'], args.check_only), indent=2))
