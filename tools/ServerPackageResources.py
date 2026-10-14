"""Collect game data without copying installations, account state or build outputs."""
from pathlib import Path, PurePosixPath
import os

# Runtime/configuration trees have separate, reviewed packaging rules.
PRIVATE_DIRS = frozenset({
    'addons', 'cl_dlls', 'dlls', 'metahook', 'logs', 'save', 'saves', 'screenshots',
    'cache', 'downloadcache', 'downloads', 'backup', 'backups', 'config', 'configs',
    'controller_configs', 'manual',
})
NON_ASSET_SUFFIXES = frozenset({
    '.exe', '.dll', '.so', '.dylib', '.pdb', '.amxx', '.sma', '.cfg', '.ini',
    '.log', '.tmp', '.ztmp', '.bak', '.old', '.orig', '.partial', '.dem',
    '.ps1', '.bat', '.cmd', '.com', '.vbs', '.sys', '.pem', '.key', '.pfx',
    '.blend', '.obj', '.mtl', '.map', '.prt', '.rmf', '.max', '.vmf',
})
GENERATED_NAMES = frozenset({'cached.wad', 'tempdecal.wad', 'pldecal.wad', 'spraypaint.wad', 'custom.hpk'})


def resource_roots(game: Path, valve: Path):
    # ReHLDS filesystem.cpp adds GAME, GAMEDOWNLOAD, then DEFAULTGAME.
    # Fold downloaded data into cstrike for a standalone server installation.
    return ((game, 'cstrike'), (game.with_name(game.name + '_downloads'), 'cstrike'), (valve, 'valve'))


def resource_path(name: str) -> PurePosixPath:
    relative = PurePosixPath(name)
    if (not name or relative.is_absolute() or any(c in name for c in ':\\\x00\r\n')
            or any(not p or p in ('.', '..') or p.startswith('.') or p.lower() in PRIVATE_DIRS for p in name.split('/'))
            or relative.name.lower() in GENERATED_NAMES or relative.suffix.lower() in NON_ASSET_SUFFIXES):
        raise ValueError('Unsafe or non-resource path: ' + name)
    return relative


def resource_files(root: Path, safe_file):
    if not root.exists():
        return
    # Inspect directories before descending; never silently traverse a junction.
    for base, dirs, names in os.walk(root, followlinks=False):
        directory = Path(base)
        if directory.is_symlink() or directory.is_junction():
            raise ValueError('Reparse resource directory: ' + str(directory))
        for name in list(dirs):
            child = directory / name
            if child.is_symlink() or child.is_junction():
                raise ValueError('Reparse resource directory: ' + str(child))
            if name.startswith('.') or name.lower() in PRIVATE_DIRS or (directory == root and name.lower() == 'maps'):
                dirs.remove(name)
        for name in sorted(names):
            path = directory / name
            relative = path.relative_to(root).as_posix()
            # Root files mix account/client settings with assets. WADs/PAKs are the
            # resource exception; explicit map .res dependencies are separate.
            if directory == root and path.suffix.lower() not in {'.wad', '.pak'}:
                continue
            try:
                resource_path(relative)
            except ValueError:
                continue
            yield relative, safe_file(path)


def collect_resources(root: Path, safe_file):
    files, names = {}, set()
    for directory, target in resource_roots(root / 'cstrike', root / 'valve'):
        for relative, source in resource_files(directory, safe_file):
            name = f'cs/{target}/{relative}'
            if name.lower() not in names:
                files[name] = source
                names.add(name.lower())
    return files


def resolve_resource(name, game, valve, safe_file):
    resource_path(name)
    for base, target in resource_roots(game, valve):
        source = base / name
        if source.is_file():
            return f'cs/{target}/{name}', safe_file(source)
    raise FileNotFoundError('Missing game resource: ' + name)
