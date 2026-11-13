#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Probe the real gameplay-settings adapter/store/parser with bounded SDK edges.

Compiles four production TUs and one fixture in place. Missing SDK link symbols
are reported, never replaced with success stubs or an expanding game-DLL link.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--msbuild-log', required=True, type=Path)
    parser.add_argument('--environment', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('matched_sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    out = sdk.checked(args.output, exists=False)
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a new workspace build directory')
    environment = sdk.read(args.environment)
    env = dict(os.environ)
    for name, value in environment.items():
        for prior in list(env):
            if prior.casefold() == name.casefold():
                del env[prior]
        env[name] = value
    command, _ = sdk.commands(args.msbuild_log)
    base = [x for x in command if not (x.lower().endswith(('.cpp', '.c')) or
        x.lower().startswith(('/yu', '/yc', '/fo', '/fd')) or
        (x.lower().startswith('/fp') and not x.lower().startswith('/fp:')))]
    base += ['/Y-', '/showIncludes']
    compiler = sdk.checked(command[0]); linker = compiler.with_name('lld-link.exe')
    units = {
        'adapter': ROOT / 'native/csol/server/custom/gameplay_settings.cpp',
        'store': ROOT / 'native/csol/server/custom/settings_store.cpp',
        'parser': ROOT / 'native/csol/scenario/custom/settings.cpp',
        'interface': ROOT / 'external/ReGameDLL_CS/regamedll/public/interface.cpp',
        'fixture': ROOT / 'tests/csol-gameplay-settings-server/main.cpp',
    }
    rapid = ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'
    base += ['/I' + str(rapid)]
    inputs = {}

    def track(path):
        row = sdk.identity(path); inputs[row['path'].casefold()] = row

    for path in [*units.values(), Path(__file__), ROOT / 'tests/csol-rebuy/run_sdk.py',
                 args.msbuild_log, args.environment, compiler, linker]:
        track(path)
    dirs = [Path(x) for x in environment['INCLUDE'].split(';') if x]
    dirs += [Path(x[2:]) for x in base if x.startswith('/I')]
    # The real ReGameDLL precompiled header includes project weapon/player
    # declarations too. Inventory headers without adding those implementation
    # TUs or accepting any missing linker symbols.
    dirs += [ROOT / 'native', rapid]
    resource = subprocess.check_output([str(compiler), '-print-resource-dir'], text=True).strip()
    dirs += [Path(resource)]
    for directory in dirs:
        if not directory.is_absolute(): directory = sdk.SDK_CWD / directory
        if directory.is_dir():
            for path in directory.rglob('*'):
                if any(part.lower().endswith('.tlog') for part in path.parts):
                    continue
                if path.is_file() and (not path.suffix or path.suffix.lower() in ('.h', '.hpp', '.inc', '.inl')):
                    track(path)
    libraries = [Path(d) / name for name in ('libcmt.lib', 'libcpmt.lib', 'libvcruntime.lib', 'libucrt.lib', 'oldnames.lib', 'kernel32.lib', 'user32.lib')
                 for d in environment['LIB'].split(';') if (Path(d) / name).is_file()]
    for path in libraries: track(path)
    out.mkdir()
    receipt = dict(success=False, sourceCopies=0, fullGameDLL=False, deployed=False, liveGameplay=False,
                   inputsBefore=list(inputs.values()), steps=[], objects={})

    def save():
        (out / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), 'utf-8')

    def run(name, values, cwd=sdk.SDK_CWD):
        values = [str(x) for x in values]
        step = dict(name=name, exit=None, command=values); receipt['steps'].append(step); save()
        done = subprocess.run(values, cwd=cwd, env=env, capture_output=True, timeout=180)
        (out / (name + '.log')).write_bytes(done.stdout + done.stderr); step['exit'] = done.returncode
        text = (done.stdout + done.stderr).decode('utf-8', errors='replace')
        if name.startswith('compile-'):
            headers = re.findall(r'Note: including file:\s*([^\r\n]+)', text)
            for header in headers:
                if str(sdk.checked(header.strip())).casefold() not in inputs:
                    raise ValueError('Unrecorded actual compiler input: ' + header)
            step['dependencies'] = headers
        elif name in ('writer', 'reader', 'failure', 'invalid', 'no-engine') and not done.returncode:
            summaries = re.findall(r'^pid=(\d+) checks=(\d+) failures=(\d+)\s*$', text, re.M)
            if len(summaries) != 1 or int(summaries[0][1]) <= 0 or int(summaries[0][2]):
                raise ValueError('Missing nonempty adapter integration summary: ' + name)
            step.update(pid=int(summaries[0][0]), checks=int(summaries[0][1]), failures=int(summaries[0][2]))
        save(); print(name, done.returncode, text[-5000:] if done.returncode or not name.startswith(('compile-', 'link-')) else '', flush=True)
        if done.returncode:
            raise RuntimeError(name + ' failed; retain exact compiler/link symbols before expanding scope')

    try:
        objects = []
        for name, source in units.items():
            obj = out / (name + '.obj'); objects.append(obj)
            run('compile-' + name, [*base, '/Fo' + str(obj), '/Fd' + str(out / (name + '.pdb')), source])
            receipt['objects'][name] = sdk.identity(obj); save()
        executable = out / 'settings-server.exe'
        run('link-probe', [linker, '/subsystem:console', '/machine:x86', '/threads:1', '/opt:lldltojobs=1',
                          '/out:' + str(executable), *objects, *libraries])
        data = out / 'data'; data.mkdir()
        for group, name in [('writer', 'normal'), ('reader', 'normal'), ('failure', 'failure'), ('invalid', 'invalid'), ('no-engine', 'no-engine')]:
            run(group, [executable, group, data / name], out)
        receipt['executable'] = sdk.identity(executable)
        receipt['success'] = True
    finally:
        receipt['changedInputs'] = [row['path'] for row in inputs.values()
            if not Path(row['path']).is_file() or sdk.identity(row['path']) != row]
        receipt['inputsUnchanged'] = not receipt['changedInputs']
        receipt['success'] &= receipt['inputsUnchanged']; save()
    return 0 if receipt['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
