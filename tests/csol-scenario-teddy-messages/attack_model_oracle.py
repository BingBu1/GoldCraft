#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe original Attack2/Blackhole2 TE145 packets with explicit ABI edges.

Original PE bytes execute under Unicorn, never as native game code. BaseEvent is
an explicit callee-clean fixture and the real origin getter is executed. This
proves event selection, fresh actor reads and packet fields, not the full AI or
the runtime value of the normal-zero template.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import sys

from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('mp', 'evidence', 'dust-evidence', 'events-evidence', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Use a fresh output directory')
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location('ground_original', Path(__file__).with_name('ground_oracle.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    class Original(module.GroundOriginal):
        def hook(self, u, address, size, _):
            if address == 0x1093cf40:
                sp = u.reg_read(UC_X86_REG_ESP)
                ret, event = struct.unpack('<2I', u.mem_read(sp, 8))
                self.base_calls.append(event)
                # Exercise a fresh read after the base callback.
                u.mem_write(0x604008, struct.pack('<3f', *self.after_origin))
                u.mem_write(0x604050, struct.pack('<3f', *self.after_angles))
                u.reg_write(UC_X86_REG_ESP, sp + 8)
                u.reg_write(UC_X86_REG_EIP, ret)
                return
            return super().hook(u, address, size, _)

        def event(self, kind, event, origin, angles, model):
            self.open, self.packets, self.stub_calls, self.base_calls = None, [], [], []
            self.after_origin = [v + d for v, d in zip(origin, (1.25, -2.5, 3.75))]
            self.after_angles = [v + d for v, d in zip(angles, (5., 10., -15.))]
            self.word(0x600100, 0x601000)
            self.word(0x60101c, 0x602000)
            self.word(0x602000, 0x603000)
            self.word(0x602008, 0x604000)
            self.word(0x603128, 0x10612440)
            self.u.mem_write(0x604008, struct.pack('<3f', *origin))
            self.u.mem_write(0x604050, struct.pack('<3f', *angles))
            self.u.mem_write(0x11ef7b48, struct.pack('<H', model))
            self.u.mem_write(0x12278584, bytes(12))  # Explicit fixture input.
            self.u.mem_write(0x608000, struct.pack('<2I', 0x500000, event & 0xffffffff))
            self.u.reg_write(UC_X86_REG_ESP, 0x608000)
            self.u.reg_write(UC_X86_REG_ECX, 0x600104)
            self.u.emu_start(0x10997910 if kind == 0 else 0x10997980, 0x500000, count=4000)
            assert self.open is None and self.u.reg_read(UC_X86_REG_ESP) == 0x608008
            assert not self.stub_calls
            return dict(kind=kind, event=event, origin=origin, angles=angles, model=model,
                        afterOrigin=self.after_origin, afterAngles=self.after_angles,
                        baseCalls=list(self.base_calls), packets=self.packets)

    inputs = [module.identity(Path(__file__)), module.identity(Path(module.__file__)),
              module.identity(args.events_evidence)]
    original = Original(args.mp, args.evidence, args.dust_evidence)
    # Validate all new executed windows against the saved IDA capture.
    capture = json.loads(args.events_evidence.read_text(encoding='utf8'))
    records = module.unwrap(capture['bytes'])
    if isinstance(records, dict):
        records = records['result']
    for row in records:
        address = int(row['addr'], 16)
        data = bytes(int(b, 16) for b in row['data'].split())
        if bytes(original.u.mem_read(address, len(data))) != data:
            raise ValueError('Saved IDA bytes do not match MP')
        original.verified.append(dict(va=address, bytes=len(data)))
    cases = []
    lines = []
    for origin, angles in (((10.1875, -20.1875, 30.1875), (12.375, -91.7, 7.125)),
                           ((4096.125, -8192.25, 0.), (179., 361., -181.)),
                           ((-.0625, .0625, -4096.125), (0., 0., 0.))):
        for model in (1, 321, 32767):
            for kind in (0, 1):
                for event in ((0, 1, 1003) if kind == 0 else (1, 2)):
                    case = original.event(kind, event, origin, angles, model)
                    expected_packet = (kind == 0 and event == 0) or (kind == 1 and event == 1)
                    assert len(case['packets']) == int(expected_packet)
                    assert case['baseCalls'] == ([event] if kind == 0 else [])
                    if expected_packet:
                        packet = case['packets'][0]
                        assert (packet['route'], packet['message'], packet['recipient'], packet['pvs']) == (0, 23, 0, None)
                        used_origin = case['afterOrigin'] if kind == 0 else origin
                        used_angles = case['afterAngles'] if kind == 0 else angles
                        lines.append(' '.join(map(str, (kind, *used_origin, *used_angles, model, packet['payload']))))
                    cases.append(case)
    args.output.mkdir(parents=True)
    fixture = args.output / 'attack_model_cases.txt'
    fixture.write_text(str(len(lines)) + '\n' + '\n'.join(lines) + '\n', encoding='ascii')
    assert all(module.identity(Path(row['path'])) == row for row in inputs + original.files + [original.sample])
    result = dict(success=True, inputsUnchanged=True, inputs=inputs, sample=original.sample,
                  executedOriginalInstructions=[hex(a) for a in sorted(original.executed)],
                  cases=cases, fixture=module.identity(fixture),
                  verifiedRanges=original.verified, eventsEvidence=module.identity(args.events_evidence),
                  boundaries=dict(baseEvent='explicit ABI callback with actor mutation',
                                  velocity='explicit zero fixture; runtime contract audited separately',
                                  rendered=False, deployed=False))
    (args.output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(dict(success=True, events=len(cases), packets=len(lines))))


if __name__ == '__main__':
    main()
