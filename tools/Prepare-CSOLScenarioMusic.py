#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage map BGM from consolidated common tables/BSP and the original bgm folder.

Uses the production C++ resource reader. Writes a fresh build directory and a
deployment manifest; it never changes the game installation or running server.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def digest(data):
    return hashlib.sha256(data).hexdigest()


def entities(data):
    if len(data) < 124 or struct.unpack_from('<i', data)[0] != 30:
        raise ValueError('Expected a GoldSrc version 30 BSP')
    start, size = struct.unpack_from('<ii', data, 4)
    if start < 124 or size < 1 or start > len(data) - size:
        raise ValueError('Invalid BSP entity lump')
    text = data[start:start + size].rstrip(b'\0').decode('latin1')
    tokens = re.findall(r'"[^"\r\n]*"|[{}]|\S+', text)
    result, current, index = [], None, 0
    while index < len(tokens):
        token = tokens[index]
        if token == '{' and current is None:
            current = {}
        elif token == '}' and current is not None:
            result.append(current)
            current = None
        elif current is not None and token.startswith('"') and index + 1 < len(tokens) and tokens[index + 1].startswith('"'):
            current[token[1:-1]] = tokens[index + 1][1:-1]
            index += 1
        else:
            raise ValueError('Malformed BSP entity keyvalues')
        index += 1
    if current is not None:
        raise ValueError('Unclosed BSP entity')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--bgm', type=Path, required=True)
    parser.add_argument('--reader', type=Path, required=True)
    parser.add_argument('--map', action='append', required=True, dest='maps')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT / 'build'):
        parser.error('Use a fresh workspace build output')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            raise ValueError('Linked output refused')
    inputs, paths = [], set()

    def read(path):
        data = path.read_bytes()
        inputs.append(dict(path=str(path), bytes=len(data), sha256=digest(data)))
        return data

    def resolve(mode, data):
        result = subprocess.run([str(args.reader.resolve()), mode], input=data, capture_output=True, check=True)
        return result.stdout.decode('ascii').splitlines()

    for map_name in args.maps:
        if not re.fullmatch(r'[a-zA-Z0-9_]+', map_name):
            raise ValueError('Invalid map name')
        paths.update(resolve('round', read(args.common / 'maps' / ('zbs_' + map_name + '.cso'))))
        for entity in entities(read(args.common / 'maps' / (map_name + '.bsp'))):
            if entity.get('classname') == 'trigger_bgm' and entity.get('filename'):
                paths.update(resolve('path', entity['filename'].encode('latin1')))
    files, bodies = [], {}
    for name in sorted(paths):
        if not name.startswith('bgm/') or '..' in Path(name).parts or Path(name).is_absolute():
            raise ValueError('Invalid resource reader output')
        source = args.bgm / name[4:]
        body = read(source)
        if not body:
            raise ValueError('Empty music file: ' + name)
        bodies[name] = body
        files.append(dict(source=str(output / name), destination=name, bytes=len(body), sha256=digest(body)))
    output.mkdir(parents=True)
    for name, body in bodies.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream:
            stream.write(body)
    manifest = dict(schemaVersion=1, scope='Map BGM closure; no runtime deployment',
                    maps=args.maps, inputs=inputs, readerSha256=digest(args.reader.read_bytes()),
                    fileCount=len(files), files=files)
    (output / 'resource-plan.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(files=len(files), bytes=sum(row['bytes'] for row in files))))


if __name__ == '__main__':
    main()
