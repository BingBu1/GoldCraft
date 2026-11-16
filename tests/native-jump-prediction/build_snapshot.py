#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rebuild client.cpp and relink an immutable, accepted game-DLL object set."""
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
    parser.add_argument('--seal', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    seal_path, out = sdk.checked(args.seal), sdk.checked(args.output, exists=False)
    if not seal_path.is_relative_to(ROOT / 'build') or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a sealed workspace build and a fresh output')
    seal = sdk.read(seal_path)
    sdk.verify(seal['receipt'])
    full = sdk.read(seal['receipt']['path'])
    if not full['success'] or not full['inputsUnchanged']:
        raise ValueError('Baseline full game-DLL build was not accepted')
    inputs = [*seal['linkInputs'], seal['compiler'], seal['linker'], seal['environment']]
    for row in inputs:
        sdk.verify(row)
    replaced = [row for row in seal['linkInputs'] if Path(row['path']).name.casefold() == 'client.obj']
    if len(replaced) != 1:
        raise ValueError('Expected exactly one real snapshot translation unit')
    source = ROOT / 'external/ReGameDLL_CS/regamedll/dlls/client.cpp'
    controls = [sdk.identity(p) for p in (Path(__file__), seal_path, source)]
    before = {str(Path(row['path'])).casefold(): row for row in sdk.read(seal['sourceInventory']['path'])}
    env = dict(os.environ)
    for key, value in sdk.read(seal['environment']['path']).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir()
    result = dict(success=False, sourceCopies=0, fullCurrentSourceBuild=False, deployed=False,
                  baselineArtifact=seal['artifact'], reusedLinkInputs=seal['linkInputs'],
                  replacedObject=replaced[0], controls=controls, headers=[], headerDrift=[], steps=[])

    def run(name, command):
        for row in inputs + controls:
            sdk.verify(row)
        response = out / (name + '.rsp')
        response.write_text(subprocess.list2cmdline(command[1:]), 'utf-8')
        done = subprocess.run([command[0], '@' + str(response)], cwd=sdk.SDK_CWD, env=env,
                              capture_output=True, timeout=600)
        text = (done.stdout + done.stderr).decode('utf-8', 'replace')
        (out / (name + '.log')).write_text(text, 'utf-8')
        result['steps'].append(dict(name=name, command=command, exit=done.returncode))
        if done.returncode:
            print(text[-4000:])
            raise RuntimeError(name + ' failed')
        return text

    try:
        flags = [x for x in seal['compileCommand'] if not x.lower().endswith(('.cpp', '.c'))
                 and not x.lower().startswith(('/fo', '/fd', '/yu', '/yc'))
                 and not (x.lower().startswith('/fp') and not x.lower().startswith('/fp:'))]
        obj = out / 'client.obj'
        text = run('compile-snapshot', flags + ['/Y-', '/showIncludes', '/Fo' + str(obj), str(source)])
        headers = {}
        for include in re.findall(r'Note: including file:\s*([^\r\n]+)', text):
            path = (sdk.SDK_CWD / include.strip()).resolve(strict=True)
            if path.is_relative_to(ROOT):
                row = sdk.identity(path)
                headers[row['path'].casefold()] = row
        result['headers'] = list(headers.values())
        for key, row in headers.items():
            prior = before.get(key)
            if not prior or prior['sha256'] != row['sha256']:
                result['headerDrift'].append(dict(current=row, baseline=prior))
        controls.extend(headers.values())
        command = []
        redirects = {'/out:': out / 'mp.dll', '/pdb:': out / 'mp.pdb', '/implib:': out / 'mp.lib'}
        for value in seal['linkCommand']:
            lower = value.lower()
            if lower.startswith(('/ltcgout:', '/manifestfile:')):
                continue
            if any(lower.startswith(prefix) for prefix in redirects):
                prefix = next(prefix for prefix in redirects if lower.startswith(prefix))
                command.append(prefix + str(redirects[prefix]))
            elif value.casefold() == replaced[0]['path'].casefold():
                command.append(str(obj))
            else:
                command.append(value)
        run('link', command)
        for row in inputs + controls:
            sdk.verify(row)
        result.update(success=True, inputsUnchanged=True, artifact=sdk.identity(out / 'mp.dll'),
                      pdb=sdk.identity(out / 'mp.pdb'), rebuiltObject=sdk.identity(obj))
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')
    print(json.dumps(dict(success=True, artifact=result['artifact'],
                         headerDrift=[row['current']['path'] for row in result['headerDrift']],
                         boundary='One rebuilt snapshot translation unit; remaining objects retain the sealed baseline')))


if __name__ == '__main__':
    main()
