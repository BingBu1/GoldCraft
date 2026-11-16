#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check paired CSOL resource-update and Renderer interface markers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


INTERFACE = b'GoldCraftCsolResource002\0'
MOVING_LIGHTS = b'MetaRendererMovingLights_API_001\0'


def module(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError(f'Expected a PE module: {path}')
    pe = struct.unpack_from('<I', data, 60)[0]
    if (pe + 26 > len(data) or data[pe:pe + 4] != b'PE\0\0' or
            struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or
            struct.unpack_from('<H', data, pe + 24)[0] != 0x10b):
        raise ValueError(f'Expected an x86 PE32 module: {path}')
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                resourceRefreshMarker=INTERFACE in data, movingLightsMarker=MOVING_LIGHTS in data)


def verify_renderer(candidate, renderer=None):
    candidate = module(candidate)
    result = dict(candidate=candidate, movingLightsRequired=candidate['movingLightsMarker'],
                  boundary='Static interface-marker preflight; live factory registration, initialization and rendering require gameplay verification')
    if not result['movingLightsRequired']:
        return result
    if renderer is None or not Path(renderer).is_file():
        raise ValueError('CSOLSupport requires MetaRendererMovingLights_API_001; provide its paired Renderer DLL')
    result['renderer'] = module(renderer)
    if not result['renderer']['movingLightsMarker']:
        raise ValueError('CSOLSupport requires MetaRendererMovingLights_API_001; this Renderer cannot provide player lights')
    return result


def verify_renderer_rows(rows, prefix):
    names = {name: prefix + 'metahook/plugins/' + filename for name, filename in
             (('candidate', 'CSOLSupport.dll'), ('renderer', 'Renderer_AVX2.dll'))}
    sources = {}
    for row in rows:
        for name, path in names.items():
            if row['path'].casefold() == path.casefold():
                if name in sources:
                    raise ValueError('Duplicate paired client module: ' + path)
                sources[name] = row['source']
    if 'candidate' not in sources:
        return None
    return verify_renderer(sources['candidate'], sources.get('renderer'))


def verify(consumer, candidate=None, build=None):
    consumer = module(consumer)
    required = consumer['resourceRefreshMarker']
    result = dict(consumer=consumer, refreshRequired=required,
                  boundary='Static registration-marker preflight; actual CreateInterface/provider readiness requires a live client')
    if build is not None:
        cache = (Path(build) / 'CMakeCache.txt').read_text('utf-8-sig')
        match = re.search(r'^CSOL_RESOURCE_REFRESH:BOOL=(ON|OFF|TRUE|FALSE|1|0)$', cache, re.M)
        if not match:
            raise ValueError('CSOL_RESOURCE_REFRESH is missing or invalid in the client CMake cache')
        enabled = match[1] in {'ON', 'TRUE', '1'}
        if required and not enabled:
            raise ValueError('Installed GoldCraft requires CSOL resource refresh; configure CSOL_RESOURCE_REFRESH=ON before building')
        result['configuredResourceRefresh'] = enabled
    if candidate is not None:
        result['candidate'] = module(candidate)
        if required and not result['candidate']['resourceRefreshMarker']:
            raise ValueError('Installed GoldCraft requires GoldCraftCsolResource002; candidate CSOLSupport omits it')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, help='Optional installed GoldCraft resource-update consumer')
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--build', type=Path)
    parser.add_argument('--renderer', type=Path, help='Renderer installed with the candidate CSOLSupport')
    args = parser.parse_args()
    if args.candidate is None and args.build is None:
        parser.error('Provide --candidate or --build')
    if args.consumer is None and args.build is not None:
        parser.error('--build requires --consumer')
    result = verify(args.consumer, args.candidate, args.build) if args.consumer is not None else {}
    if args.candidate is not None:
        result['rendererCapabilities'] = verify_renderer(args.candidate, args.renderer)
    print(json.dumps(result))


if __name__ == '__main__':
    main()
