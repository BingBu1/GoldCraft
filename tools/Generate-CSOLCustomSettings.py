#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate the typed custom-settings example with the compiled pure codec.

The parser executable owns defaults and Chinese explanations; this tool keeps
no second table. With no --output, print JSON. Existing files are never replaced.
This is an offline authoring helper, not the live administrator persistence API.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def plain(path):
    for part in (path, *path.parents):
        if part.is_symlink() or getattr(part, 'is_junction', lambda: False)():
            raise ValueError('Refusing redirected path: ' + str(part))
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True, type=Path,
                        help='Compiled csol_custom_settings_tests executable')
    parser.add_argument('--output', type=Path,
                        help='New file inside this workspace; omitted prints JSON')
    args = parser.parse_args()
    executable = plain(args.executable.absolute()).resolve(strict=True)
    if not executable.is_relative_to(ROOT):
        raise ValueError('Use the workspace-built pure settings executable')
    result = subprocess.run([str(executable), '--example'], cwd=ROOT,
                            capture_output=True, check=True, timeout=30)
    text = result.stdout.decode('utf-8', errors='strict').replace('\r\n', '\n')
    document = json.loads(text)
    version = document.get('version')
    if version == 1:
        valid = set(document) == {'version', 'features', 'values', 'comments'}
    elif version == 2:
        valid = set(document) == {'version', 'features'} and isinstance(document['features'], dict)
        for feature in document['features'].values():
            valid = valid and isinstance(feature, dict) and set(feature) == {'enabled', 'comment', 'values'}
            if not valid:
                break
            valid = isinstance(feature['enabled'], bool) and isinstance(feature['comment'], str) and \
                isinstance(feature['values'], dict)
    else:
        valid = False
    if not valid:
        raise ValueError('Codec did not produce a supported single-file settings envelope')
    if args.output is None:
        sys.stdout.buffer.write(text.encode('utf-8'))
        return 0
    output = plain(args.output.absolute())
    parent = output.parent.resolve(strict=True)
    if not parent.is_relative_to(ROOT):
        raise ValueError('Output must remain within this workspace')
    output = parent / output.name
    # Exclusive creation makes this an authoring helper, never an in-place
    # writer that could truncate a hard-linked runtime or frozen resource.
    with output.open('xb') as stream:
        stream.write(text.encode('utf-8'))
    print(output)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
