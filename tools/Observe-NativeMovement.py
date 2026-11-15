#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, read-only native PM sampling; never sends commands to a game."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import time

ROOT = Path(__file__).resolve().parent.parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--csol-pdb', type=Path)
    parser.add_argument('--seconds', type=int, default=20, choices=range(1, 61))
    parser.add_argument('--hz', type=int, default=200, choices=range(1, 501))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output, probe = args.output.resolve(), args.probe.resolve(strict=True)
    if output.exists() or not output.is_relative_to(ROOT / 'build') or not probe.is_relative_to(ROOT / 'build'):
        raise ValueError('Use verified workspace probe and fresh build output')
    receipt = json.loads((probe / 'receipt.json').read_text('utf-8'))
    if not receipt['success'] or not receipt['inputsUnchanged']:
        raise ValueError('Native PM probe must pass before sampling')
    layout = json.loads((probe / 'layout.json').read_text('utf-8'))
    observer = load('admission', ROOT / 'tools/Observe-CSOLAdmission.py')
    audit = load('audit', ROOT / 'tools/Inspect-MetaHookRuntime.py')
    process = observer.ReadOnlyClient(args.pid, args.installation.resolve(strict=True))
    try:
        image, base = next((path, base) for path, base in process.modules if path.name.lower() == 'client.dll')
        pe = audit.PE(image)
        if pe.sha256 != 'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2':
            raise ValueError('Unmatched native client')
        # IDA HUD_PlayerMove100602C0 stores the public PM argument here.
        # Verify its complete store instruction in this exact native image.
        code = process.read(base + 0x602e4, 8)
        if code[:4] != bytes.fromhex('8b4508a3') or int.from_bytes(code[4:], 'little') != base + 0x12841c:
            raise ValueError('Native PM argument store differs from verified IDA')
        hud = None
        if args.csol_pdb:
            pdb = args.csol_pdb.resolve(strict=True)
            if not pdb.is_relative_to(ROOT):
                raise ValueError('Use workspace CSOL symbols')
            image, csol_base = next((path, base) for path, base in process.modules if path.name.lower() == 'csolsupport.dll')
            csol_pe = audit.PE(image)
            symbols = audit.Symbols(pdb, csol_pe)
            if process.read(csol_base + csol_pe.codeview['rva'], 24).hex() != csol_pe.codeview['identity']:
                raise ValueError('Loaded CSOL does not match its PDB')
            units = re.findall(r'Mod (\d+) \| `([^`]+)`', symbols.dump('-modules'))
            matched = [int(i) for i, name in units if name.lower().endswith(('stamina_hud.obj', 'stamina_hud.cpp.obj'))]
            if len(matched) != 1:
                raise ValueError('Missing stamina compilation unit')
            text = symbols.dump('-symbols', '-modi=' + str(matched[0]))
            state = re.search(r'S_LDATA32[^\n]*::state`\s*\n[^\n]*addr = (\d+):(\d+)', text)
            if not state:
                raise ValueError('Missing actual received stamina state')
            state_address = csol_base + csol_pe.sections[int(state[1]) - 1]['rva'] + int(state[2])
            presentation = symbols.layout('csol::scenario::custom::StaminaPresentation')
            stamina = symbols.layout('csol::scenario::custom::StaminaSnapshot')
            refdef = symbols.layout('ref_params_s')
            view_address = csol_base + symbols.global_rva('csol::client::view')
            hud = (state_address, presentation, stamina, view_address, refdef)
        output.mkdir()
        start, cpu = time.perf_counter(), time.process_time()
        count = unstable = 0
        previous, changes, jump_samples, departures = None, 0, 0, 0
        previous_z = None
        with (output / 'samples.jsonl').open('w', encoding='utf-8') as stream:
            while time.perf_counter() - start < args.seconds:
                pointer = process.u32(base + 0x12841c)
                if pointer:
                    head = process.read(pointer, 548)
                    cmd = process.read(pointer + layout['cmd'], layout['cmdSize'])
                    physics = process.read(pointer + layout['physinfo'], 256).split(b'\0', 1)[0]
                    stable = (process.u32(base + 0x12841c) == pointer and
                              process.read(pointer + layout['time'], 4) == head[layout['time']:layout['time'] + 4] and
                              process.read(pointer + layout['cmd'], layout['cmdSize']) == cmd)
                    sample = {'wall': time.perf_counter() - start, 'stable': stable,
                              'time': struct.unpack_from('<f', head, layout['time'])[0],
                              'origin': struct.unpack_from('<fff', head, layout['origin']),
                              'velocity': struct.unpack_from('<fff', head, layout['velocity']),
                              'oldbuttons': struct.unpack_from('<i', head, layout['oldbuttons'])[0],
                              'onground': struct.unpack_from('<i', head, layout['onground'])[0],
                              'fuser2': struct.unpack_from('<f', head, layout['fuser2'])[0],
                              'dead': struct.unpack_from('<i', head, layout['dead'])[0],
                              'movetype': struct.unpack_from('<i', head, layout['movetype'])[0],
                              'buttons': struct.unpack_from('<H', cmd, layout['cmdButtons'])[0],
                              'msec': cmd[layout['cmdMsec']],
                              'jumpDenied': b'\\cso_jdeny\\1' in physics}
                    if hud:
                        address, presentation, stamina, view_address, refdef = hud
                        raw = process.read(address, presentation['size'])
                        offset = presentation['members']['value_']
                        sample['stamina'] = {name: struct.unpack_from('<f', raw, offset + stamina['members'][name])[0]
                                              for name in ('amount', 'maximum')}
                        sample['stamina']['known'] = bool(raw[presentation['members']['known_']])
                        sample['stamina']['infinite'] = bool(raw[offset + stamina['members']['infinite']])
                        view = process.read(view_address, refdef['size'])
                        sample['view'] = {name: struct.unpack_from('<fff', view, refdef['members'][name])
                                          for name in ('vieworg', 'simorg', 'simvel')}
                        sample['view']['onground'] = struct.unpack_from('<i', view, refdef['members']['onground'])[0]
                        sample['view']['time'] = struct.unpack_from('<f', view, refdef['members']['time'])[0]
                    stream.write(json.dumps(sample) + '\n')
                    count += 1
                    unstable += not stable
                    key = (sample['time'], sample['origin'], sample['buttons'], sample['oldbuttons'])
                    changes += previous is not None and previous != key
                    previous = key
                    jump_samples += bool(sample['buttons'] & 2)
                    z = sample['velocity'][2]
                    departures += previous_z is not None and previous_z <= 0 and z > 100
                    previous_z = z
                remaining = start + (count + 1) / args.hz - time.perf_counter()
                if remaining > 0:
                    time.sleep(min(remaining, 1 / args.hz))
        result = dict(pid=args.pid, nativeSha256=pe.sha256, layout=layout, samples=count,
                      unstableSamples=unstable, observedChanges=changes, jumpSamples=jump_samples,
                      departures=departures, wallSeconds=time.perf_counter() - start,
                      samplerCpuSeconds=time.process_time() - cpu, readOnly=True, noInput=True,
                      boundary='External PM snapshots can miss intra-frame replay; not keypress or render latency measurements')
        (output / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2))
    finally:
        process.close()


if __name__ == '__main__':
    main()
