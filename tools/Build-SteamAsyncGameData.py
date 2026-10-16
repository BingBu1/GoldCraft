"""Build the read-only Steam context catalog for the verified HL10210 copies."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    paths = {name: getattr(args, name).resolve(strict=True) for name in ('engine', 'client')}
    output = args.output.resolve()
    if not all(p.is_relative_to(ROOT) for p in [*paths.values(), output]):
        raise ValueError('Inputs/output must stay inside the workspace')
    config = json.loads((ROOT / 'native/client/steam_async_10210.json').read_text())
    inspect = runpy.run_path(str(ROOT / 'tools/Inspect-EngineIdentity.py'))['inspect']
    pe_reader = runpy.run_path(str(ROOT / 'tools/Build-VisibleEntityGameData.py'))['engine_bytes']
    for module, path in paths.items():
        identity = inspect(path)
        wanted = config['binaries'][module]
        if identity['sha256'] != wanted['sha256'] or identity['crc64xz'].lower() != wanted['crc64']:
            raise ValueError(f'Unverified {module} identity')
        read = pe_reader(path)
        for record in config['records']:
            if record['module'] != module:
                continue
            p = record['payload']
            base = int(p['gv_va'], 16) - int(p['gv_rva'], 16)
            start = int(p['gv_sig_va'], 16) - base
            pattern = p['gv_sig'].split()
            actual = read(start, len(pattern))
            if any(value != '??' and actual[i] != int(value, 16) for i, value in enumerate(pattern)):
                raise ValueError(f'Changed reference: {record["symbolName"]}')
            operand = start + int(p['gv_inst_offset'], 16) + int(p['gv_inst_disp'], 16)
            if struct.unpack('<I', read(operand, 4))[0] != int(p['gv_va'], 16):
                raise ValueError('Context operand mismatch')
    version = 'goldcraft-steam-hl-10210'
    snapshot = {'schemaVersion': 5, 'source': {'snapshotSchemaVersion': 8,
        'analysisOutputContractVersion': 3, 'gameVersion': version},
        'binaries': {k: {'windows': {'crc64': v['crc64']}} for k, v in config['binaries'].items()},
        'records': config['records']}
    payload = (json.dumps(snapshot, indent=2) + '\n').encode()
    index = {'schemaVersion': 4, 'versions': [{'gameVersion': version, 'url': version + '.json',
        'sha256': hashlib.sha256(payload).hexdigest(), 'size': len(payload),
        'snapshotSchemaVersion': 8, 'fileCount': len(config['records'])}]}
    output.mkdir(parents=True, exist_ok=True)
    (output / (version + '.json')).write_bytes(payload)
    (output / 'index.json').write_text(json.dumps(index, indent=2) + '\n', encoding='utf-8')
    print('Verified 2 module identities and 3 read-only Steam context references')


if __name__ == '__main__':
    main()
