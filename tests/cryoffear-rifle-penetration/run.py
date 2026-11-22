# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute CoF rifles through sealed actual FireBullets3 server objects.

No source snapshots, simulated native material function or production deploy.
This component test does not revalidate the entire later working tree.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--adapter-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = sdk.checked(args.baseline)
    adapter = sdk.checked(args.adapter_dir)
    out = sdk.checked(args.output, exists=False)
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use a fresh workspace build directory')
    seal_path = base / 'link-inputs.json'
    seal = sdk.read(seal_path)
    receipt = sdk.read(base / 'receipt.json')
    if not receipt['success'] or not receipt['inputsUnchanged'] or receipt['sourceCopies']:
        raise ValueError('A successful source-in-place full build is required')
    for name in ('receipt', 'sourceInventory', 'log', 'environment', 'compiler', 'linker'):
        sdk.verify(seal[name])
    for row in seal['linkInputs']:
        sdk.verify(row)
    old = {str(sdk.checked(row['path'])).casefold(): row for row in sdk.rows(sdk.read(seal['sourceInventory']['path']))}
    # Execute the actual native ballistic/damage/material implementation from
    # these objects, and require its source to match that build's inventory.
    owners = [ROOT / path for path in (
        'native/guest/cof/server/ballistics.cpp', 'native/guest/cof/scenario_damage.cpp',
        'external/ReGameDLL_CS/regamedll/dlls/cbase.cpp',
        'external/ReGameDLL_CS/regamedll/dlls/weapons.cpp',
        'external/ReGameDLL_CS/regamedll/dlls/util.cpp')]
    for path in owners:
        sdk.verify(old[str(path).casefold()])
    controls = [Path(__file__), ROOT / 'tests/csol-rebuy/run_sdk.py', seal_path]
    controls += [Path(seal[name]['path']) for name in ('receipt', 'sourceInventory', 'log', 'environment', 'compiler', 'linker')]
    fixture = Path(__file__).with_name('main.cpp')
    profile = ROOT / 'native/guest/cof/scenario_damage.v5.json'
    inputs = {str(path).casefold(): sdk.identity(path) for path in controls + owners + [fixture, profile]}
    out.mkdir(); (out / 'temporary').mkdir()
    env = dict(os.environ)
    if env.get('CL') or env.get('_CL_'):
        raise ValueError('Implicit compiler options are not accepted')
    for key, value in sdk.read(seal['environment']['path']).items():
        for previous in list(env):
            if previous.casefold() == key.casefold():
                del env[previous]
        env[key] = value
    env['TEMP'] = env['TMP'] = str(out / 'temporary')
    command = [arg for arg in seal['compileCommand'] if not (
        arg.lower().endswith(('.cpp', '.c')) or arg.lower().startswith(('/yu', '/yc', '/fo', '/fd')) or
        (arg.lower().startswith('/fp') and not arg.lower().startswith('/fp:')))]
    command += ['/Y-', '/I' + str(ROOT), '/I' + str(adapter)]
    depfile = out / 'headers.d'
    # Dependency discovery is preprocessing, so do not retain compile-only /c.
    depcommand = [arg for arg in command if arg.casefold() != '/c']
    depcommand += ['/clang:-M', '/clang:-MF', '/clang:' + str(depfile),
                   '/clang:-MT', '/clang:fixture', str(fixture)]
    record = dict(success=False, sourceCopies=0, fullDLL=False, wholeCurrentTreeRevalidated=False,
                  nativeMaterialFunction='Actual sealed CBaseEntity::FireBullets3 object and hookchain',
                  baselineArtifact=seal['artifact'], supportInputs=seal['linkInputs'],
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  steps=[], headerEquivalences=[], deployment=False, gameplay=False, multiplayer=False, performanceAccepted=False)

    def save():
        record['inputs'] = list(inputs.values())
        (out / 'receipt.json').write_text(json.dumps(record, indent=2) + '\n', 'utf-8')

    def verify():
        for row in list(inputs.values()) + seal['linkInputs']:
            sdk.verify(row)

    def execute(name, job):
        verify(); save(); started = time.monotonic()
        launched = job
        if name != 'behavior':
            response = out / (name + '.rsp')
            response.write_text(subprocess.list2cmdline(job[1:]), 'utf-8')
            launched = [job[0], '@' + str(response)]
        result = subprocess.run(launched, cwd=sdk.SDK_CWD, env=env, capture_output=True,
                                timeout=900 if name == 'link' else 240)
        log = (result.stdout + result.stderr).decode('utf-8', errors='replace')
        (out / (name + '.log')).write_text(log, 'utf-8')
        record['steps'].append(dict(name=name, command=job, exit=result.returncode, seconds=time.monotonic() - started))
        save(); print(name, result.returncode, flush=True)
        if result.returncode or name == 'behavior':
            print(log[-5000:], flush=True)
        if result.returncode:
            raise RuntimeError(name + ' failed')
        return log

    try:
        execute('dependencies', depcommand)
        names = re.findall(r'(?:\\ |[^\s])+', depfile.read_text('utf-8').replace('\\\n', ' ').split(':', 1)[1])
        for name in names:
            path = sdk.checked(sdk.SDK_CWD / name.replace('\\ ', ' '))
            row = sdk.identity(path)
            if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / 'tests') and not path.is_relative_to(ROOT / 'build'):
                expected = old.get(str(path).casefold())
                if not expected or expected['sha256'] != row['sha256']:
                    # Another domain appended one unused free declaration.
                    # Prove exact reversal to the support header; no type,
                    # inline function or other textual difference is allowed.
                    gate = ROOT / 'native/csol/server/scenario/core/game_rules_gate.h'
                    declaration = ('// zs_supply ammo (108ECEB0) sets player +192C around its "primammo" buy;\n'
                                   '// MP114D04A0 CanPlayerBuy then returns true and 1140C880 skips the charge.\n'
                                   'bool CSOL_ScenarioSupplyPurchase(const CBasePlayer* player);\n').encode()
                    raw = path.read_bytes()
                    reverse = raw.replace(declaration, b'')
                    if path != gate or not expected or raw.count(declaration) != 1 or hashlib.sha256(reverse).hexdigest() != expected['sha256']:
                        raise ValueError('Fixture type dependency differs from support build: ' + str(path))
                    record['headerEquivalences'].append(dict(current=row, baseline=expected,
                        removedText=declaration.decode(), reverseSha256=hashlib.sha256(reverse).hexdigest(),
                        reason='One unused non-inline free-function declaration; all existing bytes/types match'))
            inputs[str(path).casefold()] = row
        execute('compile', command + ['/Fo' + str(out / 'fixture.obj'), '/Fd' + str(out / 'fixture.pdb'), str(fixture)])
        remove = ('/out:', '/pdb:', '/implib:', '/def:', '/ltcgout:', '/manifest', '/natvis:',
                  '/subsystem:', '/threads:', '/opt:lldltojobs=', '/lldltocache:')
        link = [arg for arg in seal['linkCommand'] if arg.lower() != '/dll' and not arg.lower().startswith(remove)]
        link += ['/SUBSYSTEM:CONSOLE', '/OUT:' + str(out / 'rifles.exe'), '/PDB:' + str(out / 'rifles.pdb'),
                 '/threads:1', '/opt:lldltojobs=1', str(out / 'fixture.obj')]
        execute('link', link)
        log = execute('behavior', [str(out / 'rifles.exe'), str(profile)])
        counts = re.findall(r'^scenarios=(\d+) behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$', log, re.M)
        if len(counts) != 1 or any(int(value) <= 0 for value in counts[0][:3]) or int(counts[0][3]):
            raise ValueError('Missing successful nonempty material result')
        record['scenarios'], record['behaviorChecks'], record['sdkChecks'], _ = map(int, counts[0])
        record['executable'] = sdk.identity(out / 'rifles.exe')
        verify(); record['inputsUnchanged'] = True; record['success'] = True
    except Exception as error:
        record['error'] = str(error)
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
