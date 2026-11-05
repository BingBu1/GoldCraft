# SPDX-License-Identifier: GPL-3.0-or-later
"""Build independent x86 round-parser tests; never deploy or build a game DLL."""
from pathlib import Path
import argparse
import csv
import io
import json
import os
import re
import subprocess
import sys
import time

from oracle import Original, HW_SHA, identity, module

ROOT = Path(__file__).resolve().parents[2]
TEST = 'tests/csol-scenario-chaos-rounds/'
MAPS = ('chaos', 'lostcity', 'trap', '2gate', 'lastclue', 'nightmare')


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'evidence', 'mp', 'hw', 'hw-metadata', 'hw-decoder', 'common', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build') and not out.exists()
    for parent in (out, *out.parents):
        assert not parent.is_symlink() and not parent.is_junction()
    out.mkdir()
    for name in ('temp', 'objects', 'fixtures'):
        (out / name).mkdir()
    consumed, frozen = {}, {}

    def record(path):
        item = identity(path)
        consumed[item['path']] = item
        return item

    def freeze(relative):
        if relative in frozen:
            return
        source = ROOT / relative
        before = record(source)
        dest = out / 'source' / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(source.read_bytes())
        assert identity(source) == before and dest.stat().st_nlink == 1
        frozen[relative] = dict(original=before, frozen=record(dest))
        if dest.suffix in ('.cpp', '.hpp'):
            for token in re.findall(r'^\s*#\s*include\s+"([^"]+)"', dest.read_text('utf8'), re.M):
                child = (source.parent / token).resolve()
                if child.is_file():
                    freeze(child.relative_to(ROOT).as_posix())

    core = ['native/csol/scenario/' + name for name in
            ('config.cpp', 'spawn_points.cpp', 'round_settings.cpp', 'monster_attributes.cpp')]
    units = core + [TEST + 'main.cpp', 'tests/csol-scenario/config_tests.cpp']
    for relative in [*units, TEST + 'run.py', TEST + 'oracle.py', 'tests/csol-scenario/csv_getters_original.py']:
        freeze(relative)
    assert record(args.hw)['sha256'] == HW_SHA
    record(args.mp); record(args.hw_metadata)
    original = Original(args.mp, args.evidence)
    for item in original.verified:
        record(item['ida']['path'])
    getter_module = module(ROOT / 'tests/csol-scenario/csv_getters_original.py', 'chaos_round_getters')
    getter = getter_module.Original(args.hw, args.hw_metadata)
    decoder_module = module(args.hw_decoder, 'chaos_round_hw_decoder')
    decoder = decoder_module.ConfigOracle()
    assert decoder_module.meta['sha256'] == HW_SHA
    # Record every local helper imported by the independent original HW oracle.
    for loaded in list(sys.modules.values()) + [getter_module, decoder_module]:
        path = getattr(loaded, '__file__', None)
        if path and Path(path).resolve().is_relative_to(ROOT) and Path(path).is_file():
            record(path)
    record(args.hw_decoder)
    hw_refs = []
    for name, address, length in (
            ('round-string-wrapper-ida-bytes', 0x3732d310, 13),
            ('round-string-vtables-ida-bytes', 0x3811a468, 72),
            ('round-string-cell-ida-bytes', 0x37c15b70, 69)):
        path = args.evidence / (name + '.json')
        doc = read(path)
        doc = doc.get('structuredContent') or json.loads(doc['content'][0]['text'])
        data = bytes(int(v, 16) for v in doc['result'][0]['data'].replace(',', ' ').split())
        assert data == getter.original(address, length)
        hw_refs.append(record(path))
    modes = [dict(mode=n, first=original.start(n)) for n in [-2147483648,-1,*range(64),2147483647]]
    scans = []
    for text in ('', 'abc', '1', '1 2 1', '-3/+4', '5.5', '1,2;3:4', '65535 65536 65537',
                 '32768/999999999', '\t5\r\n6', '001 000 000000001', '0000000000', '1234567890',
                 'a9b8c7', '--4 ++5', '1e3', '2/1234567890'):
        actual = original.scan(text)
        scans.append(dict(text=text, valid=actual is not None, groups=actual or []))
    lines = ['const ModeCase Modes[] = {']
    for item in modes:
        lines.append('{%d,%d},' % (item['mode'], item['first']))
    lines += ['};', 'const ScannerCase Scanners[] = {']
    for item in scans:
        lines.append('{%s,%s,{%s}},' % (json.dumps(item['text']), str(item['valid']).lower(), ','.join(map(str,item['groups']))))
    lines += ['};', 'const ExpectedMap Maps[] = {']
    datasets = []
    for name in MAPS:
        stem = 'zbs_zs_' + name
        source = args.common / 'maps' / (stem + '.cso')
        encoded = source.read_bytes()
        source_id = record(source)
        if name == 'chaos':
            assert source_id['sha256'] == '98bcbc31df866051db1c1e57d826f15132aaf4f89cee67304cf506de3230a78d'
        plain = decoder.decode(encoded)
        (out / 'fixtures' / (stem + '.cso')).write_bytes(encoded)
        (out / 'fixtures' / (stem + '.csv')).write_bytes(plain)
        record(out / 'fixtures' / (stem + '.cso')); record(out / 'fixtures' / (stem + '.csv'))
        rows = list(csv.reader(io.StringIO(plain.decode('latin1'), newline='')))
        assert len(rows) >= 7
        lines.append('{"%s",{' % stem)
        expected_rows = []
        for row in rows[7:]:
            groups = original.scan(row[1] if len(row) > 1 else '')
            assert groups is not None
            values = []
            for column in range(3, 12):
                result = getter.get('int8' if column == 10 else 'int16', row[column] if len(row) > column else '')
                values.append(result['value'] & 255 if column == 10 else result['value'])
            lines.append('{{%s},{%s}},' % (','.join(map(str,groups)), ','.join(map(str,values))))
            expected_rows.append(dict(groups=groups, scalarColumns3to11=values))
        lines.append('}},')
        datasets.append(dict(source=source_id, plain=record(out / 'fixtures' / (stem + '.csv')),
                             originalHeader=rows[6], rows=expected_rows))
    lines += ['};']
    expected = out / 'expected.inc'
    expected.write_text('\n'.join(lines) + '\n', 'utf8')
    record(expected)
    oracle = dict(mp=original.input, hw=record(args.hw), mpIda=original.verified, hwIda=hw_refs,
                  modes=modes, scanners=scans, datasets=datasets, originalDecoder='37C18460',
                  substitutions=['service getter fixed explicit value', 'host UCRT atoi for <=9 digits',
                                 'security-cookie endpoint', 'HW decoder allocation/memcpy/CRT endpoints'],
                  nativeDLLLoaded=False, liveGameplay=False)
    save(out / 'oracle.json', oracle); record(out / 'oracle.json')
    commands = read(args.baseline / 'build-commands.json')
    record(args.baseline / 'build-commands.json'); record(args.baseline / 'environment.json')
    compiler = next(x['command'][0] for x in commands if x['kind'] == 'compile')
    linker = next(x['command'][0] for x in commands if x['kind'] == 'link')
    record(compiler); record(linker)
    env = os.environ | read(args.baseline / 'environment.json')
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    steps, jobs, objects = [], [], {}
    receipt = dict(success=False, source=frozen, steps=steps, mapCount=len(datasets),
                   originalModeCases=len(modes), originalScannerCases=len(scans),
                   fullDLL=False, deployed=False, liveGameplay=False, maxWorkers=2)
    try:
        for relative in units:
            name = Path(relative).stem
            obj = out / 'objects' / (name + '.obj')
            objects[relative] = str(obj)
            argv = [compiler,'/c','/nologo','/W4','/WX','/EHsc','/MT','/utf-8','/std:c++20',
                    '--target=i686-pc-windows-msvc','/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin',
                    '/showIncludes','/I'+str(out),'/I'+str(out / 'source/native/csol/scenario'),
                    '/Fo'+str(obj),str(out / 'source' / relative)]
            jobs.append((name,'compile',argv))
        for name, main_source in [('test',TEST+'main.cpp'),('legacy','tests/csol-scenario/config_tests.cpp')]:
            argv = [linker,'/subsystem:console','/machine:x86','/out:'+str(out / (name+'.exe')),
                    '/threads:2','/opt:lldltojobs=2',*[objects[p] for p in core],objects[main_source]]
            jobs.append((name+'-link','link',argv))
            jobs.append((name,'test',[str(out / (name+'.exe'))] + ([str(out/'fixtures')] if name=='test' else [])))
        for name, kind, argv in jobs:
            rsp = out / (name + '.rsp'); rsp.write_text(subprocess.list2cmdline(argv[1:]), 'utf8')
            start = time.monotonic()
            done = subprocess.run(argv if kind == 'test' else [argv[0],'@'+str(rsp)], cwd=ROOT,
                                  env=env,capture_output=True,timeout=180)
            log = out / (name + '.log'); log.write_bytes(done.stdout + done.stderr)
            text = log.read_text('utf-8-sig', errors='replace')
            includes = sorted(set(re.findall(r'Note: including file:\s*([^\r\n]+)',text)))
            for path in includes:
                record(path.strip())
            steps.append(dict(name=name,kind=kind,command=argv,exit=done.returncode,log=record(log),
                              seconds=time.monotonic()-start,includes=includes))
            save(out/'result.json',receipt)
            print(name,done.returncode,flush=True)
            if kind=='test' or done.returncode:
                print(text[-5000:],flush=True)
            assert done.returncode==0,name+' failed'
        receipt['executable']=record(out/'test.exe')
        receipt['legacyExecutable']=record(out/'legacy.exe')
        receipt['success']=True
    finally:
        receipt['observedInputs']=list(consumed.values())
        receipt['changedAfter']=[p for p,r in consumed.items() if identity(p)!=r]
        receipt['success'] &= not receipt['changedAfter']
        save(out/'result.json',receipt)


if __name__ == '__main__':
    main()
