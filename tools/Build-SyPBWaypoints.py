"""Prepare pinned cs_assault waypoints or encode Bloodmoon's actual hull traces."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import math
import struct
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / 'sandbox/cs-server/Half-Life/cstrike'
MAP = 'sy_zombie2_Bloodmoon'
CSV = BASE / 'addons/sypb/wptdefault/bloodmoon-hull-nav.csv'
OUTPUT = CSV.with_name(MAP + '.pwf')
ASSAULT_COMMIT = '38e8928b3f09607c7d3a3327f0e6cf93948db954'
ASSAULT_SHA256 = '2677c21a49e06eeab8cb36ef7bc16e924278d88ed3196363ad08c1017cf1ed6e'


def writable_path(path):
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError('Waypoint destination escapes workspace')
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError('Waypoint destination traverses a reparse point')
        if parent == ROOT:
            break
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def prepare_assault():
    url = f'https://raw.githubusercontent.com/CCNHsK-Dev/SyPB_Waypoint/{ASSAULT_COMMIT}/Waypoints/cs_assault.pwf'
    cache = writable_path(ROOT / 'dist/sypb/waypoints/cs_assault.pwf')
    if cache.exists():
        blob = cache.read_bytes()
    else:
        with urllib.request.urlopen(url, timeout=30) as response:
            blob = response.read(1_048_577)
    if hashlib.sha256(blob).hexdigest() != ASSAULT_SHA256:
        raise ValueError('Official cs_assault waypoint hash does not match its pin')
    magic, version, count, map_name, author = struct.unpack_from('<8sii32s32s', blob)
    if (magic.rstrip(b'\0') != b'PODWAY!' or version != 7 or not 1 <= count <= 1024
            or map_name.split(b'\0')[0] != b'cs_assault' or len(blob) != 80 + count * 204):
        raise ValueError('Unexpected SyPB waypoint header or size')
    edges = 0
    for i in range(count):
        index = struct.unpack_from('<i', blob, 80 + i * 204)[0]
        links = struct.unpack_from('<8h', blob, 80 + i * 204 + 40)
        if index != i or any(n < -1 or n >= count or n == i for n in links):
            raise ValueError('Malformed SyPB waypoint index or link')
        edges += sum(n >= 0 for n in links)
    destination = writable_path(BASE / 'addons/sypb/wptdefault/cs_assault.pwf')
    if destination.exists() and destination.read_bytes() != blob:
        raise ValueError('Preserving an existing modified cs_assault waypoint file')
    cache.write_bytes(blob)
    destination.write_bytes(blob)
    report = {'map': 'cs_assault', 'source': url, 'commit': ASSAULT_COMMIT,
              'sha256': ASSAULT_SHA256, 'nodes': count, 'edges': edges,
              'author': author.split(b'\0')[0].decode('utf-8'),
              'limitations': 'Official map route; actual zombie navigation/combat requires runtime observation.'}
    writable_path(ROOT / 'analysis/goldcraft-tests/assault-waypoints.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))
    print('Dedicated SyPB loads waypoints on map load. Reload cs_assault, then set sypb_quota 6.')


def build_bloodmoon():
    rows = list(csv.DictReader(CSV.open()))
    if not 1 <= len(rows) <= 1024:
        raise ValueError('SyPB source requires 1..1024 waypoints')
    points = [(float(row['x']), float(row['y']), float(row['z'])) for row in rows]
    links = [[int(row['n' + str(i)]) for i in range(8)] for row in rows]
    for i, (row, neighbors) in enumerate(zip(rows, links)):
        if int(row['id']) != i or not all(-1 <= n < len(rows) and n != i for n in neighbors):
            raise ValueError('Malformed engine navigation export')
    seen, components = set(), []
    for i in range(len(rows)):
        if i in seen:
            continue
        todo, component = [i], []
        seen.add(i)
        while todo:
            n = todo.pop()
            component.append(n)
            for other in links[n]:
                if other >= 0 and other not in seen:
                    seen.add(other)
                    todo.append(other)
        components.append(component)
    components.sort(key=len, reverse=True)
    # Keep every hull-verified node, including separate courtyard sections.
    # Camp positions are close to the map's original human spawn and reachable
    # inside its component. All combat/target selection stays in SyPB.
    original_spawn = (-1958.0, 442.0, 38.0)
    nearest = min(range(len(points)), key=lambda i: math.dist(points[i], original_spawn))
    human_component = next(c for c in components if nearest in c)
    camps = set(sorted(human_component, key=lambda i: math.dist(points[i], original_spawn))[:4])
    blob = bytearray(struct.pack('<8sii32s32s', b'PODWAY!', 7, len(rows), MAP.encode(), b'GoldCraft native hull traces'))
    layout = struct.Struct('<ii8f8h8H24f8i2H')
    assert layout.size == 204
    for i, (point, neighbors) in enumerate(zip(points, links)):
        flags = (1 << 10) | (1 << 4) if i in camps else 0
        distances = [round(math.dist(point, points[n])) if n >= 0 else 0 for n in neighbors]
        blob.extend(layout.pack(i, flags, *point, 0.0, 0.0, 0.0, 0.0, 0.0,
                                *neighbors, *([0] * 8), *([0.0] * 24), *distances, 0, 0))
    if not OUTPUT.resolve().is_relative_to(ROOT / 'sandbox') or OUTPUT.is_symlink():
        raise ValueError('Waypoint destination escapes sandbox')
    OUTPUT.write_bytes(blob)
    report = {'map': MAP, 'source': 'ReHLDS EngFunc_TraceHull, HULL_HUMAN, all brush entities',
              'bspSha256': hashlib.sha256((BASE / 'maps' / (MAP + '.bsp')).read_bytes()).hexdigest(),
              'csvSha256': hashlib.sha256(CSV.read_bytes()).hexdigest(),
              'pwfSha256': hashlib.sha256(blob).hexdigest(), 'nodes': len(rows),
              'edges': sum(n >= 0 for ns in links for n in ns),
              'componentSizes': [len(c) for c in components], 'humanSpawnNode': nearest,
              'humanComponentSize': len(human_component), 'campNodes': sorted(camps),
              'limitations': 'Standing walk paths only; actual bot navigation/combat still requires runtime validation.'}
    (ROOT / 'analysis/goldcraft-tests/bloodmoon-waypoints.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', choices=('cs_assault', MAP), default=MAP)
    args = parser.parse_args()
    if args.map == 'cs_assault':
        prepare_assault()
    else:
        build_bloodmoon()


if __name__ == '__main__':
    main()
