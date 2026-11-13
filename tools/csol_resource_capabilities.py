#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Reject a CSOL client that removes its installed resource-update provider."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


INTERFACE = b'GoldCraftCsolResource002\0'


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
                resourceRefreshMarker=INTERFACE in data)


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
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--candidate', type=Path)
    parser.add_argument('--build', type=Path)
    args = parser.parse_args()
    if args.candidate is None and args.build is None:
        parser.error('Provide --candidate or --build')
    print(json.dumps(verify(args.consumer, args.candidate, args.build)))


if __name__ == '__main__':
    main()
