# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and test the independent Chaos route core with the pinned toolchain.

The expected original routes are generated from the locally decoded, pinned
RRR using Python's independent CSV reader. Numeric expectations execute bounded
original MP instructions. No game DLL, shared CMake or runtime is modified.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import io
import json
import os
import re
import subprocess
import time

from oracle import generate, identity

ROOT = Path(__file__).resolve().parents[2]
TEST = 'tests/csol-scenario-chaos-routes/'
PINS = {
    'zbs_rrr_zs_chaos.cso': 'd169aa0f89c9668d528d386126e12d54183951e8bde189329d8df10ec8283abc',
    'zbs_trg_zs_chaos.cso': '7873373eef6ee7ddc439b44e04a2811ce8988103099e2520121d55c91ab94c7a',
}


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf8')


def fixtures(rrr, oracle):
    rows = list(csv.reader(io.StringIO(rrr.decode('latin1'), newline='')))
    count = int(rows[0][1])
    blocks, block = [], []
    for row in rows[2:]:
        if row and row[0].startswith('*'):
            blocks.append(block)
            block = []
            if len(blocks) == count:
                break
        else:
            block.append(row)
    assert len(blocks) == 3
    lines = ['std::vector<ChaosRoute> Expected() { std::vector<ChaosRoute> out(3);']
    sizes = []
    for i, block in enumerate(blocks):
        starts = [int(r[1]) for r in block if r[0] == 'START']
        ends = [int(r[1]) for r in block if r[0] == 'END']
        assert len(starts) == len(ends) == 1
        exits, waves, endwaves = {}, {}, {}
        for row in block:
            if row[0].startswith('g'):
                exits.setdefault(row[0][1:], int(row[1]))
            elif row[0].startswith('w'):
                waves.setdefault(row[0][1:], [int(v) & 65535 for v in re.findall(r'[0-9]+', row[1])])
                endwaves.setdefault(row[0][1:], [int(v) & 65535 for v in re.findall(r'[0-9]+', row[2])])
        assert len(exits) == 12
        assert starts == [[6], [1], [4]][i] and ends == [7]
        lines += [f'out[{i}].startRoom={starts[0]}; out[{i}].endRoom={ends[0]};']
        for key, value in exits.items():
            lines += [f'out[{i}].exits.emplace({json.dumps(key)}, {value});']
        for member, values in [('waves', waves), ('endWaves', endwaves)]:
            for key, value in values.items():
                lines += [f'out[{i}].{member}.emplace({json.dumps(key)}, std::vector<std::uint16_t>{{'+','.join(map(str,value))+'});']
        sizes.append(dict(start=starts[0], end=ends[0], exits=len(exits), waves=len(waves), endWaves=len(endwaves)))
    lines += ['return out; }',
        'struct SelectionCase { std::size_t count; float overrideValue; std::uint32_t random; std::size_t expected; };',
        'constexpr SelectionCase Selections[] = {']
    for row in oracle['selections']:
        lines.append('{'+f"{row['count']},{row['override']!r}f,{row['random']}u,{row['actual']}"+'},')
    lines += ['};', 'struct RoomCase { std::size_t count; std::int32_t destination; bool narrowing; std::int16_t expected; };',
              'constexpr RoomCase Rooms[] = {']
    for row in oracle['rooms']:
        lines.append('{'+f"{row['count']},{row['destination']},{str(row['narrowing']).lower()},{row['actual']}"+'},')
    lines += ['};']
    return '\n'.join(lines)+'\n', sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'evidence', 'mp', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    for parent in (out, *out.parents):
        assert not parent.is_symlink() and not parent.is_junction()
    out.mkdir()
    (out/'temp').mkdir()
    (out/'objects').mkdir()
    consumed, frozen = {}, {}

    def record(path):
        row = identity(path)
        consumed[row['path']] = row
        return row

    def freeze(relative):
        if relative in frozen:
            return
        original = record(ROOT/relative)
        destination = out/'source'/relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT/relative).read_bytes())
        assert identity(original['path']) == original and destination.stat().st_nlink == 1
        frozen[relative] = dict(original=original, frozen=record(destination))
        if destination.suffix in ('.cpp','.hpp'):
            for token in re.findall(r'^\s*#\s*include\s+"([^"]+)"',destination.read_text('utf8'),re.M):
                child = ((ROOT/relative).parent/token).resolve()
                if child.is_file():
                    freeze(child.relative_to(ROOT).as_posix())

    units = ['native/csol/scenario/'+n for n in ('chaos_routes.cpp','config.cpp','spawn_points.cpp','trigger_rules.cpp')]
    units.append(TEST+'main.cpp')
    for rel in [*units, TEST+'run.py', TEST+'oracle.py']:
        freeze(rel)
    inputs = read(args.evidence/'resources.json')
    record(args.evidence/'resources.json')
    data = {}
    for row in inputs['tables']:
        name = Path(row['input']['path']).name
        if name not in PINS:
            continue
        assert record(row['input']['path']) == row['input'] and row['input']['sha256'] == PINS[name]
        assert record(row['decoded']['path']) == row['decoded']
        path = out/(name+'.csv')
        path.write_bytes(Path(row['decoded']['path']).read_bytes())
        data[name] = record(path)
    assert len(data) == 2
    oracle = generate(args.mp, args.evidence, out/'oracle.json')
    record(args.mp)
    record(out/'oracle.json')
    expected, sizes = fixtures(Path(data['zbs_rrr_zs_chaos.cso']['path']).read_bytes(), oracle)
    (out/'expected.inc').write_text(expected,'utf8')
    record(out/'expected.inc')
    commands = read(args.baseline/'build-commands.json')
    record(args.baseline/'build-commands.json')
    record(args.baseline/'environment.json')
    compiler = next(j['command'][0] for j in commands if j['kind']=='compile')
    linker = next(j['command'][0] for j in commands if j['kind']=='link')
    record(compiler); record(linker)
    env = os.environ | read(args.baseline/'environment.json')
    env['TEMP'] = env['TMP'] = str(out/'temp')
    objects, steps = [], []
    receipt = dict(success=False, source=frozen, inputRoutes=sizes, selections=len(oracle['selections']),
                   rooms=len(oracle['rooms']), steps=steps, fullDLL=False, deployed=False, liveGameplay=False,
                   maxWorkers=2, scope='Standalone real core plus original instruction comparison; no engine Replay validation')
    try:
        jobs = []
        for rel in units:
            obj = out/'objects'/(Path(rel).stem+'.obj')
            objects.append(str(obj))
            argv = [compiler,'/c','/nologo','/W4','/WX','/EHsc','/MT','/utf-8','/std:c++20',
                    '--target=i686-pc-windows-msvc','/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin',
                    '/showIncludes','/I'+str(out),'/Fo'+str(obj),str(out/'source'/rel)]
            jobs.append((Path(rel).stem,'compile',argv))
        jobs.append(('link','link',[linker,'/subsystem:console','/machine:x86','/out:'+str(out/'test.exe'),
                    '/threads:2','/opt:lldltojobs=2',*objects]))
        jobs.append(('test','test',[str(out/'test.exe'),data['zbs_rrr_zs_chaos.cso']['path'],data['zbs_trg_zs_chaos.cso']['path']]))
        for name,kind,argv in jobs:
            rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(argv[1:]),'utf8')
            started=time.monotonic()
            done=subprocess.run(argv if kind=='test' else [argv[0],'@'+str(rsp)],cwd=ROOT,env=env,
                                capture_output=True,timeout=180)
            log=out/(name+'.log');log.write_bytes(done.stdout+done.stderr)
            text=log.read_text('utf-8-sig',errors='replace')
            includes=sorted(set(re.findall(r'Note: including file:\s*([^\r\n]+)',text)))
            for path in includes:
                record(path.strip())
            steps.append(dict(name=name,kind=kind,command=argv,exit=done.returncode,log=record(log),
                              seconds=time.monotonic()-started,includes=includes))
            save(out/'result.json',receipt)
            print(name,done.returncode,flush=True)
            if kind=='test' or done.returncode:
                print(text[-5000:],flush=True)
            assert done.returncode==0,name+' failed'
        receipt['executable']=record(out/'test.exe')
        receipt['success']=True
    finally:
        receipt['observedInputs']=list(consumed.values())
        receipt['changedAfter']=[p for p,r in consumed.items() if identity(p)!=r]
        receipt['success'] &= not receipt['changedAfter']
        save(out/'result.json',receipt)


if __name__ == '__main__':
    main()
