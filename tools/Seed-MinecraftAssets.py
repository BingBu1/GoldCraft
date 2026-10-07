"""Reuse only hash-verified objects from the user's existing Minecraft asset cache."""
import hashlib
import argparse
import json
import os
from pathlib import Path
import shutil
import uuid

root = Path(__file__).resolve().parent.parent
destination = root / '.tools/gradle-cache/caches/fabric-loom/assets'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True,
                    help='Existing Minecraft assets/objects directory (read only)')
source = parser.parse_args().source.resolve(strict=True)
index = json.loads((destination / 'indexes/1.21-17.json').read_text(encoding='utf-8'))
copied = found = absent = 0
for asset in index['objects'].values():
    digest = asset['hash']
    if len(digest) != 40 or any(c not in '0123456789abcdef' for c in digest):
        raise ValueError('Invalid asset hash')
    target = destination / 'objects' / digest[:2] / digest
    if target.is_file():
        found += 1
        continue
    original = source / digest[:2] / digest
    if not original.is_file() or original.stat().st_size != asset['size']:
        absent += 1
        continue
    with original.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha1').hexdigest() != digest:
            absent += 1
            continue
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(digest + '.goldcraft-' + uuid.uuid4().hex + '.tmp')
    try:
        shutil.copyfile(original, temporary)
        # On Windows rename refuses an existing target; leave an independently downloaded object alone.
        try:
            os.rename(temporary, target)
            copied += 1
        except FileExistsError:
            found += 1
    finally:
        temporary.unlink(missing_ok=True)
print(json.dumps({'copiedVerified': copied, 'alreadyPresent': found, 'requiresDownload': absent}))
