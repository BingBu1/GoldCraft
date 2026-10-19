"""Required post resources from the native source contract, without shader contents.

Both package paths validate this contract before producing output. A recursive
directory walk alone cannot detect a required file that was never deployed.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

HEADER = Path(__file__).resolve().parents[2] / 'native/client/post_effect_assets.hpp'
POST_DIRECTORY = 'metahook/shaders/goldcraft/vix-post/'


def post_contract():
    source = HEADER.read_text(encoding='utf-8')
    declaration = re.search(r'inline constexpr std::array<Asset,(\d+)> shaders\{\{(.*?)\}\};', source, re.S)
    if declaration is None:
        raise ValueError('Missing native post shader contract')
    entries = re.findall(r'\{"([a-z_]+\.fsh)","([a-f0-9]{64})"\}', declaration[2])
    remainder = re.sub(r'\{"[a-z_]+\.fsh","[a-f0-9]{64}"\}', '', declaration[2])
    if (not entries or len(entries) != int(declaration[1]) or
            re.sub(r'[\s,]', '', remainder) or len({name for name, _ in entries}) != len(entries)):
        raise ValueError('Malformed native post shader contract')
    resources = {POST_DIRECTORY + name: digest for name, digest in sorted(entries)}
    identity = hashlib.sha256(json.dumps(resources, sort_keys=True, separators=(',', ':')).encode('ascii')).hexdigest()
    return dict(schema=1, sha256=identity, resources=resources)


def verify_post_rows(rows, prefix, contract):
    """Check actual planned payload, including plans loaded from disk."""
    if contract != post_contract():
        raise ValueError('Post shader contract changed or missing; create a new package plan')
    indexed = {}
    for row in rows:
        key = row['path'].casefold()
        if key in indexed:
            raise ValueError('Duplicate resource destination: ' + row['path'])
        indexed[key] = row
    for name, expected in contract['resources'].items():
        destination = prefix + name
        row = indexed.get(destination.casefold())
        if row is None:
            raise ValueError('Missing required post shader: ' + destination)
        if row.get('sha256') != expected or type(row.get('bytes')) is not int or row['bytes'] <= 0:
            raise ValueError('Post shader differs from native contract: ' + destination)


def verify_post_runtime(game, safe):
    """Read only: report missing/stale resources before creating package output."""
    contract = post_contract()
    for name, expected in contract['resources'].items():
        path = safe(Path(game) / 'cstrike' / name)
        if not path.is_file():
            raise ValueError('Missing required post shader: ' + name)
        with path.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected:
            raise ValueError('Post shader differs from native contract: ' + name)
    return contract
