#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual CoF action damage against a current sealed full server build."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import re
import time

ROOT = Path(__file__).resolve().parents[2]
GROUPS = 'cof-native,cof-damage,cof-death,cof-reentry,cof-cancel,cof-owner,kick-damage,kick-stagger,kick-death,kick-reentry,kick-cancel,kick-owner'

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('build', 'environment', 'light-provider', 'sample', 'proof', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('sdk_runner', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    out = sdk.checked(args.output, exists=False)
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Fresh workspace SDK output required')
    proof_path, sample = sdk.checked(args.proof), sdk.checked(args.sample)
    proof = sdk.read(proof_path)
    if not proof['beforeImplementation'] or sdk.digest(sample) != proof['mpSha256']:
        raise ValueError('Original CoF identity or pre-edit proof mismatch')
    raw = sample.read_bytes()
    nt = int.from_bytes(raw[60:64], 'little')
    count = int.from_bytes(raw[nt+6:nt+8], 'little')
    optional = nt + 24
    table = optional + int.from_bytes(raw[nt+20:nt+22], 'little')
    image = int.from_bytes(raw[optional+28:optional+32], 'little')
    for region in proof['regionsVerified']:
        rva, size = int(region['va'], 16) - image, region['size']
        data = None
        for index in range(count):
            section = table + index * 40
            va = int.from_bytes(raw[section+12:section+16], 'little')
            length = int.from_bytes(raw[section+16:section+20], 'little')
            if va <= rva and rva + size <= va + length:
                at = int.from_bytes(raw[section+20:section+24], 'little') + rva - va
                data = raw[at:at+size]
                break
        if data is None or hashlib.sha256(data).hexdigest() != region['sha256']:
            raise ValueError('Original instruction region changed: ' + region['va'])
    provider_path = sdk.checked(args.light_provider)
    provider = sdk.read(provider_path)
    if not provider['success'] or not provider['inputsUnchanged']:
        raise ValueError('Actual engine lightstyle provider build did not pass')
    inputs = [sdk.identity(Path(__file__)), sdk.identity(proof_path), sdk.identity(sample),
              sdk.identity(provider_path), provider['artifact'], *provider['inputs']]
    for row in inputs:
        sdk.verify(row)
    env = dict(os.environ)
    env['GOLDCRAFT_TEST_LIGHT_PROVIDER'] = provider['artifact']['path']
    build = sdk.checked(args.build)
    command = [sys.executable, '-B', str(Path(__file__)), *sys.argv[1:]]
    result = 1
    try:
        result = execute(sdk, build, sdk.checked(args.environment), out, env, inputs)
    finally:
        unchanged = True
        try:
            for row in inputs:
                sdk.verify(row)
        except ValueError:
            unchanged = False
        record = dict(success=result == 0 and unchanged, inputsUnchanged=unchanged,
            inputs=inputs, command=command, exit=result, originalRegions=len(proof['regionsVerified']),
            boundary='Actual controlled-engine SDK damage; no deployment, gameplay or live performance')
        out.with_name(out.name + '-driver.json').write_text(json.dumps(record, indent=2) + '\n', 'utf8')
    return 0 if record['success'] else 1

def execute(sdk, build, environment_path, out, env, original_inputs):
    """Seal actual compiler dependencies and every production/link input.

    A project-root include path must not recursively inventory build/sandbox.
    Dependency discovery uses the same compiler flags before the real compile;
    /showIncludes and verbose linker records must stay inside that sealed set.
    """
    arguments = argparse.Namespace(receipt=build / 'receipt.json',
        source_inputs=build / 'sources/source-inputs.json', msbuild_log=build / 'msbuild.log',
        environment=environment_path)
    current, production, environment = sdk.build_provenance(arguments)
    seal_path = build / 'link-inputs.json'
    if sdk.read(seal_path) != current:
        raise ValueError('Current production objects differ from their original seal')
    for key, value in environment.items():
        for old in list(env):
            if old.casefold() == key.casefold(): del env[old]
        env[key] = value
    out.mkdir(); (out / 'temp').mkdir()
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    fixture = Path(__file__).with_name('sdk.cpp')
    compiler = current['compileCommand']
    base = [x for x in compiler if not (x.lower().endswith(('.cpp', '.c')) or
        x.lower().startswith(('/yu', '/yc', '/fo', '/fd')) or
        (x.lower().startswith('/fp') and not x.lower().startswith('/fp:')))]
    base += ['/Y-', '/I' + str(ROOT), '/I' + str(ROOT / 'build/scenario-sdk-current-20261108-05')]
    dependencies = out / 'compiler.d'
    discover = [*base, '/clang:-M', '/clang:-MF', '/clang:' + str(dependencies), str(fixture)]
    discovered = subprocess.run(discover, cwd=sdk.SDK_CWD, env=env, capture_output=True, timeout=60)
    (out / 'dependency.log').write_bytes(discovered.stdout + discovered.stderr)
    if discovered.returncode:
        raise RuntimeError('Actual compiler dependency discovery failed')
    make = dependencies.read_text().replace(chr(92) + chr(10), ' ')
    consumed = [sdk.checked(x.replace(chr(92) + ' ', ' '))
        for x in re.findall(r'(?:\\ |[^\s])+', make.split(':', 1)[1])]
    tracked = {}
    def track(path, expected=None):
        value = sdk.identity(path); key = value['path'].casefold()
        if expected and value['sha256'] != expected:
            raise ValueError('Production input changed before compile: ' + value['path'])
        if key in tracked and tracked[key]['sha256'] != value['sha256']:
            raise ValueError('Actual dependency changed during preparation: ' + value['path'])
        tracked[key] = value
    for row in production + current['linkInputs'] + original_inputs:
        track(row['path'], row['sha256'])
    # /MT emits these default-library directives. Resolve each through the
    # recorded LIB search order before linking; do not inventory entire roots.
    for name in ('libcmt.lib', 'oldnames.lib', 'libcpmt.lib', 'libvcruntime.lib', 'libucrt.lib'):
        library = next((Path(directory) / name for directory in environment['LIB'].split(';')
                        if (Path(directory) / name).is_file()), None)
        if library is None:
            raise ValueError('Recorded compiler runtime library is missing: ' + name)
        track(library)
    for path in consumed + [fixture, Path(__file__), ROOT / 'tests/csol-rebuy/run_sdk.py', seal_path]:
        track(path)
    for key in ('receipt', 'sourceInventory', 'log', 'environment', 'artifact', 'compiler', 'linker'):
        track(current[key]['path'], current[key]['sha256'])
    record = dict(success=False, inputsUnchanged=False, sourceCopies=0, fullDLL=False,
        deployed=False, liveGameplay=False, fixture=str(fixture), productionSeal=str(seal_path),
        revision=subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip(),
        inputScope='All sealed production/link/original/provider inputs plus compiler-discovered dependencies',
        dependencyCommand=discover, steps=[], inputsBefore=list(tracked.values()))
    def save(): (out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n', 'utf8')
    compile_job = [*base, '/showIncludes', '/Fo' + str(out / 'fixture.obj'), str(fixture)]
    remove = ('/out:', '/pdb:', '/implib:', '/def:', '/ltcgout:', '/manifest', '/natvis:',
              '/subsystem:', '/threads:', '/opt:lldltojobs=', '/lldltocache:')
    link_job = [x for x in current['linkCommand'] if x.lower() != '/dll' and not x.lower().startswith(remove)]
    link_job += ['/subsystem:console', '/out:' + str(out / 'rebuy.exe'), '/pdb:' + str(out / 'rebuy.pdb'),
                 '/threads:1', '/opt:lldltojobs=1', '/verbose', str(out / 'fixture.obj')]
    jobs = [('compile', compile_job), ('link', link_job)]
    jobs += [(group, [str(out / 'rebuy.exe'), group]) for group in GROUPS.split(',')]
    try:
        for name, job in jobs:
            save(); start = time.monotonic(); launched = job
            if name in ('compile', 'link'):
                response = out / (name + '.rsp')
                response.write_text(subprocess.list2cmdline(job[1:]), 'utf8')
                launched = [job[0], '@' + str(response)]
            done = subprocess.run(launched, cwd=sdk.SDK_CWD, env=env, capture_output=True,
                timeout=600 if name == 'link' else 240)
            log = out / (name + '.log'); log.write_bytes(done.stdout + done.stderr)
            text = log.read_text('utf-8-sig', errors='replace')
            observed = re.findall(r'Note: including file:\s*([^\r\n]+)', text) if name == 'compile' else []
            if name == 'link':
                archives = {}
                for archive, member in re.findall(r'^lld-link: Reading (.*?\.lib)(\([^\r\n]*\))?\s*$', text, re.M):
                    if member:
                        if len(archives.get(Path(archive).name.casefold(), set())) != 1:
                            raise ValueError('Unresolved actual linker archive member')
                    else:
                        path = sdk.checked(sdk.SDK_CWD / archive)
                        archives.setdefault(Path(archive).name.casefold(), set()).add(str(path))
                        observed.append(str(path))
            for value in observed:
                path = sdk.checked(sdk.SDK_CWD / value.strip()); key = str(path).casefold()
                if key not in tracked:
                    raise ValueError('Unrecorded actual compiler/linker input: ' + str(path))
                track(path, tracked[key]['sha256'])
            record['steps'].append(dict(name=name, command=job, exit=done.returncode,
                seconds=time.monotonic()-start, log=str(log), dependencies=observed))
            save(); print(name, done.returncode, flush=True)
            if done.returncode or name not in ('compile', 'link'): print(text[-8000:], flush=True)
            if done.returncode: return 1
            if name not in ('compile', 'link') and not re.search(r'^behaviorChecks=[1-9]\d* checks=[1-9]\d* failures=0\s*$', text, re.M):
                raise ValueError('Missing nonempty successful SDK behavior result')
        record['executable'] = sdk.identity(out / 'rebuy.exe')
        record['success'] = True
    finally:
        record['changedInputs'] = [row['path'] for row in tracked.values()
            if not Path(row['path']).is_file() or sdk.digest(row['path']) != row['sha256']]
        record['inputsUnchanged'] = not record['changedInputs']
        record['success'] &= record['inputsUnchanged']; save()
    return 0 if record['success'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
