#!/usr/bin/env python3
"""Preflight and prepare hash-pinned, original Cry of Fear player weapons.

This tool preserves every source byte. It does not invent missing P models or
select character animations. Original assets stay local under build/.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

WORKSPACE = Path(__file__).resolve().parents[1]
ANCHOR = 'Bip01 R Hand'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ordinary_path(path: Path) -> Path:
    """Reject reparse points before resolving either source or destination."""
    absolute = path.absolute()
    for item in (absolute, *absolute.parents):
        if item.exists() or item.is_symlink():
            info = item.lstat()
            if item.is_symlink() or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError(f'Reparse point is not permitted: {item}')
    return absolute.resolve()


class Studio:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 244 or data[:4] != b'IDST' or self.i(4) != 10 or self.i(72) != len(data):
            raise ValueError('Expected complete studio v10 model')
        count, offset = self.i(140), self.i(144)
        self.span(offset, count, 112, 128)
        self.bones = []
        for index in range(count):
            at = offset + index * 112
            parent = self.i(at + 32)
            if parent < -1 or parent >= index:
                raise ValueError('Bone hierarchy is not ordered')
            self.bones.append((self.string(at, 32), parent))
        if len({name for name, _ in self.bones}) != count:
            raise ValueError('Ambiguous bone names')

    def span(self, offset: int, count: int, size: int, limit: int = 65536) -> None:
        if count < 0 or count > limit or offset < 0 or offset > len(self.data) or count * size > len(self.data) - offset:
            raise ValueError('Studio table is outside model')

    def i(self, at: int) -> int:
        return struct.unpack_from('<i', self.data, at)[0]

    def string(self, at: int, size: int) -> str:
        self.span(at, 1, size)
        return self.data[at:at + size].split(bytes([0]))[0].decode('latin1')

    def sequences(self) -> list[str]:
        count, offset = self.i(164), self.i(168)
        self.span(offset, count, 176, 2048)
        return [self.string(offset + index * 176, 32) for index in range(count)]

    def player_weapon(self) -> dict:
        anchors = [i for i, (name, _) in enumerate(self.bones) if name == ANCHOR]
        if len(anchors) != 1:
            raise ValueError('An original right-hand anchor is required; no offset is inferred')
        anchor = anchors[0]
        descendants = {anchor}
        for index, (_, parent) in enumerate(self.bones):
            if parent in descendants:
                descendants.add(index)
        vertex_count = normal_count = model_count = 0
        count, offset = self.i(204), self.i(208)
        self.span(offset, count, 76, 256)
        for index in range(count):
            body = offset + 76 * index
            models, model_at = self.i(body + 64), self.i(body + 72)
            self.span(model_at, models, 112, 256)
            for j in range(models):
                at = model_at + 112 * j
                for count_at, bone_at, vector_at in ((80, 84, 88), (92, 96, 100)):
                    n, bones, vectors = self.i(at + count_at), self.i(at + bone_at), self.i(at + vector_at)
                    self.span(bones, n, 1)
                    self.span(vectors, n, 12)
                    if any(b not in descendants for b in self.data[bones:bones + n]):
                        raise ValueError('Rendered geometry does not follow the original hand branch')
                    if count_at == 80:
                        vertex_count += n
                    else:
                        normal_count += n
                model_count += 1
        if not vertex_count:
            raise ValueError('Player weapon has no geometry')
        for count_at, offset_at, size, bone_at in ((156, 160, 32, 0), (212, 216, 88, 36)):
            n, at = self.i(count_at), self.i(offset_at)
            self.span(at, n, size, 256)
            if any(not 0 <= self.i(at + size * i + bone_at) < len(self.bones) for i in range(n)):
                raise ValueError('Invalid hitbox or attachment bone')
        return dict(anchor=ANCHOR, anchorIndex=anchor, bones=len(self.bones),
                    vertices=vertex_count, normals=normal_count, models=model_count,
                    sequences=self.sequences(), mode='unchanged-original-player-model')


def inspect_player(path: Path) -> dict:
    path = ordinary_path(path)
    data = path.read_bytes()
    studio = Studio(data)
    if ANCHOR not in {name for name, _ in studio.bones}:
        raise ValueError(f'Player lacks original hand anchor: {path.name}')
    actions = [name for name in studio.sequences()
               if name.startswith(('ref_aim_', 'ref_shoot_', 'ref_shoot2_', 'ref_reload_',
                                   'crouch_aim_', 'crouch_shoot_', 'crouch_shoot2_', 'crouch_reload_'))]
    return dict(name=path.stem, sha256=sha(data), bones=len(studio.bones),
                sequences=len(studio.sequences()), upperBodyActions=actions)


def prepare(source_root: Path, inventory: list[dict], output_root: Path | None,
            players: list[Path]) -> dict:
    if not isinstance(inventory, list) or not inventory:
        raise ValueError('A nonempty hash-pinned P-model inventory is required')
    source_root = ordinary_path(source_root)
    build = ordinary_path(WORKSPACE / 'build')
    if output_root is not None:
        output_root = ordinary_path(output_root)
        if not output_root.is_relative_to(build) or output_root == build:
            raise ValueError('Prepared game resources must stay in a dedicated build/ directory')
    target_players = [inspect_player(p) for p in players]
    resources, pending = [], []
    destinations = set()
    # Validate every input and every destination before writing any resource.
    for entry in inventory:
        path = ordinary_path(Path(entry['path']))
        if not path.is_relative_to(source_root):
            raise ValueError('Inventory source is outside source root')
        if not path.name.startswith('p_') or path.suffix.lower() != '.mdl':
            raise ValueError('Only original P model inventory entries are accepted')
        data = path.read_bytes()
        if sha(data) != entry['sha256']:
            raise ValueError(f'Source identity changed: {path.name}')
        result = Studio(data).player_weapon()
        relative = Path(path.parent.name) / path.name
        if str(relative).casefold() in destinations:
            raise ValueError('Duplicate output identity')
        destinations.add(str(relative).casefold())
        destination = ordinary_path(output_root / relative) if output_root else None
        if destination is not None and destination.exists() and destination.read_bytes() != data:
            raise ValueError('Refusing to overwrite another resource')
        resources.append(dict(path=relative.as_posix(), sha256=entry['sha256'], bytes=len(data), **result))
        pending.append((destination, data))
    if output_root:
        output_root.mkdir(parents=True, exist_ok=True)
        for destination, data in pending:
            destination.parent.mkdir(parents=True, exist_ok=True)
            if not destination.exists():
                descriptor, temp = tempfile.mkstemp(prefix='.prepare-', dir=destination.parent)
                try:
                    with os.fdopen(descriptor, 'wb') as stream:
                        stream.write(data)
                    os.replace(temp, destination)
                finally:
                    if os.path.exists(temp):
                        os.unlink(temp)
            if destination.read_bytes() != data:
                raise ValueError('Prepared bytes differ from source')
    return dict(schema=1, originalResources=len(resources), resources=resources,
                players=target_players, prepared=output_root is not None,
                validation='Original bytes and hand branch only; no visual grip, chosen animation, runtime or FPS acceptance')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--inventory', type=Path, required=True,
                        help='Hash-pinned original P-model evidence JSON list')
    parser.add_argument('--player', type=Path, action='append', default=[])
    parser.add_argument('--output-root', type=Path, help='Omit for read-only preflight')
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    report = ordinary_path(args.report)
    if report.suffix.lower() != '.json' or not report.is_relative_to(ordinary_path(WORKSPACE / 'build')):
        raise ValueError('Validation receipt must stay under build/')
    inventory = json.loads(args.inventory.read_text(encoding='utf-8-sig'))
    result = prepare(args.source_root, inventory, args.output_root, args.player)
    report.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix='.receipt-', dir=report.parent)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2)
        os.replace(temporary, report)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(json.dumps(dict(originalResources=result['originalResources'], players=len(result['players']),
                          prepared=result['prepared']), ensure_ascii=False))


if __name__ == '__main__':
    main()
