#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real catalog publisher against the matched PE and bad metadata."""
import argparse
import contextlib
import copy
import hashlib
import io
import json
import runpy
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def save(path, data):
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2), 'utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    out.mkdir()
    fixture = out / 'root'
    files = ['tools/Build-ClientGameData.py', 'tools/Inspect-EngineIdentity.py']
    files += ['native/client/' + name for name in ('vitals_10210.json', 'motion_10210.json',
        'spectator_menu_10210.json', 'hud_clock_10210.json', 'shared_random_10210.json',
        'native_hud_client_10210.json')]
    snapshots = []
    for relative in files:
        original = ROOT / relative
        target = fixture / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(original.read_bytes())
        snapshots.append(dict(original=identity(original), frozen=identity(target)))
    client = fixture / 'client.dll'
    client.write_bytes(args.client.read_bytes())
    assert hashlib.sha256(client.read_bytes()).hexdigest() == 'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2'
    tool = runpy.run_path(str(fixture / 'tools/Build-ClientGameData.py'))
    metadata = fixture / 'native/client/shared_random_10210.json'
    original_metadata = metadata.read_bytes()
    baseline = json.loads(original_metadata)
    cases = [('valid', None, None), ('wrong-module', 'module', 'wrong-runtime.dll'),
             ('wrong-name', 'importName', 'srand'), ('misaligned-iat', 'iatRva', '0xbf221'),
             ('other-iat', 'iatRva', '0xbf224'), ('wrong-name-rva', 'nameRva', '0xe9ad9'),
             ('wrong-metadata-identity', 'clientSha256', '0' * 64)]
    results = []
    argv = sys.argv
    try:
        for name, key, value in cases:
            candidate = copy.deepcopy(baseline)
            if key == 'clientSha256':
                candidate[key] = value
            elif key:
                candidate['imports'][0][key] = value
            save(metadata, candidate)
            recorded = out / (name + '-metadata.json')
            recorded.write_bytes(metadata.read_bytes())
            destination = fixture / 'output' / name
            sys.argv = ['Build-ClientGameData.py', '--client', str(client), '--output', str(destination)]
            output = io.StringIO()
            error = None
            try:
                with contextlib.redirect_stdout(output):
                    tool['main']()
            except ValueError as exception:
                error = str(exception)
            valid = name == 'valid'
            assert (error is None) == valid, (name, error)
            if valid:
                catalog = json.loads((destination / 'cs-client-10210-goldcraft.json').read_text('utf-8'))
                matches = [r for r in catalog['records'] if r['symbolName'] == 'CSOL_ClientCRTRand']
                assert len(matches) == 1 and matches[0]['kind'] == 'global'
                payload = matches[0]['payload']
                assert payload == {
                    'gv_rva': '0xbf220', 'gv_va': '0x100bf220',
                    'gv_sig': 'FF 15 ?? ?? ?? ??', 'gv_sig_va': '0x1003743a',
                    'gv_inst_offset': '0x0', 'gv_inst_disp': '0x2', 'gv_inst_length': '0x6'}
                # Verify the emitted operand against the exact original PE,
                # independently of the publisher's instruction validation.
                image = client.read_bytes()
                pe = struct.unpack_from('<I', image, 0x3c)[0]
                count = struct.unpack_from('<H', image, pe + 6)[0]
                optional = struct.unpack_from('<H', image, pe + 20)[0]
                reference = int(payload['gv_sig_va'], 16) - 0x10000000
                instruction = None
                for section in range(count):
                    offset = pe + 24 + optional + section * 40
                    _, rva, size, raw = struct.unpack_from('<IIII', image, offset + 8)
                    if rva <= reference and reference + 6 <= rva + size:
                        instruction = image[raw + reference - rva:raw + reference - rva + 6]
                assert instruction == bytes.fromhex('FF 15 20 F2 0B 10')
                assert struct.unpack_from('<I', instruction, int(payload['gv_inst_disp'], 16))[0] == int(payload['gv_va'], 16)
            else:
                assert not destination.exists(), 'rejected metadata must not publish output'
                assert error == ('Shared random catalog identity mismatch' if key == 'clientSha256'
                                 else 'Client CRT import changed: CSOL_ClientCRTRand')
            results.append(dict(name=name, metadata=identity(recorded), expectedSuccess=valid,
                                error=error, stdout=output.getvalue(), passed=True))
    finally:
        sys.argv = argv
        metadata.write_bytes(original_metadata)
    changed = [row['frozen']['path'] for row in snapshots if identity(row['frozen']['path']) != row['frozen']]
    assert not changed
    save(out / 'result.json', dict(success=True, cases=results, inputs=snapshots,
        originalClient=identity(args.client), frozenClient=identity(client), runner=identity(__file__),
        changed=changed, deployed=False, boundary='Actual catalog publisher and exact PE import validation; no runtime deployment or gameplay.'))
    print(json.dumps(dict(passed=len(results), failures=0, output=str(out))))


if __name__ == '__main__':
    main()
