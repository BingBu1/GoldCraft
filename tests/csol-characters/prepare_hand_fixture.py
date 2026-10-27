"""Create two disposable viewmodels for the server-downloaded hand adapter test.

Usage: python tests/csol-characters/prepare_hand_fixture.py STOCK_AK47.mdl OUTPUT
OUTPUT must be a new folder. Never writes to the supplied source model.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct


def prepare(source: Path, output: Path, bodygroups=False):
    original = source.read_bytes()
    custom = bytearray(original)

    def word(data, offset):
        return struct.unpack_from('<i', data, offset)[0]

    def put(data, offset, value):
        struct.pack_into('<i', data, offset, value)

    if len(custom) < 244 or word(custom, 0) != 0x54534449 or word(custom, 4) != 10:
        raise ValueError('Requires a decoded IDST v10 AK47 model')
    count, start = word(custom, 180), word(custom, 184)
    if not 1 <= count <= 1024 or start < 244 or start + count * 80 > len(custom):
        raise ValueError('Invalid texture table')
    marked = None
    for i in range(count):
        at = start + i * 80
        name = bytes(custom[at:at + 64]).split(b'\0')[0]
        if not name.startswith(b'view_'):
            palette = word(custom, at + 76) + word(custom, at + 68) * word(custom, at + 72)
            if palette < start or palette + 768 > len(custom):
                raise ValueError('Invalid texture palette')
            custom[palette:palette + 768] = bytes([240, 120, 40]) * 256
            marked = name.decode('ascii')
            break
    if marked is None:
        raise ValueError('No separate weapon texture')
    main = custom[:start]
    textures = bytearray(244) + custom[start:]
    put(textures, 0, 0x54534449)
    put(textures, 4, 10)
    for offset in (180, 192, 196):
        put(textures, offset, word(custom, offset))
    for offset in (184, 188, 200):
        if word(custom, offset) < start:
            raise ValueError('Expected trailing texture and skin data')
        put(textures, offset, word(custom, offset) - start + 244)
    for i in range(count):
        put(textures, 244 + i * 80 + 76, word(custom, start + i * 80 + 76) - start + 244)
    for offset in range(180, 204, 4):
        put(main, offset, 0)
    put(main, 72, len(main))
    put(textures, 72, len(textures))
    output.mkdir(exist_ok=False, parents=True)
    files = []
    payloads = [('v_hand_custom.mdl', custom), ('v_hand_split.mdl', main), ('v_hand_splitT.mdl', textures)]
    if bodygroups:
        conditional = bytearray(custom)
        groups = bytearray()
        count, table = word(custom, 204), word(custom, 208)
        if count != 3:
            raise ValueError('Expected separate right hand, left hand, weapon groups')
        def append(data):
            conditional.extend(b'\0' * (-len(conditional) % 4))
            offset = len(conditional)
            conditional.extend(data)
            return offset
        for i, expected in enumerate((b'rhand', b'lhand', b'weapon')):
            group = bytearray(custom[table + i * 76:table + (i + 1) * 76])
            if bytes(group[:64]).split(b'\0')[0] != expected or word(group, 64) != 1:
                raise ValueError('Unexpected source bodygroup')
            at = word(group, 72)
            models = custom[at:at + 112] + bytes(112)
            put(group, 64, 2)
            put(group, 68, 1 << i)
            put(group, 72, append(models))
            groups.extend(group)
        put(conditional, 208, append(groups))
        put(conditional, 72, len(conditional))
        payloads = [('v_hand_states.mdl', conditional)]
    for name, data in payloads:
        path = output / 'models' / 'csol' / 'tests' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        files.append(dict(path=path.relative_to(output).as_posix(), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    (output / 'manifest.json').write_text(json.dumps(dict(sourceSHA256=hashlib.sha256(original).hexdigest(), markerTexture=marked, files=files), indent=2), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--bodygroups', action='store_true', help='Generate independently switchable hands and weapon')
    args = parser.parse_args()
    prepare(args.source, args.output, args.bodygroups)
