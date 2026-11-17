#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual IFileSystem009 metadata regression against immutable resources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('component', 'filesystem', 'resources', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    component, output = args.component.resolve(), args.output.resolve()
    if not component.is_relative_to(ROOT / 'build') or not output.is_relative_to(ROOT / 'build') or output.exists():
        parser.error('component and fresh output must be inside workspace build')
    if any(p.is_symlink() or p.is_junction() for p in (output, *output.parents)):
        parser.error('linked output rejected')
    if sha(args.filesystem) != '07a432b6214c6ea5ea4a988d5c8384d3ab35a531b49fe71c354f3fed5ce9f316':
        parser.error('reviewed original IFileSystem009 DLL required')
    compiled = json.loads((component / 'receipt.json').read_text('utf-8'))
    if not compiled['success'] or not compiled['unchanged']:
        parser.error('component compilation and lifecycle checks must pass first')
    exe = component / 'overlay.exe'
    before = dict(compiled['before'])
    for path in (Path(__file__).resolve(), args.resources.resolve(), exe):
        before[str(path)] = sha(path)
    if any(sha(Path(path)) != digest for path, digest in before.items()):
        parser.error('component input changed since compilation')
    resources = [Path(line).resolve(strict=True) for line in args.resources.read_text('utf-8').splitlines()]
    if not resources or any(not path.is_relative_to(ROOT / 'sandbox') for path in resources):
        parser.error('only existing workspace sandbox resources')
    stamps = [(p.stat().st_size, p.stat().st_mtime_ns, p.stat().st_ino) for p in resources]
    output.mkdir(parents=True)
    command = [str(exe), str(args.filesystem.resolve()), str(output / 'fixture'), str(args.resources.resolve())]
    result = dict(success=False, command=command, inputs=before, resources=len(resources),
        sourceCopies=0, mainRuntimeModified=False, performanceScope='Actual VFS component; not whole map or gameplay')
    try:
        run = subprocess.run(command, cwd=output, capture_output=True, timeout=120,
            creationflags=subprocess.CREATE_NO_WINDOW)
        (output / 'test.log').write_bytes(run.stdout + run.stderr)
        result['exit'] = run.returncode
        if run.returncode:
            raise RuntimeError('actual original-DLL fixture failed; see test.log')
        records = [json.loads(line) for line in run.stdout.decode('utf-8').splitlines() if line.startswith('{')]
        result['rounds'] = [row for row in records if 'round' in row]
        result['checks'] = records[-1]['checks']
        result['failures'] = records[-1]['failures']
        if len(result['rounds']) != 3 or result['failures']:
            raise RuntimeError('missing new/native/new comparison')
        result['inputsUnchanged'] = all(sha(Path(path)) == digest for path, digest in before.items())
        result['resourcesUnchanged'] = stamps == [(p.stat().st_size, p.stat().st_mtime_ns, p.stat().st_ino) for p in resources]
        result['success'] = result['inputsUnchanged'] and result['resourcesUnchanged']
    except Exception as error:
        result['error'] = str(error)
    finally:
        (output / 'receipt.json').write_text(json.dumps(result, indent=2), 'utf-8')
    print(json.dumps({key:value for key,value in result.items() if key not in ('inputs', 'command')}))
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
