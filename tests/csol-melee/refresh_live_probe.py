"""Compile an updated Pawn probe while preserving a reviewed resource plan.

Reuse requires exact hashes of the previous compiler, includes and resources.
The new source alone is copied into a new output directory. This performs no
deployment, process launch or modification of existing evidence.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import time


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def verify(record):
    actual = identity(record['path'])
    assert all(actual[key] == record[key] for key in actual), record['path']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--include-header', type=Path, action='append', default=[])
    args = parser.parse_args()
    plan = json.loads((args.previous / 'plan.json').read_text('utf-8-sig'))
    verify(plan['compiler'])
    verify(plan['compilerDll'])
    for row in plan['files']:
        verify(row['frozen'])
    for row in plan['resources']:
        verify(row['source'])
    verify(plan['reapi'])
    args.output.mkdir(parents=True)
    source = args.output / 'source/probe.sma'
    source.parent.mkdir()
    shutil.copy2(args.source, source)
    assert source.stat().st_nlink == 1
    assert identity(source)['sha256'] == identity(args.source)['sha256']
    old_source = Path(plan['command'][1]).resolve()
    rows = [row for row in plan['files'] if Path(row['frozen']['path']).resolve() != old_source]
    assert len(rows) + 1 == len(plan['files'])
    rows.insert(0, dict(original=identity(args.source), frozen=identity(source)))
    if args.include_header:
        includes = args.output / 'source/includes'
        includes.mkdir()
        for header in args.include_header:
            copied = includes / header.name
            assert not copied.exists()
            shutil.copy2(header, copied)
            assert copied.stat().st_nlink == 1
            assert identity(header)['sha256'] == identity(copied)['sha256']
            rows.append(dict(original=identity(header), frozen=identity(copied)))
        plan['command'].append('-i' + str(includes.resolve()))
    artifact = args.output / 'csol_melee_live_test.amxx'
    plan['files'] = rows
    plan['artifact'] = str(artifact.resolve())
    plan['command'][1] = str(source.resolve())
    plan['command'][2] = '-o' + str(artifact.resolve())
    (args.output / 'plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding='utf8')
    started = time.monotonic()
    run = subprocess.run(plan['command'], cwd=args.output, capture_output=True, timeout=90)
    log = args.output / 'compile.log'
    log.write_bytes(run.stdout + run.stderr)
    unchanged = all(identity(row['frozen']['path']) == row['frozen'] for row in rows)
    result = dict(success=run.returncode == 0 and unchanged, exitCode=run.returncode,
                  seconds=time.monotonic() - started, inputsUnchanged=unchanged,
                  log=identity(log), artifact=identity(artifact) if artifact.exists() else None)
    (args.output / 'compile-result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    print(json.dumps(result))
    if not result['success']:
        print(log.read_text('utf8', errors='replace'))
        raise SystemExit(1)


if __name__ == '__main__':
    main()
