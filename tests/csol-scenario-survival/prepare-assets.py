#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Inventory lostcity BSP and dialogue dependencies without deploying assets.

This supplements the monster/material closure. The production model collector
and dialogue reader are required; no archive or substitute table parser is used.
"""
from pathlib import Path, PureWindowsPath
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from csol_scenario_resources import dialogue_contract

BSP_SHA = '5970511398eb588dc3949563eed5f5f42baecc976730744f0cf41335f946d56e'


def identity(path):
    path = path.resolve(strict=True)
    with path.open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha)


def relative_name(value):
    name = value.replace('\\', '/')
    if not name or name.startswith('/') or ':' in name or any(p in ('', '.', '..') for p in name.split('/')):
        raise ValueError('Invalid resource path: ' + value)
    return name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--dialogue-reader', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    common = args.common.resolve(strict=True)
    bsp = common / 'maps/zs_lostcity.bsp'
    bsp_id = identity(bsp)
    if bsp_id['sha256'] != BSP_SHA:
        raise ValueError('This parser is verified only for the recorded original lostcity BSP')
    raw = bsp.read_bytes()
    if struct.unpack_from('<i', raw)[0] != 30:
        raise ValueError('Expected GoldSrc BSP v30')
    offset, size = struct.unpack_from('<ii', raw, 4)
    if offset < 124 or size <= 0 or offset + size > len(raw):
        raise ValueError('Entity lump outside BSP')
    entity_data = raw[offset:offset + size]
    text = entity_data.decode('latin1').rstrip('\0')
    if '\\"' in text:
        raise ValueError('Escaped strings need a parser update')
    entities = [dict(re.findall(r'"([^"\n]*)"\s*"([^"\n]*)"', block))
                for block in re.findall(r'\{(.*?)\}', text, re.S)]
    if len(entities) != 234 or entities[0].get('classname') != 'worldspawn':
        raise ValueError('Expected original worldspawn and entity count')
    world = entities[0]
    reasons = {}
    def add(path, reason):
        name = relative_name(path)
        reasons.setdefault(name, set()).add(reason)
    for value in world['wad'].split(';'):
        if value:
            add(PureWindowsPath(value).name, 'worldspawn WAD')
    for face in ('bk', 'dn', 'ft', 'lf', 'rt', 'up'):
        add('gfx/env/' + world['skyname'] + face + '.tga', 'worldspawn sky')
    models = set()
    for entity in entities:
        for value in entity.values():
            if value.lower().endswith(('.mdl', '.spr')):
                models.add(relative_name(value))
                add(value, 'BSP entity model')
            elif value.lower().endswith(('.wav', '.mp3')):
                add(value if value.startswith('sound/') else 'sound/' + value, 'BSP entity audio')
    if models:
        result = subprocess.run([str(args.collector.resolve(strict=True)), str(common), '--json', *sorted(models)],
                                check=True, capture_output=True)
        for row in json.loads(result.stdout):
            if relative_name(row['path']) != relative_name(row['source']):
                raise ValueError('Derived model resources require material staging before this plan')
            add(row['path'], 'production model collector')
    paths, dialogue = dialogue_contract(args.source_root.resolve(strict=True), common,
        args.dialogue_reader, 'maps/zbs_wave_zs_lostcity.cso')
    for name in paths:
        add(name, 'production dialogue reader')
    files = []
    for name in sorted(reasons):
        source = common / name
        if any(p.is_symlink() or p.is_junction() for p in (source, *source.parents)):
            raise ValueError('Linked resource ancestry: ' + name)
        source.resolve(strict=True).relative_to(common)
        record = identity(source)
        if not record['bytes']:
            raise ValueError('Empty resource: ' + name)
        files.append(dict(relative=name, source=record, reasons=sorted(reasons[name])))
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if any(p.is_symlink() or p.is_junction() for p in (output, *output.parents)):
        raise ValueError('Linked output ancestry')
    with output.open('x', encoding='utf8') as stream:
        json.dump(dict(bsp=bsp_id, entitySha256=hashlib.sha256(entity_data).hexdigest(),
            files=files, dialogue=dialogue, collector=identity(args.collector),
            scope='Map WAD/sky/entity and dialogue supplement; monster, weapon and shared UI plans remain required.',
            deployed=False), stream, ensure_ascii=False, indent=2)
    print(json.dumps(dict(files=len(files), output=str(output), deployed=False)))


if __name__ == '__main__':
    main()
