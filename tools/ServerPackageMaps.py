"""GoldSrc map selection and resource closure for a clean server release."""
from pathlib import Path, PurePosixPath
import hashlib
import re
import struct
import zlib


def select_maps(game: Path, selected: list[str], default: str) -> list[Path]:
    available = {p.stem.lower(): p for p in sorted((game / 'maps').glob('*.bsp'))}
    names = list(available) if selected == ['*'] else selected
    if not names or any(not re.fullmatch(r'[A-Za-z0-9_]+', n) for n in names):
        raise ValueError('maps must be ["*"] or a nonempty list of map names without .bsp')
    keys = [name.lower() for name in names]
    if len(set(keys)) != len(keys):
        raise ValueError('Duplicate map names')
    if default.lower() not in keys:
        raise ValueError('defaultMap must be included in maps')
    missing = set(keys) - available.keys()
    if missing:
        raise ValueError('Maps not installed: ' + ', '.join(sorted(missing)))
    return [available[default.lower()], *(available[k] for k in keys if k != default.lower())]


def map_resources(bsp: Path, game: Path, valve: Path, safe_file):
    """Return resources and diagnostics; never follow absolute compiler WAD paths."""
    data = safe_file(bsp).read_bytes()
    if len(data) < 124 or struct.unpack_from('<i', data)[0] != 30:
        raise ValueError(f'Unsupported GoldSrc BSP: {bsp.name}')

    def lump(index):
        offset, length = struct.unpack_from('<ii', data, 4 + index * 8)
        if offset < 0 or length < 0 or offset + length > len(data):
            raise ValueError(f'Invalid BSP lump: {bsp.name}')
        return data[offset:offset + length]

    entities = lump(0).decode('latin1')
    textures = lump(2)
    external = set()
    if textures:
        if len(textures) < 4:
            raise ValueError(f'Invalid BSP texture table: {bsp.name}')
        count, = struct.unpack_from('<i', textures)
        if count < 0 or 4 + count * 4 > len(textures):
            raise ValueError(f'Invalid BSP texture table: {bsp.name}')
        for i in range(count):
            offset, = struct.unpack_from('<i', textures, 4 + 4 * i)
            if offset == -1:
                continue
            if offset < 0 or offset + 40 > len(textures):
                raise ValueError(f'Invalid BSP texture: {bsp.name}')
            if struct.unpack_from('<I', textures, offset + 24)[0] == 0:
                external.add(textures[offset:offset + 16].split(b'\0')[0].lower())

    resources = {f'cs/cstrike/maps/{bsp.name}': bsp}
    available_wads = {}
    for directory, target in ((valve, 'valve'), (game, 'cstrike')):
        for path in directory.glob('*.wad'):
            available_wads[path.name.lower()] = (path, f'cs/{target}/{path.name}')
    wad_values = re.findall(r'"wad"\s*"([^"]*)"', entities, re.I)
    wad_names = {name.replace('\\', '/').rsplit('/', 1)[-1].lower()
                 for value in wad_values for name in value.split(';') if name.strip()}
    found = set()
    for name in sorted(wad_names):
        if name not in available_wads:
            continue  # Compilers also list unused WADs. Check actual textures below.
        path, target = available_wads[name]
        with safe_file(path).open('rb') as stream:
            header = stream.read(12)
            if len(header) != 12 or header[:4] not in (b'WAD2', b'WAD3'):
                raise ValueError(f'Invalid WAD: {path.name}')
            count, offset = struct.unpack_from('<ii', header, 4)
            if count < 0 or offset < 12 or offset + count * 32 > path.stat().st_size:
                raise ValueError(f'Invalid WAD directory: {path.name}')
            stream.seek(offset)
            table = stream.read(count * 32)
            names = {table[i * 32 + 16:i * 32 + 32].split(b'\0')[0].lower() for i in range(count)}
        if names & external:
            resources[target] = path
            found.update(names)
    if external - found:
        missing = ', '.join(n.decode('latin1') for n in sorted(external - found))
        raise ValueError(f'{bsp.name}: missing WAD textures: {missing}')

    for extension in ('.res', '.txt'):
        sidecar = bsp.with_suffix(extension)
        if sidecar.exists():
            resources[f'cs/cstrike/maps/{sidecar.name}'] = safe_file(sidecar)
    res = bsp.with_suffix('.res')
    if res.exists():
        for line in safe_file(res).read_text(encoding='utf-8-sig', errors='replace').splitlines():
            name = line.split('//', 1)[0].strip().strip('"').replace('\\', '/')
            if not name:
                continue
            relative = PurePosixPath(name)
            if relative.is_absolute() or '..' in relative.parts or ':' in name or relative.suffix.lower() not in {
                    '.bsp', '.wad', '.tga', '.bmp', '.mdl', '.wav', '.spr', '.txt', '.res', '.mp3'}:
                raise ValueError(f'{res.name}: unsafe or unsupported resource path')
            for base, target in ((game, 'cstrike'), (valve, 'valve')):
                source = base / name
                if source.is_file():
                    resources[f'cs/{target}/{name}'] = safe_file(source)
                    break
            else:
                raise FileNotFoundError(f'{res.name}: missing resource {name}')

    waypoint = game / 'addons/sypb/wptdefault' / (bsp.stem + '.pwf')
    if waypoint.exists():
        resources[f'cs/cstrike/addons/sypb/wptdefault/{waypoint.name}'] = safe_file(waypoint)
    crc = zlib.crc32(data)
    return resources, {'map': bsp.stem, 'crc32': f'{crc:08x}', 'bspSha256': hashlib.sha256(data).hexdigest(),
                       'dimension': f'goldcraft:{bsp.stem.lower()}_{crc:08x}',
                       'botWaypoints': waypoint.exists()}
