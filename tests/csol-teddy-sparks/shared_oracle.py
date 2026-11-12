#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare real shared-runtime rings to original instructions with the real CRT boundary."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tests/csol-teddy-blackhole-smoke'))
import originals as O
from unicorn.x86_const import UC_X86_REG_XMM0


def execute(exe, mode, lines=None):
    result = subprocess.run([str(exe), mode], input=None if lines is None else '\n'.join(lines) + '\n',
                            capture_output=True, text=True, cwd=ROOT, timeout=60)
    if result.returncode:
        raise RuntimeError(mode + ' failed: ' + result.stderr)
    return [json.loads(line) for line in result.stdout.splitlines()]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for key in ('executable', 'client', 'evidence', 'output'):
        ap.add_argument('--' + key, type=Path, required=True)
    a = ap.parse_args()
    out = a.output.resolve()
    if out.exists():
        raise FileExistsError(out)
    exe = a.executable.resolve(strict=True)
    D = a.evidence.resolve(strict=True)
    Ring, _, inputs, ida = O.make_originals(ROOT, a.client.resolve(strict=True), D)
    identity_rows = execute(exe, '--math-identity')
    if len(identity_rows) != 1:
        raise AssertionError('Expected one loaded CRT identity')
    runtime = Path(identity_rows[0]['path']).resolve(strict=True)
    runtime_bytes = runtime.read_bytes()
    pe = struct.unpack_from('<I', runtime_bytes, 0x3c)[0]
    if runtime_bytes[:2] != b'MZ' or struct.unpack_from('<H', runtime_bytes, pe + 4)[0] != 0x14c:
        raise AssertionError('Loaded precise math module is not the actual x86 PE')
    inputs += [O.identity(exe), O.identity(Path(__file__)), O.identity(runtime),
               O.identity(ROOT / 'native/csol/client/scenario/core/particle_effects.cpp'),
               O.identity(ROOT / 'native/csol/client/scenario/core/particle_effects.hpp'),
               O.identity(ROOT / 'native/csol/client/scenario/core/precise_trigonometry.hpp')]
    reference = json.loads((D / 'server-smoke-reference.json').read_text('utf8'))
    packet = bytes.fromhex(reference['packet']['payload'])
    first = Ring()
    first.ring(packet.hex())
    angles = [entry['input'] for entry in first.math if entry['operation'] == 'cos']
    actual_math = execute(exe, '--math-values', [repr(value) for value in angles])
    if len(actual_math) != len(angles):
        raise AssertionError('Missing native math rows')
    lookup = {value: result for value, result in zip(angles, actual_math)}

    class NativeMathRing(Ring):
        def hook(self, u, address, size, context):
            if address in (0x2da898d, 0x2da8891):
                raw = u.reg_read(UC_X86_REG_XMM0).to_bytes(16, 'little')
                value = struct.unpack('<d', raw[:8])[0]
                if value not in lookup:
                    raise AssertionError('Unexpected original CRT input')
                index = int(address == 0x2da8891)
                output_bits = lookup[value][index]
                self.math.append(dict(operation='sin' if index else 'cos', input=value,
                                      outputBits64=output_bits))
                u.reg_write(UC_X86_REG_XMM0, output_bits)
                self.ret()
                return
            super().hook(u, address, size, context)

    packets = [packet]
    for center in ((-32768, -1, 32767), (0, 0, 0), (1, -32768, 32767), (32767, 32767, -32768)):
        packets.append(packet[:-6] + struct.pack('<3h', *center))
    expected, original_calls = [], []
    base = 0x3c30a88 + 0xb414
    for payload in packets:
        original = NativeMathRing()
        original.ring(payload.hex())
        records = []
        for i in range(36):
            at = base + i * 0x168
            floats = (*original.floats(at + 0x34, 3), *original.floats(at + 0x4c, 3),
                      original.floats(at + 0x30)[0], original.floats(at + 0x124)[0],
                      original.floats(at + 0x138)[0], original.floats(at + 0x28)[0])
            records.append([bits(value) for value in floats] + [original.integer(at + 0x1c)])
        expected.append(records)
        original_calls.append(original.math)
    actual = execute(exe, '--smoke-ring-oracle', [payload.hex() for payload in packets])
    if len(actual) != len(expected):
        raise AssertionError('Missing shared runtime ring rows')
    differences = []
    for n, (before, after) in enumerate(zip(expected, actual)):
        if before != after:
            differences.append(dict(packet=n, original=before, actual=after))
    unique = {row['path']: row for row in inputs}
    unchanged = all(O.identity(Path(row['path'])) == row for row in unique.values())
    report = dict(success=unchanged and not differences, inputsUnchanged=unchanged,
                  inputs=list(unique.values()), completeIda=ida, packets=len(packets),
                  emitterRecords=36 * len(packets), float32Fields=36 * len(packets) * 10,
                  nativeMath=dict(identity=identity_rows[0], module=O.identity(runtime),
                                  samples=len(angles), results=actual_math),
                  originalMathCalls=original_calls, differences=differences,
                  sourceCopies=0, deployed=False, liveGameplay=False, performanceAccepted=False,
                  boundary='Original case88/readers/Add instructions with actual host CRT XMM0 outputs; actual SDK ReceiveSmoke ring records. No native original DLL loading, original CRT version identity, real network, trace, renderer or gameplay claim.')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=2) + '\n', 'utf8')
    print(json.dumps({key: report[key] for key in ('success', 'inputsUnchanged', 'packets',
                                                   'emitterRecords', 'float32Fields')}))
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
