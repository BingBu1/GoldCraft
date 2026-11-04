"""Widen weapon IDs and ammo in a workspace server's existing delta schema.

The engine ABI is unchanged. Preserve every unrelated field, precision and
encoder. This does not copy CSO's incompatible entvars/clientdata layout.
"""
import argparse
from pathlib import Path
import re


def mask_comments(text: str) -> str:
    """Preserve offsets/newlines so only live schema fields can be edited."""
    return re.sub(r'/\*.*?\*/|//[^\r\n]*',
                  lambda m: re.sub(r'[^\r\n]', ' ', m[0]), text, flags=re.S)


def expand(data: bytes) -> bytes:
    text = data.decode('latin1')
    schema = mask_comments(text)
    client = re.search(r'(?m)^clientdata_t\s+[^\r\n]+\s*\{([^}]*)\}', schema)
    permission = re.search(
        r'DEFINE_DELTA\(\s*iuser3\s*,\s*DT_INTEGER\s*,\s*(\d+)\s*,\s*1\.0\s*\)',
        client[1] if client else '')
    if not permission or int(permission[1]) < 8:
        raise ValueError('A Counter-Strike delta schema with clientdata_t.iuser3 is required; '
                         'use the matching ReGameDLL_CS/dist/delta.lst, not valve/delta.lst')
    # UpdateClientData sends all stock reserves plus the active weapon's
    # reserve here. The original 9-bit fields saturate at 511, even when the
    # reliable CSOL HUD messages contain the full count. Prediction must see
    # the same 0..32767 range as the extended ammo protocol.
    for table, fields in {
        'clientdata_t': {
            'm_iId': 16,
            'ammo_nails': 16, 'ammo_shells': 16,
            'ammo_cells': 16, 'ammo_rockets': 16,
            'vuser2[0]': 15, 'vuser2[1]': 15, 'vuser2[2]': 15,
            'vuser3[0]': 15, 'vuser3[1]': 15, 'vuser3[2]': 15,
            'vuser4[1]': 15,
        },
        'weapon_data_t': {'m_iId': 16, 'm_iClip': 16},
    }.items():
        pattern = rf'(?m)^{table}\s+[^\r\n]+\s*\{{([^}}]*)\}}'
        matches = list(re.finditer(pattern, mask_comments(text)))
        if len(matches) != 1:
            raise ValueError(f'Expected one {table} schema')
        match = matches[0]
        body = text[match.start(1):match.end(1)]
        for name, width in fields.items():
            field = rf'(DEFINE_DELTA\(\s*{re.escape(name)}\s*,\s*[^,]+,\s*)(\d+)'
            found = list(re.finditer(field, mask_comments(body)))
            if len(found) != 1:
                raise ValueError(f'Expected one {table}.{name} field')
            item = found[0]
            body = body[:item.start(2)] + str(max(width, int(item[2]))) + body[item.end(2):]
        text = text[:match.start(1)] + body + text[match.end(1):]
    return text.encode('latin1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    target = args.output.resolve()
    if not target.is_relative_to(root) or target == args.source.resolve():
        raise ValueError('Use a separate output file inside the workspace')
    data = expand(args.source.read_bytes())
    if target.exists():
        if target.read_bytes() != data:
            raise ValueError('Output differs; preserve and resolve the previous schema explicitly')
        print('Weapon delta already matches; no file written.')
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    with target.open('xb') as output:
        output.write(data)
    print('Weapon ID, clip and reserve delta fields prepared; unrelated fields preserved.')


if __name__ == '__main__':
    main()
