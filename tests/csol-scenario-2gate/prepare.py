# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate local zs_2gate fixtures from the pinned original BSP.

The copyrighted BSP/entity data is not checked into the repository. This
script only reads its inputs and writes a new output directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import struct

BSP_SHA256 = 'bd2d95e5f3496cd60306eea42b88096eb4dd6474bea4c346888adbcfe13f32cc'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def entities(raw):
    version, offset, length = struct.unpack_from('<3i', raw)
    if version != 30 or offset < 124 or length < 1 or offset + length > len(raw):
        raise ValueError('Invalid BSP entity lump')
    lump = raw[offset:offset + length]
    # The pinned lump has plain quoted scalars only; refuse anything else.
    if b'\\"' in lump:
        raise ValueError('Escaped BSP strings require an explicit parser update')
    tokens = re.findall(rb'"([^"\r\n]*)"|([{}])', lump)
    rows, at = [], 0
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
    return rows, hashlib.sha256(lump).hexdigest()


def points(bsp):
    raw = bsp.read_bytes()
    if hashlib.sha256(raw).hexdigest() != BSP_SHA256:
        raise ValueError('Unexpected zs_2gate BSP; inspect its identity before changing the fixture')
    rows, lump_sha = entities(raw)
    lines = ['struct ActualPoint { bool monster; int index,id,quota,team; float x,y,z,pitch,yaw,roll; };',
             'constexpr ActualPoint Actual2GatePoints[] = {']
    counts = [0, 0]
    for index, fields in enumerate(rows):
        classname = fields.get('classname')
        # zs_2gate keeps 27 zbsbreak walls (handled by the wall entity, not points).
        if classname == 'zbsbreak' and fields.get('hmdmgratio') != '0':
            raise ValueError('zs_2gate wall accepts player damage; its zero reward threshold would be consumed')
        if classname in ('playerspawn', 'zbstrigger'):
            raise ValueError('zs_2gate unexpectedly has scenario-specific ' + classname)
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
    if len(rows) != 252 or counts != [15, 75]:
        raise ValueError('Unexpected map entity inventory')
    lines.append('};')
    return '\n'.join(lines), dict(entities=len(rows), players=counts[0], monsters=counts[1],
        entitySHA256=lump_sha)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.relative_to(Path(__file__).resolve().parents[2])
    bsp = args.common / 'maps/zs_2gate.bsp'
    inputs = {str(bsp): digest(bsp)}
    actual, inventory = points(bsp)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'actual_2gate_points.inc').write_text(actual, encoding='utf8')
    if digest(bsp) != inputs[str(bsp)]:
        raise RuntimeError('Fixture input changed while reading')
    receipt = dict(inputs=inputs, inventory=inventory,
        generated={'actual_2gate_points.inc': digest(output / 'actual_2gate_points.inc')})
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf8')
    print(json.dumps(inventory))


if __name__ == '__main__':
    main()
