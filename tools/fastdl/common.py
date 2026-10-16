"""Shared FastDL manifest and path contract. No game files are executed."""
from pathlib import Path
from urllib.parse import urlsplit
import hashlib
import json
import os
import re
import stat

SCHEMA = 1
MAX_FILE = (1 << 24) - 1  # ReHLDS resource size field is 24 bits.
MAX_FILES = 16384
MAX_TOTAL = 4 * 1024 ** 3
MANIFEST_LIMIT = 8 * 1024 ** 2
CHUNK = 128 * 1024
KINDS = {
    'maps': {'.bsp', '.res', '.txt'}, 'models': {'.mdl'},
    'sound': {'.wav', '.mp3'}, 'sprites': {'.spr', '.txt'},
    'gfx': {'.tga', '.bmp'}, 'overviews': {'.txt', '.bmp', '.tga'},
    'events': {'.sc'},
}
RESERVED = re.compile(r'^(con|prn|aux|nul|com[0-9]|lpt[0-9])(?:\.|$)', re.I)


def plain(path):
    """Check every existing ancestor before resolve follows links/junctions."""
    path = Path(path).absolute()
    for part in (path, *path.parents):
        try:
            s = part.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(s.st_mode) or getattr(s, 'st_file_attributes', 0) & 0x400:
            raise ValueError('Reparse/symlink path: ' + str(part))
    resolved = str(path.resolve())
    # Windows realpath may retain the extended prefix if a missing parent is
    # created by another worker during resolution. Both forms name the same
    # file, but pathlib treats their anchors as different. Normalize known
    # drive/UNC prefixes before containment comparisons; never relax the fence.
    if os.name == 'nt':
        if resolved.startswith('\\\\?\\UNC\\'):
            resolved = '\\\\' + resolved[8:]
        elif resolved.startswith('\\\\?\\') and re.match(r'[A-Za-z]:\\', resolved[4:]):
            resolved = resolved[4:]
    return Path(resolved)


def resource_name(name):
    """Conservative GoldSrc + UDP fallback compatible asset path allowlist."""
    if not isinstance(name, str) or not name or len(name) > (69 if name.startswith('sound/') else 63):
        raise ValueError('Resource name length')
    if any(ord(c) < 32 or ord(c) > 126 or c in '\\:%?#<>"|*~' for c in name):
        raise ValueError('Unsafe resource characters: ' + name)
    parts = name.split('/')
    if any(not p or p in ('.', '..') or p.startswith('.') or p.endswith((' ', '.'))
           or RESERVED.match(p) for p in parts):
        raise ValueError('Unsafe resource component: ' + name)
    # ReHLDS IsSafeFileToDownload accepts exactly one dot and a 3-char suffix.
    if name.count('.') != 1 or (len(Path(name).suffix) != 4 and not name.startswith('events/')):
        raise ValueError('Resource is not compatible with native fallback: ' + name)
    suffix = Path(name).suffix.lower()
    if len(parts) == 1:
        if suffix != '.wad' or any(x in name.lower() for x in ('halflife.wad', 'xeno.wad')) or name.lower() in {'cached.wad', 'tempdecal.wad', 'pldecal.wad', 'spraypaint.wad'}:
            raise ValueError('Not a downloadable root asset: ' + name)
    elif suffix not in KINDS.get(parts[0].lower(), set()):
        raise ValueError('Not an allowed asset: ' + name)
    return name


def udp_compatible(name):
    # ReHLDS rejects two-character extensions such as event scripts (.sc).
    return len(Path(resource_name(name)).suffix) == 4


def child(root, name):
    root = plain(root)
    result = plain(root / name)
    if not result.is_relative_to(root) or result == root:
        raise ValueError(f'Path escaped root: {result!s} (root {root!s})')
    return result


def digest(path):
    with plain(path).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def canonical(value):
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(',', ':')) + '\n').encode()


def size_hash(row, limit=MAX_FILE):
    if not isinstance(row, dict) or type(row.get('bytes')) is not int or not 0 < row['bytes'] <= limit:
        raise ValueError('Invalid resource size')
    if not isinstance(row.get('sha256'), str) or not re.fullmatch('[0-9a-f]{64}', row['sha256']):
        raise ValueError('Invalid SHA256')


def manifest(value):
    if not isinstance(value, dict) or type(value.get('schema')) is not int or value['schema'] != SCHEMA or not isinstance(value.get('files'), list) or not 0 < len(value['files']) <= MAX_FILES:
        raise ValueError('Invalid manifest')
    seen, total = set(), 0
    for row in value['files']:
        if not isinstance(row, dict):
            raise ValueError('Invalid manifest entry')
        name = resource_name(row.get('path'))
        if type(row.get('udpFallback')) is not bool or row['udpFallback'] != udp_compatible(name):
            raise ValueError('Invalid native fallback capability')
        if name.casefold() in seen:
            raise ValueError('Case-insensitive duplicate: ' + name)
        seen.add(name.casefold())
        size_hash(row)
        total += row['bytes']
        if 'gzip' in row:
            gz = row['gzip']
            size_hash(gz)
            if gz.get('path') != '_fastdl/' + gz['sha256'] + '.gz' or gz['bytes'] >= row['bytes']:
                raise ValueError('Invalid compressed variant')
    if total > MAX_TOTAL:
        raise ValueError('Manifest total exceeds budget')
    return value


def base_url(url):
    if not isinstance(url, str) or any(ord(c) <= 32 or ord(c) >= 127 for c in url):
        raise ValueError('URL must be ASCII without whitespace')
    # This value also enters a GoldSrc console config. Reject command/quote
    # delimiters everywhere, including the authority (not only the URL path).
    if not re.fullmatch(r'[A-Za-z0-9:/._\[\]-]+', url):
        raise ValueError('URL contains unsupported or console-control characters')
    p = urlsplit(url)
    if p.scheme not in ('http', 'https') or not p.hostname or p.username or p.password or p.query or p.fragment:
        raise ValueError('Expected HTTP(S) base URL without credentials/query/fragment')
    _ = p.port
    if any(x in p.path for x in ['%', '..', '\\', '"']) or not p.path.endswith('/'):
        raise ValueError('Use a plain base URL ending with /')
    if len(url.encode('ascii')) > 127:
        raise ValueError('sv_downloadurl exceeds 127-byte client storage limit')
    return url
