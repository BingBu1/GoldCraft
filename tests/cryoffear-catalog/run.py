#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate generated CoF host metadata with the production x86 resolver."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('environment', 'client', 'catalog', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (output, *output.parents)):
        raise ValueError('Use a fresh workspace build directory without reparse points')
    output.mkdir(parents=True)
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    temporary = output / 'temp'
    temporary.mkdir()
    environment.update(TEMP=str(temporary), TMP=str(temporary))
    toolchain = ROOT / '.tools/clang-toolchain.json'
    compiler = Path(json.loads(toolchain.read_text('utf-8-sig'))['root']) / 'bin/clang-cl.exe'
    base = ROOT / 'external/MetaHookSv-20261007/MetaHook'
    includes = [base / 'include', base / 'include/Interface',
                *[base / 'include/HLSDK' / d for d in ('common', 'cl_dll', 'engine', 'pm_shared', 'public')],
                base / 'thirdparty/rapidjson/include', base / 'thirdparty/Chocobo1Hash/src']
    source = ROOT / 'tests/cryoffear-catalog/main.cpp'
    flags = ['/nologo', '/std:c++20', '/EHsc', '/MT', '/DNOMINMAX', '/O2', '/clang:-O3',
             '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off',
             '--target=i686-pc-windows-msvc', '-Wno-overriding-option',
             '-Wno-microsoft-enum-forward-reference', '/wd4005', '/wd4996',
             *['/I' + str(p) for p in includes]]

    def run(name, command):
        result = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                                text=True, timeout=90)
        (output / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        return result

    dependencies = output / 'inputs.d'
    run('headers', [str(compiler), *flags, '/clang:-M', '/clang:-MF',
                   '/clang:' + str(dependencies), str(source)]).check_returncode()
    make = dependencies.read_text().replace('\\\n', ' ')
    inputs = {Path(p.replace('\\ ', ' ')).resolve()
              for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1])}
    inputs.update((Path(__file__).resolve(), toolchain, compiler, compiler.with_name('lld-link.exe'),
                   args.environment.resolve(), args.client.resolve(), ROOT / 'tools/Build-ClientGameData.py'))
    inputs.update((ROOT / 'native/client').glob('*10210.json'))
    inputs.update(p for p in args.catalog.resolve().rglob('*.json') if p.is_file())
    before = {str(p): sha(p) for p in sorted(inputs)}
    fixed = output / 'fixed'
    generated = run('generate', [str(Path(os.sys.executable)), str(ROOT / 'tools/Build-ClientGameData.py'),
        '--client', str(args.client.resolve()), '--existing-catalog', str(args.catalog.resolve()),
        '--output', str(fixed)])
    generated.check_returncode()
    executable = output / 'catalog.exe'
    built = run('compile', [str(compiler), *flags, '/clang:-fuse-ld=lld', str(source),
        '/Fe:' + str(executable), '/link', '/machine:x86', '/threads:1', '/opt:ref'])
    built.check_returncode()
    good = run('fixed', [str(executable), str(fixed)])
    good.check_returncode()
    # Reproduce the actual defect using only generated metadata, with valid
    # content-addressed index fields. Never weaken the production resolver.
    bad = output / 'missing-operands'
    bad.mkdir()
    index = json.loads((fixed / 'index.json').read_text())
    entry = index['versions'][0]
    snapshot = json.loads((fixed / entry['url']).read_text())
    for record in snapshot['records']:
        if record['symbolName'].startswith('GoldCraftParticle_'):
            record['payload'] = {k: v for k, v in record['payload'].items() if k in ('gv_rva', 'gv_va')}
    data = (json.dumps(snapshot, indent=2) + '\n').encode()
    (bad / entry['url']).write_bytes(data)
    entry.update(sha256=hashlib.sha256(data).hexdigest(), size=len(data))
    (bad / 'index.json').write_text(json.dumps(index), encoding='utf-8')
    negative = run('missing-operands', [str(executable), str(bad)])
    if negative.returncode != 1 or 'checked=6 failures=3' not in negative.stdout:
        raise RuntimeError('The original missing-operand metadata defect was not detected')
    live = run('complete-catalog', [str(executable), str(args.catalog.resolve())])
    after = {str(p): sha(p) for p in sorted(inputs)}
    receipt = dict(success=before == after, inputsUnchanged=before == after, before=before, after=after,
        generated=json.loads(generated.stdout), fixedExit=good.returncode,
        negativeExit=negative.returncode, completeCatalogExit=live.returncode,
        sourceCopies=0, gameplay=False, performance=False,
        boundary='Actual x86 production catalog parser/query; complete external catalog reported separately')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if before != after:
        raise RuntimeError('Inputs changed during validation')
    print(json.dumps({k: receipt[k] for k in ('success', 'fixedExit', 'negativeExit', 'completeCatalogExit')}))


if __name__ == '__main__':
    main()
