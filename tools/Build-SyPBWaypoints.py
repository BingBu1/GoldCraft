"""Encode SyPB v7 waypoints from the map adapter's actual ReHLDS hull traces."""
from pathlib import Path
import csv
import hashlib
import json
import math
import struct

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / 'sandbox/cs-server/Half-Life/cstrike'
MAP = 'sy_zombie2_Bloodmoon'
CSV = BASE / 'addons/sypb/wptdefault/bloodmoon-hull-nav.csv'
OUTPUT = CSV.with_name(MAP + '.pwf')


def main():
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


if __name__ == '__main__':
    main()
