#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test native/Renderer resource retirement with the configured x86 SDK command."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--renderer-command', required=True, type=Path,
                        help='JSON with argv from the configured Renderer C++ compile command')
    parser.add_argument('--environment', type=Path)
    parser.add_argument('--legacy-ref', help='Optional Git revision of the previous model cache implementation')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a new output directory under build')
    command = json.loads(args.renderer_command.read_text('utf-8-sig'))['argv']
    compiler = Path(command[0]).resolve(strict=True)
    flags = [s for s in command[1:] if not s.startswith(('/Fo', '/Fd'))
             and s not in {'/c', '-c', '--'} and not s.endswith('.cpp')]
    flags += ['/UNDEBUG', '/DNOMINMAX', '/utf-8']
    env = os.environ.copy()
    if args.environment:
        env.update(json.loads(args.environment.read_text('utf-8-sig')))
    out.mkdir(parents=True)
    (out / 'temp').mkdir()
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    steps, inputs = [], {}

    def track(path):
        path = Path(path).resolve(strict=True)
        value = hashlib.sha256(path.read_bytes()).hexdigest()
        if str(path) in inputs and inputs[str(path)] != value:
            raise RuntimeError(f'Input changed: {path}')
        inputs[str(path)] = value
        return path

    def execute(name, argv, negative=False):
        argv = list(map(str, argv))
        p = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (out / (name + '.log')).write_bytes(p.stdout)
        steps.append(dict(name=name, argv=argv, exit=p.returncode, expected_failure=negative))
        (out / 'steps.json').write_text(json.dumps(steps, indent=2), 'utf-8')
        print(name, p.returncode, flush=True)
        if (p.returncode == 0) == negative:
            print(p.stdout.decode('utf-8', errors='replace')[-6000:])
            raise RuntimeError(name)

    def compile(name, source, extra=()):
        obj = out / (name + '.obj')
        execute('compile-' + name, [compiler, *flags, *extra, '/Fo' + str(obj),
                                  '/Fd' + str(out / (name + '.pdb')), '/c', track(source)])
        return obj

    def link_run(name, objects, negative=False):
        exe = out / (name + '.exe')
        execute('link-' + name, [compiler, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
                                *objects, '/Fe' + str(exe), '/link', '/threads:1', '/OPT:REF', 'opengl32.lib'])
        execute('test-' + name, [exe], negative)

    for path in [ROOT / 'native/include/goldcraft/precache_api.h',
                 ROOT / 'native/client/precache_client.cpp',
                 ROOT / 'external/MetaHookSv-20261007/Plugins/Renderer/src/resource_refresh.inc']:
        track(path)
    fixture = compile('cache-fixture', ROOT / 'tests/native/resource_model_cache_tests.cpp')
    cache = compile('cache', ROOT / 'native/client/resource_model_cache.cpp')
    link_run('cache', [fixture, cache])
    if args.legacy_ref:
        original = subprocess.check_output(['git', 'show', args.legacy_ref + ':native/client/resource_model_cache.cpp'], cwd=ROOT)
        previous = out / 'old_resource_model_cache.cpp'
        previous.write_bytes(original)
        old = compile('old-cache', previous, ['-I' + str(ROOT / 'native/client')])
        link_run('old-cache', [fixture, old], True)

    # Only the actual SDK constructors are needed for this owner fixture. Read
    # their definitions from the pinned working source; do not ship copied SDK.
    studio = track(ROOT / 'external/MetaHookSv-20261007/Plugins/Renderer/src/gl_cvar.cpp').read_text('utf-8')
    constructors = []
    for symbol in ['StudioConVar::~StudioConVar()', 'StudioConVar::StudioConVar()']:
        start = studio.index(symbol)
        end = studio.index('}', studio.index('{', start)) + 1
        constructors.append(studio[start:end])
    ctor_source = out / 'constructors.cpp'
    ctor_source.write_text('#include "gl_local.h"\n' + '\n'.join(constructors) + '\n', 'utf-8')
    ctor = compile('constructors', ctor_source)
    renderer = compile('renderer', ROOT / 'tests/native/renderer_resource_tests.cpp')
    link_run('renderer', [renderer, ctor])
    precache = compile('precache', ROOT / 'tests/native/precache_client_tests.cpp')
    link_run('precache', [precache])
    for path in tuple(inputs):
        track(path)
    (out / 'result.json').write_text(json.dumps(dict(passed=True, inputs=inputs,
        scope='SDK component tests; no game process, full DLL or GPU validation.'), indent=2), 'utf-8')


if __name__ == '__main__':
    main()
