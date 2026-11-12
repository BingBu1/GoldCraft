#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Test current Teddy bodies over explicitly sealed prior SDK support objects.

This is a component test, not acceptance of a drifting full-server build. The
prior broad rejection is retained. Actual new compiler dependencies must match
the recorded headers, and all reused object/library identities stay unchanged.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--adapter-dir', type=Path, required=True)
    parser.add_argument('--fixture', choices=('ground', 'pack', 'reward'), default='ground')
    parser.add_argument('--groups', required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('component_sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    base, out = sdk.checked(args.baseline), sdk.checked(args.output, exists=False)
    adapter = sdk.checked(args.adapter_dir)
    if not base.is_relative_to(ROOT / 'build') or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use an existing workspace baseline and fresh component output')
    original = sdk.read(base / 'receipt.json')
    if any(step['exit'] != 0 for step in original['steps']) or not original['artifact']:
        raise ValueError('All baseline compile and link steps must have finished successfully')
    sdk.verify(original['artifact'])
    environment = sdk.checked(args.environment)
    if not any(sdk.checked(row['path']) == environment and row['sha256'] == sdk.digest(environment)
               for row in original['controls']):
        raise ValueError('Use the exact recorded compiler environment')
    env = dict(os.environ)
    if any(env.get(key) for key in ('CL', '_CL_')):
        raise ValueError('Unrecorded compiler argument environment is not supported')
    for key, value in sdk.read(environment).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    template, link = sdk.commands(base / 'msbuild.log')
    support = sdk.resolve_link_inputs(link, sdk.read(environment))
    old_sources = {str(sdk.checked(row['path'])).casefold(): row
                   for row in sdk.read(base / 'sources/source-inputs.json')}
    sources = {
        'scenario_teddy_bear.obj': ROOT / 'native/csol/server/scenario/bosses/scenario_teddy_bear.cpp',
        'csol_meteor_scenario_teddy_meteor.obj': ROOT / 'native/csol/server/scenario/bosses/scenario_teddy_meteor.cpp',
        'scenario_death_owner.obj': ROOT / 'native/csol/server/scenario/core/scenario_death_owner.cpp',
    }
    fixture = (ROOT / 'tests/csol-scenario-teddy-bear/ground_sdk.cpp' if args.fixture == 'ground' else
               ROOT / 'tests/csol-teddy-reward' / ('pack_sdk.cpp' if args.fixture == 'pack' else 'sdk.cpp'))
    allowed = set(sources.values()) | {fixture}
    inputs = {str(path).casefold(): sdk.identity(path) for path in allowed}
    controls = [sdk.identity(path) for path in (Path(__file__), ROOT / 'tests/csol-rebuy/run_sdk.py',
                base / 'receipt.json', base / 'msbuild.log', base / 'sources/source-inputs.json', environment,
                Path(template[0]), Path(link[0]))]
    for path in (ROOT / 'native/csol/config').rglob('*'):
        if path.is_file():
            inputs[str(path).casefold()] = sdk.identity(path)
    for path in adapter.rglob('*'):
        if path.is_file():
            inputs[str(path).casefold()] = sdk.identity(path)
    for path in (ROOT / 'tests').rglob('*'):
        if path.is_file() and path.suffix.lower() in ('.h', '.hpp', '.inc', '.inl'):
            inputs[str(path).casefold()] = sdk.identity(path)
    generated = []
    out.mkdir(parents=True)
    report = dict(success=False, inputsUnchanged=False, sourceCopies=0, fullDLL=False,
        originalBroadReceiptAccepted=False, baselineReceipt=sdk.identity(base / 'receipt.json'),
        supportBoundary='Current explicit object snapshot from the prior SDK build; no claim that its rejected full build is accepted',
        supportInputs=support, controls=controls, fixture=str(fixture), steps=[],
        deployed=False, gameplay=False, performanceAccepted=False)
    started = time.monotonic()

    def save():
        report['inputsBefore'] = list(inputs.values())
        report['generatedArtifacts'] = generated
        (out / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', 'utf8')

    def verify():
        for row in support + controls + list(inputs.values()) + generated:
            sdk.verify(row)

    def execute(name, command, kind):
        verify()
        rsp = out / (name + '.rsp')
        if kind != 'behavior':
            rsp.write_text(subprocess.list2cmdline(command[1:]), 'utf8')
        job = command if kind == 'behavior' else [command[0], '@' + str(rsp)]
        step_started = time.monotonic()
        done = subprocess.run(job, cwd=sdk.SDK_CWD, env=env, capture_output=True, timeout=300)
        (out / (name + '.log')).write_bytes(done.stdout + done.stderr)
        text = (done.stdout + done.stderr).decode('utf8', errors='replace')
        report['steps'].append(dict(name=name, kind=kind, command=command, exit=done.returncode,
                                    seconds=time.monotonic() - step_started))
        if not done.returncode and kind in ('compile', 'link'):
            prefix = '/fo' if kind == 'compile' else '/out:'
            outputs = [value[len(prefix):] for value in command if value.lower().startswith(prefix)]
            if len(outputs) != 1:
                raise ValueError('Need one explicit generated object or executable')
            generated.append(sdk.identity(outputs[0]))
            if kind == 'link':
                report['artifact'] = generated[-1]
        if kind == 'compile' and not done.returncode:
            for include_name in re.findall(r'Note: including file:\s*([^\r\n]+)', text):
                path = sdk.checked(sdk.SDK_CWD / include_name.strip())
                if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / 'build'):
                    row = sdk.identity(path)
                    if path not in allowed and not path.is_relative_to(ROOT / 'tests'):
                        old = old_sources.get(str(path).casefold())
                        if not old or row['sha256'] != old['sha256']:
                            raise ValueError('Prior support ABI/header changed: ' + str(path))
                    previous = inputs.setdefault(str(path).casefold(), row)
                    if previous != row:
                        raise ValueError('Compiler input changed during this component test: ' + str(path))
        if kind == 'behavior' and not done.returncode:
            values = re.findall(r'^behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$', text, re.M)
            if len(values) != 1 or int(values[0][0]) <= 0 or int(values[0][1]) <= 0 or int(values[0][2]):
                raise ValueError('Missing actual SDK behavior result')
            report['steps'][-1]['behaviorChecks'], report['steps'][-1]['sdkChecks'] = map(int, values[0][:2])
        save()
        print(name, done.returncode, flush=True)
        if done.returncode or kind == 'behavior':
            print(text[-8000:], flush=True)
        if done.returncode:
            raise RuntimeError('Component step failed: ' + name)

    try:
        flags = [value for value in template if not (value.lower().endswith(('.cpp', '.c')) or
                 value.lower().startswith(('/yu', '/yc', '/fo', '/fd')) or
                 (value.lower().startswith('/fp') and not value.lower().startswith('/fp:')))]
        flags += ['/Y-', '/showIncludes', '/I' + str(ROOT), '/I' + str(adapter)]
        for index, path in enumerate([*sources.values(), fixture]):
            execute('compile-' + str(index), [*flags, '/Fo' + str(out / f'{index}.obj'), str(path)], 'compile')
        remove = ('/out:', '/pdb:', '/implib:', '/def:', '/ltcgout:', '/manifest', '/natvis:',
                  '/subsystem:', '/threads:', '/opt:lldltojobs=', '/lldltocache:')
        linked = [value for value in link if value.lower() != '/dll' and not value.lower().startswith(remove)
                  and Path(value).name.casefold() not in sources]
        for obj in sources:
            if len([row for row in support if Path(row['path']).name.casefold() == obj]) != 1:
                raise ValueError('Ambiguous support object replacement: ' + obj)
        exe = out / 'teddy-components.exe'
        linked += ['/SUBSYSTEM:CONSOLE', '/OUT:' + str(exe), '/PDB:' + str(out / 'teddy-components.pdb'),
                   '/threads:1', '/opt:lldltojobs=1', *(str(out / f'{index}.obj') for index in range(4))]
        execute('link', linked, 'link')
        for group in args.groups.split(','):
            execute(group, [str(exe), group, str(ROOT / 'native/csol/config')], 'behavior')
        verify()
        report['inputsUnchanged'] = report['success'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['seconds'] = time.monotonic() - started
        save()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
