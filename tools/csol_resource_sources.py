"""Resolve reviewed HUD atlas leaves without changing the collector's model graph."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat

from csol_resource_order import MOUNT_ORDER


def hud_atlas(name):
    return re.fullmatch(r'sprites/640hud\d+\.spr', name, re.IGNORECASE) is not None


def checked_path(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            state = item.lstat()
            if item.is_symlink() or getattr(state, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError('Resource source traverses a reparse point: ' + str(item))
    return path.resolve()


def relative_name(name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('Invalid source resource path: ' + name)
    return path


def identity(data):
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


class ResourceSources:
    def __init__(self, source, overrides=None):
        self.source = checked_path(source)
        self.overrides = {}
        self.used = set()
        self.records = {}
        self.override_manifest = None
        if overrides is None:
            return
        path = checked_path(overrides)
        data = path.read_bytes()
        manifest = json.loads(data)
        if manifest.get('version') != 1 or not isinstance(manifest.get('overrides'), list):
            raise ValueError('Expected resource source overrides version 1')
        self.override_manifest = {'path': str(path), **identity(data)}
        for row in manifest['overrides']:
            name = row['relative']
            relative_name(name)
            if not hud_atlas(name):
                raise ValueError('Source override must be an independent HUD atlas: ' + name)
            key = name.casefold()
            if key in self.overrides:
                raise ValueError('Duplicate source override: ' + name)
            for size_key, sha_key in (('bytes', 'sha256'), ('expectedBaseBytes', 'expectedBaseSha256')):
                if (type(row.get(size_key)) is not int or row[size_key] <= 0 or
                        not isinstance(row.get(sha_key), str) or not re.fullmatch('[a-f0-9]{64}', row[sha_key])):
                    raise ValueError('Source override requires exact candidate and current-base identities: ' + name)
            original = Path(row['source'])
            if not original.is_absolute():
                original = path.parent / original
            self.overrides[key] = (row, checked_path(original))

    def locate(self, name):
        relative = relative_name(name)
        # MDL/material dependency discovery has already run against source.
        # Only independent HUD SPR leaves can use a later mounted override.
        if self.source.name.casefold() == 'common' and hud_atlas(name):
            roots = (self.source.parent / mount for mount in MOUNT_ORDER)
        else:
            roots = (self.source,)
        for root in roots:
            path = checked_path(root / relative)
            if path.is_file():
                return path
        raise FileNotFoundError(self.source / relative)

    def read(self, name):
        base = self.locate(name)
        data = base.read_bytes()
        record = {'base': {'path': str(base), **identity(data)}}
        path = base
        override = self.overrides.get(name.casefold())
        if override:
            row, path = override
            if record['base']['bytes'] != row['expectedBaseBytes'] or record['base']['sha256'] != row['expectedBaseSha256']:
                raise ValueError('Source override current-base identity changed: ' + name)
            data = path.read_bytes()
            if identity(data) != {'bytes': row['bytes'], 'sha256': row['sha256']}:
                raise ValueError('Source override candidate identity changed: ' + name)
            self.used.add(name.casefold())
        record.update(path=str(path), override=override is not None, **identity(data))
        if name in self.records and self.records[name] != record:
            raise ValueError('Resource source changed during preparation: ' + name)
        self.records[name] = record
        return data

    def finish(self):
        unused = set(self.overrides) - self.used
        if unused:
            raise ValueError('Source override was not consumed by this dependency closure: ' + ', '.join(sorted(unused)))
