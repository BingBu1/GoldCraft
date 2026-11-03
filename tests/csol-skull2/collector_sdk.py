#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build an isolated actual resource collector with a frozen SKULL2 overlay."""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def identity(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir()
    source = output/'source'
    (output/'temp').mkdir()
    selected = {row['path']: row for row in read(args.manifest)}
    inputs, production, copies = {}, {}, []

    def register(path):
        row = identity(path)
        inputs[key(path)] = row
        return row

    def freeze(relative):
        destination = source/relative
        if destination.exists():
            return
        original = root/relative
        incoming = original
        if relative in selected:
            incoming = args.manifest.parent/'source'/relative
            row = selected[relative]
            assert identity(incoming)['sha256'] == row['sha256']
            assert identity(original)['sha256'] == row['sha256']
        data = incoming.read_bytes()
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        assert destination.stat().st_nlink == 1
        production[key(original)] = identity(original)
        copies.append(dict(relative=relative, original=register(incoming), frozen=register(destination)))
        for token in re.findall(r'^\s*#include\s+"([^"]+)"', data.decode('utf-8-sig'), re.M):
            child = Path(os.path.normpath(str(Path(relative).parent/token))).as_posix()
            if (root/child).is_file():
                freeze(child)

    baseline = read(args.baseline)
    old = args.baseline.resolve().parent
    commands = []
    for command in baseline['commands']:
        argv = [arg.replace(str(old/'source'), str(source)).replace(str(old/'output'), str(output))
                for arg in command['argv']]
        if '/c' in argv:
            freeze(Path(argv[argv.index('/c')+1]).relative_to(source).as_posix())
        register(argv[0])
        commands.append(dict(name=command['name'], argv=argv))
    for row in baseline['inputs']:
        path = Path(row['path'])
        if '/source/' not in path.as_posix() and path.exists():
            register(path)
    register(args.manifest)
    register(args.baseline)
    register(__file__)
    env = os.environ.copy()
    env.update(baseline['environment'])
    env.update(TEMP=str(output/'temp'), TMP=str(output/'temp'), VSLANG='1033')
    write(output/'plan.json', dict(commands=commands, inputs=list(inputs.values()), copies=copies,
          production=list(production.values()), jobs=1,
          boundary='Actual resource collector only; no full DLL, runtime or deployment.'))
    result = dict(success=False, steps=[], unknown=[], changedInputs=[], changedProduction=[])
    start = time.monotonic()
    try:
        for command in commands:
            log = output/(command['name']+'.log')
            then = time.monotonic()
            with log.open('xb') as stream:
                run = subprocess.run(command['argv'], cwd=output, env=env, stdout=stream,
                                     stderr=subprocess.STDOUT, timeout=180)
            for path in re.findall(r'^\s*Note: including file:\s*(.*?)\s*$',
                                   log.read_text('utf8', errors='replace'), re.M):
                if key(path) not in inputs:
                    result['unknown'].append(path)
            result['steps'].append(dict(name=command['name'], exit=run.returncode,
                                       seconds=time.monotonic()-then, log=identity(log)))
            print(command['name'], run.returncode, flush=True)
            if run.returncode or result['unknown']:
                break
        result['success'] = len(result['steps']) == len(commands) and not result['unknown'] and all(
            row['exit'] == 0 for row in result['steps'])
    except BaseException as error:
        result['error'] = repr(error)
    finally:
        for row in inputs.values():
            if identity(row['path']) != row:
                result['changedInputs'].append(row['path'])
        for row in production.values():
            if identity(row['path']) != row:
                result['changedProduction'].append(row['path'])
        if result['changedInputs'] or result['changedProduction']:
            result['success'] = False
        result['seconds'] = time.monotonic()-start
        result['artifacts'] = [identity(path) for path in output.glob('*.exe')]
        write(output/'result.json', result)
    print(json.dumps({k: v for k, v in result.items() if k not in ('steps', 'artifacts')}))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
