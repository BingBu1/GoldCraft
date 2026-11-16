#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read matched loading-progress scalars during a real isolated map transition."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import time

ROOT = Path(__file__).resolve().parents[2]


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--pdb', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--seconds', type=int, choices=range(10, 181), default=90)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace output')
    observer = module('observer', ROOT / 'tools/Observe-CSOLAdmission.py')
    reader = module('reader', ROOT / 'tools/Inspect-MetaHookRuntime.py')
    process = observer.ReadOnlyClient(args.pid, ROOT / 'sandbox/cs-client-b/Half-Life')
    try:
        image, base = next((path, base) for path, base in process.modules if path.name.lower() == 'goldcraft.dll')
        pe = reader.PE(image)
        symbols = reader.Symbols(args.pdb, pe)
        if process.read(base + pe.codeview['rva'], 24).hex() != pe.codeview['identity']:
            raise ValueError('Loaded GoldCraft identity differs from its matching PDB')
        units = re.findall(r'Mod (\d+) \| `([^`]+)`', symbols.dump('-modules'))
        matches = [index for index, name in units if name.lower().endswith(('resource_bridge.obj', 'resource_bridge.cpp.obj'))]
        if len(matches) != 1:
            raise ValueError('Expected one actual progress producer')
        text = symbols.dump('-symbols', '-modi=' + str(int(matches[0])))
        locations = {}
        for name in ('stage', 'loading_phase', 'loading_total', 'loading_completed', 'client_state'):
            match = re.search(r'S_LDATA32[^\n]*::' + name + r'`\s*\n[^\n]*addr = (\d+):(\d+)', text)
            if not match:
                raise ValueError('Progress scalar missing: ' + name)
            locations[name] = base + pe.sections[int(match[1]) - 1]['rva'] + int(match[2])
        out.mkdir()
        (out / 'ready.json').write_text(json.dumps(dict(pid=args.pid, locations=locations)), 'utf-8')
        start = time.perf_counter()
        changes, previous, count = [], None, 0
        with (out / 'samples.jsonl').open('w', encoding='utf-8') as stream:
            while time.perf_counter() - start < args.seconds:
                sample = {name: process.u32(address) for name, address in locations.items()}
                sample['client_state'] = process.u32(sample['client_state'])
                key = tuple(sample.values())
                sample['wall'] = time.perf_counter() - start
                stream.write(json.dumps(sample) + '\n')
                if key != previous:
                    changes.append(sample)
                    previous = key
                count += 1
                time.sleep(.01)
        result = dict(readOnly=True, noInput=True, samples=count, changes=changes,
                      imageSha256=pe.sha256, pdb=symbols.identity,
                      boundary='Observed real progress producer and engine states; not a screenshot or rendering acceptance')
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')
        print(json.dumps(dict(samples=count, changes=len(changes))))
    finally:
        process.close()


if __name__ == '__main__':
    main()
