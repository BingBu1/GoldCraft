# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the actual map resource loader and test its four/five-table transactions.

RRR is supplied locally; no game resources are copied into the repository.
This is a console test with controlled filesystem callbacks, not a map playtest.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SCENARIO = 'native/csol/scenario/'
TEST = 'tests/csol-scenario-map-profile/'


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'rrr', 'decoded-rrr', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    args = parser.parse_args()
    source_root = args.source_root.resolve(strict=True)
    assert source_root.is_relative_to(ROOT)
    out = args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    for parent in (out, *out.parents):
        assert not parent.is_symlink() and not parent.is_junction()
    out.mkdir(parents=True)
    (out/'temp').mkdir()
    (out/'objects').mkdir()
    inputs, frozen = {}, {}

    def record(path):
        row = identity(path)
        inputs[row['path']] = row
        return row

    def freeze(relative):
        if relative in frozen:
            return
        path = (source_root/relative).resolve(strict=True)
        assert path.is_relative_to(source_root)
        for parent in (source_root/relative, *(source_root/relative).parents):
            assert not parent.is_symlink() and not parent.is_junction()
        before = record(path)
        dest = out/'source'/relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(path.read_bytes())
        assert identity(path) == before and dest.stat().st_nlink == 1
        frozen[relative] = dict(original=before, frozen=record(dest))
        if dest.suffix in ('.cpp', '.hpp'):
            for token in re.findall(r'^\s*#\s*include\s+"([^"]+)"', dest.read_text('utf8'), re.M):
                child = (path.parent/token).resolve()
                if not child.exists():
                    child = source_root/SCENARIO/token
                if child.is_file():
                    freeze(child.relative_to(source_root).as_posix())

    names = ('config', 'spawn_points', 'resources', 'map_resources', 'monster_types',
             'trigger_rules', 'chaos_routes', 'map_catalog', 'boss_skills', 'upgrade_tables',
             'difficulty', 'difficulty_monsters', 'monster_attributes')
    units = [SCENARIO+n+'.cpp' for n in names] + [TEST+'main.cpp']
    for rel in [*units, TEST+'run.py', TEST+'CMakeLists.txt']:
        freeze(rel)
    expected = [
        (args.rrr, 'd169aa0f89c9668d528d386126e12d54183951e8bde189329d8df10ec8283abc'),
        (args.decoded_rrr, '9a76fddb874839ccf33d9f355c1d9917940b7cad55f8d18cec701de3a6e5581e'),
    ]
    resources = []
    for index, (path, sha) in enumerate(expected):
        row = record(path)
        assert row['sha256'] == sha, str(path)
        dest = out/('rrr.cso' if index == 0 else 'rrr.decoded.csv')
        dest.write_bytes(Path(row['path']).read_bytes())
        resources.append(record(dest))
    commands = read(args.baseline/'build-commands.json')
    record(args.baseline/'build-commands.json')
    record(args.baseline/'environment.json')
    compiler = next(j['command'][0] for j in commands if j['kind'] == 'compile')
    linker = next(token for job in commands if job['kind'] == 'link'
                  for token in job['command'] if Path(token).name.lower() == 'lld-link.exe')
    record(compiler); record(linker)
    env = os.environ | read(args.baseline/'environment.json')
    env['TEMP'] = env['TMP'] = str(out/'temp')
    result = dict(success=False, source=frozen, steps=[], resources=resources,
                  fullDLL=False, deployed=False, liveGameplay=False)

    def save():
        (out/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), 'utf8')

    def run(name, kind, command):
        rsp = out/(name+'.rsp')
        rsp.write_text(subprocess.list2cmdline(command[1:]), 'utf8')
        begin = time.monotonic()
        done = subprocess.run(command if kind == 'test' else [command[0], '@'+str(rsp)],
                              cwd=ROOT, env=env, capture_output=True, timeout=180)
        log = out/(name+'.log'); log.write_bytes(done.stdout+done.stderr)
        text = log.read_text('utf-8-sig', errors='replace')
        for path in set(re.findall(r'Note: including file:\s*([^\r\n]+)', text)):
            record(path.strip())
        result['steps'].append(dict(name=name, kind=kind, command=command, exit=done.returncode,
                                    seconds=time.monotonic()-begin, log=record(log)))
        save()
        print(name, done.returncode, flush=True)
        if kind == 'test' or done.returncode:
            print(text[-5000:], flush=True)
        assert done.returncode == 0, name

    try:
        objects = []
        for rel in units:
            obj = out/'objects'/(Path(rel).stem+'.obj')
            objects.append(str(obj))
            run(Path(rel).stem, 'compile', [compiler, '/c', '/nologo', '/W4', '/WX', '/EHsc', '/MT',
                '/utf-8', '/std:c++20', '--target=i686-pc-windows-msvc', '/arch:AVX2', '/fp:precise',
                '/clang:-O3', '/clang:-flto=thin', '/Gy', '/Gw', '/showIncludes',
                '/I'+str(out/'source'/SCENARIO), '/Fo'+str(obj), str(out/'source'/rel)])
        run('link', 'link', [linker, '/subsystem:console', '/machine:x86', '/out:'+str(out/'test.exe'),
                           '/threads:2', '/opt:lldltojobs=2', '/opt:ref', *objects])
        run('profiles', 'test', [str(out/'test.exe')])
        run('rrr', 'test', [str(out/'test.exe'), '--chaos-resource', *[r['path'] for r in resources]])
        result['executable'] = record(out/'test.exe')
        result['success'] = True
    finally:
        result['inputs'] = list(inputs.values())
        result['changedAfter'] = [p for p,r in inputs.items() if identity(p) != r]
        result['success'] &= not result['changedAfter']
        save()


if __name__ == '__main__':
    main()
