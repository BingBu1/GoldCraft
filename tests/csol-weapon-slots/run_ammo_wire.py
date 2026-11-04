#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify wide reserves with the pinned ReHLDS bit writer and old-schema failure."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[2]

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def identity(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a new workspace build directory')
    out.mkdir(parents=True)
    paths = [ROOT / 'external/ReHLDS/rehlds/engine/common.cpp',
             ROOT / 'external/ReGameDLL_CS/dist/delta.lst',
             ROOT / 'tools/Prepare-CSOLWeaponDelta.py',
             Path(__file__).with_name('ammo_wire.cpp'), ROOT / 'tests/common/cpp_function.py']
    before = [identity(p) for p in paths]
    common = paths[0].read_bytes()
    extractor = load('cpp_function', paths[4])
    # REHLDS_FIXES is enabled in the deployed server. Select that first writer,
    # leaving the legacy #else implementation out of this translation unit.
    signature = b'void MSG_WriteBits(uint32 data, int numbits)'
    first = common.index(signature)
    second = common.index(signature, first + len(signature))
    functions = [extractor.function(common[first:second], signature)[2]]
    onebit = b'void MSG_WriteOneBit(int nValue)'
    begin = common.index(onebit)
    end = common.index(onebit, begin + len(onebit))
    functions += [extractor.function(common[begin:end], onebit)[2],
                  extractor.function(common, b'void MSG_WriteSBits(int data, int numbits)')[2]]
    (out / 'engine_ammo_writer.inc').write_bytes(b'\n'.join(functions))
    (out / 'ammo_wire.cpp').write_bytes(paths[3].read_bytes())
    delta = load('weapon_delta', paths[2])
    old = paths[1].read_bytes()
    new = delta.expand(old)
    (out / 'old-delta.lst').write_bytes(old)
    (out / 'new-delta.lst').write_bytes(new)
    def fields(data):
        body = re.search(rb'clientdata_t[^\r\n]*\s*\{([^}]*)\}', data)[1]
        return {m[1].decode(): (int(m[3]), int(b'DT_SIGNED' in m[2])) for m in
                re.finditer(rb'DEFINE_DELTA\(\s*([^,\s]+)\s*,\s*([^,]+),\s*(\d+)', body)}
    ammo_names = ['ammo_nails', 'ammo_shells', 'ammo_cells', 'ammo_rockets',
                  *[f'vuser{i}[{j}]' for i in (2, 3) for j in range(3)], 'vuser4[1]']
    schemas = {label: fields(data) for label, data in (('old', old), ('new', new))}
    assert all(schemas['new'][name] == ((16, 1) if name.startswith('ammo_') else (15, 0)) for name in ammo_names)
    env = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    exe = out / 'ammo_wire.exe'
    command = [str(args.compiler), '/nologo', '/std:c++20', '/O2', '/fp:precise',
               '/arch:AVX2', '--target=i686-pc-windows-msvc', '/MD',
               str(out / 'ammo_wire.cpp'), '/Fo' + str(out / 'ammo_wire.obj'),
               '/Fe' + str(exe), '-fuse-ld=lld']
    run = subprocess.run(command, env=env, cwd=out, capture_output=True, timeout=120)
    (out / 'compile.log').write_bytes(run.stdout + run.stderr)
    if run.returncode: raise RuntimeError('Ammo writer fixture failed to build')
    results = []
    for label, values in schemas.items():
        for bits, sign in sorted({values[name] for name in ammo_names}):
            run = subprocess.run([str(exe), str(bits), str(sign)], capture_output=True, timeout=30)
            expected = int(label == 'old')
            text = run.stdout.decode('utf-8')
            assert run.returncode == expected, text
            if expected: assert 'first mismatch sent=512 received=511' in text
            results.append(dict(schema=label, bits=bits, signed=bool(sign),
                                exitCode=run.returncode, output=text))
    assert before == [identity(p) for p in paths]
    (out / 'result.json').write_text(json.dumps(dict(success=True, inputs=before,
        compileCommand=command, schemas=schemas, results=results,
        boundary='Unmodified ReHLDS FIXES writer, exhaustively checks 0..32767 with adjacent sentinels; not a live client test.'), indent=2), encoding='utf-8')
    print(json.dumps(results, indent=2))

if __name__ == '__main__': main()
