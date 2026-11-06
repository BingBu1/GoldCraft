#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the verified original wall Default font into an isolated host scheme."""
from pathlib import Path
import argparse
import hashlib
import json
import runpy

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA = '308f479fa407df14e69e811e066dcc2d1f35aa8ee9f717d2344ab603e08e93d9'
BLOCK_SHA = '3e910ae288743ec0cbf46921f70902a2555cb0516321c1aef3d6218cdbd64538'
RESOURCE = 'resource/csol/wall/v1/WallUpgradeScheme.res'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Verified original locale_chn/resource/ClientScheme.res')
    parser.add_argument('--output', type=Path, required=True, help='New directory under the workspace build folder')
    args = parser.parse_args()
    for path in (args.source.absolute(), *args.source.absolute().parents,
                 args.output.absolute(), *args.output.absolute().parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError(f'Reparse path is not accepted: {path}')
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('Output must be a new workspace build directory')
    data = args.source.read_bytes()
    if digest(data) != SOURCE_SHA:
        raise ValueError('Original ClientScheme identity changed; review before generating')
    block_reader = runpy.run_path(str(ROOT / 'tools/Prepare-CSOLHud.py'))['scheme_block']
    _, fonts_start, fonts_end = block_reader(data, 'Fonts')
    block, start, end = block_reader(data, 'Default')
    if not fonts_start < start < end <= fonts_end or digest(block) != BLOCK_SHA:
        raise ValueError('Original Default font block changed')
    scheme = (b'"Scheme"\n{\n\t"BaseSettings"\n\t{\n\t\t"CSOLWallUpgradeRevision" "1"\n\t}\n'
              b'\t"Fonts"\n\t{\n' + block + b'\t}\n}\n')
    target = output / RESOURCE
    target.parent.mkdir(parents=True)
    target.write_bytes(scheme)
    receipt = dict(resource=RESOURCE, bytes=len(scheme), sha256=digest(scheme),
                   originalSha256=SOURCE_SHA, blockSha256=BLOCK_SHA, byteStart=start, byteEnd=end,
                   font='resource/font/df_gb_y9.ttf',
                   adaptation='Complete original Default font block, isolated scheme/tag; font registered before loading. Host scheme owns glyph sizing.')
    (output / 'manifest.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(json.dumps(receipt))


if __name__ == '__main__':
    main()
