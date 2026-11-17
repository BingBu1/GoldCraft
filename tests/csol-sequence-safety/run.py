#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual validation with a fixed previous validator, including timings."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
FORMER = 'aa42cb96e6b541315eb3735137fdfb9275c66a23'
FORMER_SHA = '152bb910986ee995c19632fa5f64d7d386dfa3fda1ff331b7ec8e01bccaa2ff2'


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--models', type=Path, required=True)
    args = parser.parse_args()
    output = args.out.resolve()
    if not output.is_relative_to(ROOT / 'build') or output.exists():
        parser.error('output must be a new directory under workspace build')
    output.mkdir(parents=True)
    source = ROOT / 'native/csol/sequence_safety.hpp'
    former = subprocess.check_output(['git', 'show', FORMER + ':native/csol/sequence_safety.hpp'], cwd=ROOT)
    if hashlib.sha256(former).hexdigest() != FORMER_SHA:
        raise ValueError('fixed former source identity differs')
    first = former.index(b'inline bool Animation(')
    begin = former.index(b'{', first)
    at, depth = begin + 1, 1
    while depth:
        if former[at] == 123:
            depth += 1
        elif former[at] == 125:
            depth -= 1
        at += 1
    adapter = output / 'former-animation.inc'
    adapter.write_bytes(former[first:at].replace(b'inline bool Animation(', b'inline bool FormerAnimation(', 1))
    model_list = args.models.resolve(strict=True)
    models = [Path(line).resolve(strict=True) for line in model_list.read_text('utf-8').splitlines()]
    if not models or any(not path.is_relative_to(ROOT / 'sandbox') for path in models):
        raise ValueError('model list must contain workspace sandbox files')
    inputs = [source, HERE / 'main.cpp', Path(__file__), adapter, model_list, args.environment, args.compiler]
    before = [identity(p) for p in inputs]
    model_inputs = [identity(p) for p in models]
    exe = output / 'check.exe'
    compile_command = [str(args.compiler), '--target=i686-pc-windows-msvc', '-fuse-ld=lld',
        '/std:c++20', '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
        '/clang:-ffp-contract=off', '/EHsc', '/MT', '/nologo', '/I' + str(ROOT), '/I' + str(output),
        str(HERE / 'main.cpp'), '/Fo' + str(output / 'check.obj'), '/Fe' + str(exe), '/link',
        '/OPT:REF', '/OPT:ICF', '/OPT:LLDLTO=3', '/threads:1']
    commands = [('compile', compile_command), ('compare', [str(exe), str(model_list)])]
    plan = dict(formerRevision=FORMER, formerSourceSha256=FORMER_SHA,
        formerFunction=dict(first=first, end=at, sha256=hashlib.sha256(former[first:at]).hexdigest()),
        inputs=before, models=model_inputs, commands=commands,
        boundary='Fixed-old differential correctness and warm-buffer costs; not game performance')
    (output / 'plan.json').write_text(json.dumps(plan, indent=2), 'utf-8')
    environment = dict(os.environ)
    environment.update(json.loads(args.environment.read_text('utf-8-sig')))
    receipt = dict(success=False, steps=[])
    try:
        for name, command in commands:
            started = time.monotonic()
            with (output / (name + '.log')).open('wb') as log:
                run = subprocess.run(command, cwd=output, env=environment, stdout=log, stderr=subprocess.STDOUT,
                    timeout=180, creationflags=subprocess.CREATE_NO_WINDOW)
            receipt['steps'].append(dict(name=name, exit=run.returncode, seconds=time.monotonic()-started))
            if run.returncode:
                raise RuntimeError(name + ' failed; inspect log')
        receipt['inputsUnchanged'] = before == [identity(p) for p in inputs]
        receipt['modelsUnchanged'] = model_inputs == [identity(p) for p in models]
        if not receipt['inputsUnchanged'] or not receipt['modelsUnchanged']:
            raise RuntimeError('measured inputs changed')
        receipt['output'] = (output / 'compare.log').read_text('utf-8')
        receipt['success'] = True
    except Exception as error:
        receipt['error'] = str(error)
    finally:
        (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(json.dumps(receipt))
    return 0 if receipt['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
