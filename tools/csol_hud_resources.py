#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Preflight the authoritative server's complete current CSOL HUD resources.

Read-only deployment check. Client root files may intentionally be overridden by
verified VFS/cache objects, so this check targets the server download source.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import struct

ROOT = Path(__file__).resolve().parent.parent


def contract(source=ROOT):
    header = (Path(source) / 'native/csol/hud_style.hpp').read_text('utf-8-sig')
    constants = dict(re.findall(r'inline constexpr char (CatalogPath|FontPath|SchemePath)\[\] = "([^"]+)";', header))
    body = re.search(r'\bResources\{\{(.*?)\}\};', header, re.S)
    if set(constants) != {'CatalogPath', 'FontPath', 'SchemePath'} or not body:
        raise ValueError('HUD resource contract must be reviewed after a source format change')
    images = re.findall(r'\{"([^"]+)",(\d+),(\d+)\}', body[1])
    if not images or len(images) != body[1].count('{'):
        raise ValueError('Incomplete HUD image contract')
    return constants, {path: (int(w), int(h)) for path, w, h in images}


def dependencies(data, constants, images):
    # Match hud_style.cpp ParseCatalog/ResourcePaths, including optional
    # selected art and all explicit ammunition frames; never accept a subset.
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate HUD catalog member: ' + key)
            result[key] = value
        return result

    value = json.loads(data, object_pairs_hook=unique)
    if not isinstance(value, dict) or type(value.get('version')) is not int or value['version'] != 1:
        raise ValueError('Invalid HUD catalog version')
    paths, names, portraits = set(constants.values()) | set(images), set(), set()

    def image(value, optional=False):
        if not isinstance(value, str) or len(value.encode('utf8')) > 63:
            raise ValueError('Invalid HUD image path')
        if optional and not value:
            return
        if (not value.startswith('resource/csol/hud/v1/') or not value.endswith('.tga') or
                '..' in value or any(char in value for char in '\0:\\"\r\n\t ')):
            raise ValueError('Unsafe HUD image path')
        paths.add(value)

    for key in ('weapons', 'portraits'):
        if not isinstance(value.get(key), list) or len(value[key]) > 1024:
            raise ValueError('Invalid HUD catalog array')
    for row in value['weapons']:
        name, title = row['name'], row['title']
        if (not isinstance(name, str) or len(name) > 63 or not re.fullmatch(r'weapon_[a-z0-9_]+', name) or name in names or
                not isinstance(title, str) or not title or len(title.encode('utf8')) > 192 or any(c in title for c in '\0\r\n\t')):
            raise ValueError('Invalid HUD weapon row')
        names.add(name)
        image(row['weapon'])
        for key in ('ammo', 'ammo2'):
            image(row[key], True)
        if 'selected' in row:
            image(row['selected'], True)
        if 'ammoFrames' in row:
            frames = row['ammoFrames']
            if not isinstance(frames, list) or len(frames) != 6 or not row['ammo'] or frames[0] != row['ammo']:
                raise ValueError('Invalid HUD ammunition frames')
            for frame in frames:
                image(frame)
    if not names:
        raise ValueError('Empty HUD weapon catalog')
    for row in value['portraits']:
        if type(row['id']) is not int or not 0 < row['id'] <= 65535 or row['id'] in portraits:
            raise ValueError('Invalid HUD portrait')
        portraits.add(row['id'])
        image(row['path'])
    return sorted(paths)


def verify(directory, source=ROOT):
    directory = Path(directory).resolve(strict=True)
    constants, images = contract(source)
    parents = {directory}

    def file_info(name):
        relative = PurePosixPath(name)
        if not relative.parts or relative.is_absolute() or '..' in relative.parts or any(c in name for c in ':\\'):
            raise ValueError('HUD dependency escapes server resource root')
        path, pending = directory / relative, []
        parent = path.parent
        while parent not in parents:
            pending.append(parent)
            parent = parent.parent
        # CPU sampling found repeated GetFinalPathName calls dominated this
        # cold check. Validate shared parents once and each leaf once; reject
        # reparse points rather than resolving every resource through them.
        for parent in reversed(pending):
            info = parent.lstat()
            if not stat.S_ISDIR(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('Reparse or non-directory HUD parent: ' + str(parent))
            parents.add(parent)
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Reparse HUD resource: ' + str(path))
        return path, info

    catalog = directory / constants['CatalogPath']
    catalog, info = file_info(constants['CatalogPath'])
    if not stat.S_ISREG(info.st_mode) or not 0 < info.st_size <= 1024 * 1024:
        raise ValueError('Missing or oversized current HUD catalog: ' + str(catalog))
    data = catalog.read_bytes()
    paths = dependencies(data.decode('utf-8-sig'), constants, images)
    missing, invalid = [], []
    for name in paths:
        try:
            path, info = file_info(name)
        except (FileNotFoundError, NotADirectoryError):
            missing.append(name)
            continue
        if not stat.S_ISREG(info.st_mode) or info.st_size <= 0:
            missing.append(name)
        elif name in images:
            with path.open('rb') as stream:
                header = stream.read(18)
            if len(header) != 18 or struct.unpack_from('<HH', header, 12) != images[name]:
                invalid.append(name)
    result = dict(success=not missing and not invalid, root=str(directory), catalog=constants['CatalogPath'],
                  catalogSha256=hashlib.sha256(data).hexdigest(), resources=len(paths), missing=missing, invalidDimensions=invalid,
                  boundary='Server download closure and fixed image dimensions; client VFS mounting/rendering require live verification')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True, help='Authoritative server cstrike download source')
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args = parser.parse_args()
    try:
        result = verify(args.root, args.source_root)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = dict(success=False, error=str(error))
    print(json.dumps(result, ensure_ascii=True))
    raise SystemExit(0 if result['success'] else 1)
