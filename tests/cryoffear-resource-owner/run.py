#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the real SDK cold owner and XAudio2/Aurora consumers, without game launch."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = (
    'native/csol/client/weapons/guest_cof_resources.cpp',
    'native/csol/client/weapons/guest_cof_audio.cpp',
    'native/csol/client/weapons/guest_cof_particles.cpp',
    'native/csol/client/weapons/guest_cof_effects.cpp',
    'native/guest/cof/client/weapon_effects.cpp', 'native/guest/cof/client/view_punch.cpp',
    'native/guest/cof/client/forward_displacement.cpp',
    'native/guest/cof/client/audio.cpp', 'native/guest/cof/client/presentation.cpp',
    'native/guest/cof/aim_fov.cpp', 'native/guest/cof/sound_protocol.cpp',
    'native/guest/cof/particle_protocol.cpp', 'native/guest/cof/client/aurora.cpp',
    'native/guest/cof/client/aurora_render.cpp', 'tests/cryoffear-resource-owner/main.cpp')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('environment', 'commands', 'original', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.absolute()
    if (out.exists() or not out.resolve().is_relative_to(ROOT/'build') or
            any(p.is_symlink() or os.path.isjunction(p) for p in (out, *out.parents))):
        parser.error('Use a fresh, independent workspace build directory')
    expected = json.loads(Path(__file__).with_name('inputs.json').read_text('utf-8'))
    resources = {args.original / name: digest for name, digest in expected.items()}
    if any(sha(path) != digest for path, digest in resources.items()):
        parser.error('Original resource identity differs')
    env = os.environ | {k: v for k, v in json.loads(args.environment.read_text('utf-8-sig')).items()
                        if k in ('INCLUDE', 'LIB', 'LIBPATH', 'PATH')}
    out.mkdir(parents=True)
    (out/'temp').mkdir()
    env.update(TEMP=str(out/'temp'), TMP=str(out/'temp'))
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'
    linker = compiler.with_name('lld-link.exe')
    db = json.loads(args.commands.read_text('utf-8'))
    command = next(e['command'] for e in db
                   if e['file'].replace(chr(92), '/').endswith('/native/csol/client/plugin.cpp'))
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', command)
    flags = ['/nologo', '/TP', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/W3', '/WX',
             '--target=i686-pc-windows-msvc', '/O2', '/clang:-O3', '/clang:-flto=thin',
             '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off', '-Wno-overriding-option',
             '/DNDEBUG', *[s for s in tokens if s.startswith('-D')],
             *['/imsvc' + s[2:] for s in tokens if s.startswith('-I')]]
    inputs = {ROOT/s for s in SOURCES} | set(resources) | {
        compiler, linker, args.commands.resolve(), args.environment.resolve(),
        ROOT/'.tools/clang-toolchain.json', Path(__file__).resolve(),
        Path(__file__).with_name('inputs.json'), Path(__file__).with_name('README.md')}
    steps = []

    def run(name, argv):
        result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=env, capture_output=True,
                                encoding='utf-8', timeout=90)
        (out/(name + '.log')).write_text(result.stdout + result.stderr, 'utf-8')
        steps.append(dict(name=name, argv=list(map(str, argv)), exit=result.returncode))
        (out/'steps.json').write_text(json.dumps(steps, indent=2), 'utf-8')
        if result.returncode:
            print(result.stdout + result.stderr, flush=True)
            result.check_returncode()
        return result.stdout + result.stderr

    for i, source in enumerate(SOURCES):
        dep = out/(str(i) + '.d')
        run('dependencies-' + str(i), [compiler, *flags, '/clang:-M', '/clang:-MF',
                                      '/clang:' + str(dep), ROOT/source])
        make = dep.read_text('utf-8').replace('\\\n', ' ')
        inputs.update(Path(p.replace('\\ ', ' ')).resolve()
                      for p in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1]))
    before = {str(p): sha(p) for p in sorted(inputs)}
    objects = []
    for i, source in enumerate(SOURCES):
        obj = out/(str(i) + '.obj')
        objects.append(obj)
        run('compile-' + str(i), [compiler, *flags, '/c', '/Fo' + str(obj), ROOT/source])
    exe = out/'resource-owner.exe'
    run('link', [linker, '/nologo', '/machine:X86', '/subsystem:console', '/OPT:REF', '/OPT:ICF',
                 '/threads:1', '/out:' + str(exe), *objects, 'ole32.lib'])
    result = run('test', [exe, args.original.resolve()])
    after = {p: sha(p) for p in before}
    receipt = dict(success=before == after, revision=subprocess.check_output(
        ['git', 'rev-parse', 'HEAD'], cwd=ROOT, encoding='utf-8').strip(),
        before=before, after=after, unchanged=before == after, steps=steps, flags=flags,
        result=result, executableSha256=sha(exe), sourceCopies=0, actualSdk=True, realXAudio2=True,
        fullDll=False, live=False, audible=False, performance=False)
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(result.strip(), flush=True)
    if not receipt['success']:
        raise RuntimeError('Actual inputs changed during validation')


if __name__ == '__main__':
    main()
