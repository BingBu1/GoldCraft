# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate local fixtures from the actual source and the pinned lostcity BSP.

The copyrighted BSP/entity fixture is not checked into the repository. This
script does not copy assets, deploy a server, or modify its input files.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def extract(text, token):
    """Extract an unchanged C++ method, skipping quoted braces and comments."""
    if text.count(token) != 1:
        raise ValueError('Ambiguous or missing C++ method: ' + token)
    start = text.index(token)
    at = text.index('{', start)
    depth = 0
    state = 'code'
    while at < len(text):
        char = text[at]
        pair = text[at:at + 2]
        if state == 'line':
            if char == '\n':
                state = 'code'
        elif state == 'block':
            if pair == '*/':
                state = 'code'
                at += 1
        elif state in ('"', "'"):
            if char == '\\':
                at += 1
            elif char == state:
                state = 'code'
        elif pair in ('//', '/*'):
            state = 'line' if pair == '//' else 'block'
            at += 1
        elif char in ('"', "'"):
            state = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return text[start:at + 1]
        at += 1
    raise ValueError('Unterminated C++ method: ' + token)


def points(bsp):
    raw = bsp.read_bytes()
    if hashlib.sha256(raw).hexdigest() != '5970511398eb588dc3949563eed5f5f42baecc976730744f0cf41335f946d56e':
        raise ValueError('Unexpected zs_lostcity BSP; inspect its identity before changing the fixture')
    version, offset, length = struct.unpack_from('<3i', raw)
    if version != 30 or offset < 124 or length < 1 or offset + length > len(raw):
        raise ValueError('Invalid BSP entity lump')
    entity_bytes = raw[offset:offset + length]
    rows = []
    # The pinned lump has ordinary quoted scalar fields and no escaped quotes.
    # Do not silently use this simple parser on another map/version.
    if b'\\"' in entity_bytes:
        raise ValueError('Escaped BSP strings require an explicit parser update')
    tokens = re.findall(rb'"([^"\r\n]*)"|([{}])', entity_bytes)
    at = 0
    while at < len(tokens):
        if tokens[at][1] != b'{':
            raise ValueError('Expected BSP entity opening brace')
        at += 1
        fields = {}
        while at < len(tokens) and tokens[at][1] != b'}':
            if at + 1 >= len(tokens) or tokens[at][1] or tokens[at + 1][1]:
                raise ValueError('Incomplete BSP key/value pair')
            fields[tokens[at][0].decode('latin1')] = tokens[at + 1][0].decode('latin1')
            at += 2
        if at >= len(tokens):
            raise ValueError('Unclosed BSP entity')
        at += 1
        rows.append(fields)
    lines = ['struct ActualPoint { bool monster; int index,id,quota,team; float x,y,z,pitch,yaw,roll; };',
             'constexpr ActualPoint ActualLostcityPoints[] = {']
    counts = [0, 0]
    for index, fields in enumerate(rows):
        classname = fields.get('classname')
        if classname not in ('zombiespawn', 'info_player_start'):
            continue
        monster = classname == 'zombiespawn'
        counts[int(monster)] += 1
        coordinates = fields.get('origin', '0 0 0').split()
        coordinates += fields.get('angles', '0 ' + fields.get('angle', '0') + ' 0').split()
        if len(coordinates) != 6 or any(not re.fullmatch(r'[-+]?\d+(?:\.\d+)?', part) or
                not math.isfinite(float(part)) for part in coordinates):
            raise ValueError('Invalid point coordinates')
        values = [str(monster).lower(), str(index), str(int(fields.get('id', '0'))),
                  str(int(fields.get('maxspawn', '0'))),
                  str(int(fields.get('team2', '1' if monster else '2'))), *coordinates]
        lines.append('{' + ','.join(values) + '},')
    if len(rows) != 234 or counts != [15, 58]:
        raise ValueError('Unexpected map entity inventory')
    lines.append('};')
    return '\n'.join(lines), dict(entities=len(rows), players=counts[0], monsters=counts[1],
        entitySHA256=hashlib.sha256(entity_bytes).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = args.source_root.resolve()
    output = args.output.resolve()
    output.relative_to(Path(__file__).resolve().parents[2])
    sources = [source / 'native/csol/server/game_rules_gate.cpp',
               source / 'external/ReGameDLL_CS/regamedll/dlls/gamerules.h',
               args.common / 'maps/zs_lostcity.bsp']
    inputs = {str(path): digest(path) for path in sources}
    timeout = '\n'.join(extract(path.read_text('utf8'), token) for path, token in zip(sources,
        ['void CheckSurvivalTimeout()', 'float GetRoundRemainingTime() const']))
    actual_points, inventory = points(sources[2])
    output.mkdir(parents=True, exist_ok=False)
    for name, text in [('actual_timeout.inc', timeout), ('actual_lostcity_points.inc', actual_points)]:
        (output / name).write_text(text, encoding='utf8')
    if any(digest(Path(path)) != sha for path, sha in inputs.items()):
        raise RuntimeError('Fixture inputs changed while reading')
    receipt = dict(inputs=inputs, inventory=inventory,
        generated={name: digest(output / name) for name in ['actual_timeout.inc', 'actual_lostcity_points.inc']})
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf8')
    print(json.dumps(inventory))


if __name__ == '__main__':
    main()
