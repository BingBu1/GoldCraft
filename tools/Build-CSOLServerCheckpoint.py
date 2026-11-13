#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build current in-place server sources and seal their before/after identities."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent


def read_identity(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return dict(path=str(path), bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(), nlink=path.stat().st_nlink)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--compiler-dir', type=Path, required=True)
    parser.add_argument('--msbuild', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4, choices=range(1, 17))
    args = parser.parse_args()
    build, out = args.build.resolve(strict=True), args.output.resolve()
    if not build.is_relative_to(ROOT / 'build') or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use an existing workspace build and fresh workspace receipt directory')
    out.mkdir(parents=True)
    spec = importlib.util.spec_from_file_location('candidate_guard', ROOT / 'tools/Build-CSOLCandidate.py')
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    baseline = out / 'sources'
    guard.begin(baseline)
    environment = args.environment.resolve(strict=True)
    env = dict(os.environ)
    for key, value in json.loads(environment.read_text('utf-8-sig')).items():
        for previous in list(env):
            if previous.casefold() == key.casefold():
                del env[previous]
        env[key] = value
    compiler = str(args.compiler_dir.resolve(strict=True)) + '/'
    msbuild = args.msbuild.resolve(strict=True)
    artifact = build / 'regamedll/mp.dll'
    if artifact.exists() and guard.identity(artifact)['nlink'] != 1:
        raise ValueError('Compiler output is a shared hard link')
    command = [str(msbuild),
        str(ROOT / 'external/ReGameDLL_CS/msvc/ReGameDLL.sln'), '/m:' + str(args.jobs), '/nologo', '/v:normal',
        '/p:Configuration=Release', '/p:Platform=Win32', '/p:PlatformToolset=v145',
        '/p:PreferredToolArchitecture=x64', '/p:WindowsTargetPlatformVersion=10.0.26100.0',
        '/p:VcpkgEnabled=false', '/p:CLToolPath=' + compiler, '/p:CLToolExe=clang-cl.exe',
        '/p:LinkToolPath=' + compiler, '/p:LinkToolExe=lld-link.exe',
        '/p:LibToolPath=' + compiler, '/p:LibToolExe=llvm-lib.exe', '/p:WholeProgramOptimization=false',
        '/p:ForceImportAfterCppTargets=' + str(build / 'Build-Isolated.targets'),
        '/p:PreBuildEventUseInBuild=false', '/p:PostBuildEventUseInBuild=false',
        '/p:MultiProcessorCompilation=false', '/p:UseMultiToolTask=true',
        '/p:EnforceProcessCountAcrossBuilds=true', '/p:MultiProcMaxCount=' + str(args.jobs),
        '/p:CL_MPCount=' + str(args.jobs),
        '/p:GoldCraftRoot=' + str(ROOT),
        '/p:ForceImportBeforeCppTargets=' + str(ROOT / 'native/server/GoldCraft.targets'),
        '/p:GoldCraftCommonLibrary=' + str(build / 'common/goldcraft_common.lib'),
        '/p:GoldCraftCsolWeaponTests=false', '/p:OutDir=' + str(build / 'regamedll') + '/']
    controls = [read_identity(path) for path in (Path(__file__), ROOT / 'tools/Build-CSOLCandidate.py',
                                                 ROOT / 'tests/csol-rebuy/run_sdk.py',
                                                 environment, build / 'Build-Isolated.targets', msbuild,
                                                 Path(compiler) / 'clang-cl.exe', Path(compiler) / 'lld-link.exe',
                                                 Path(compiler) / 'llvm-lib.exe')]
    started, steps = time.monotonic(), []
    for name, job in [('common', ['cmake', '--build', str(build / 'common'), '--target', 'goldcraft_common', '--parallel', '1']),
                      ('msbuild', command)]:
        print('BUILD STEP ' + name, flush=True)
        with (out / (name + '.log')).open('w', encoding='utf8') as log:
            result = subprocess.run(job, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        steps.append(dict(name=name, command=job, exit=result.returncode))
        if result.returncode:
            break
    unchanged = True
    try:
        guard.check_sources(baseline)
        if any(read_identity(Path(row['path'])) != row for row in controls):
            raise ValueError('Build controls changed during compilation')
    except Exception as error:
        unchanged = False
        (out / 'source-drift.txt').write_text(str(error), 'utf8')
    linked = len(steps) == 2 and steps[-1]['exit'] == 0
    receipt = dict(steps=steps, inputsUnchanged=unchanged, controls=controls, sourceCopies=0,
        success=linked and all(row['exit'] == 0 for row in steps) and unchanged,
        revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        elapsedSeconds=time.monotonic() - started,
        validation='Full current-source x86 server link; no deployment or gameplay',
        artifact=guard.identity(artifact) if linked and artifact.is_file() else None)
    receipt_path = out / 'receipt.json'
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    if receipt['success']:
        # Seal the real output objects immediately after this unchanged full
        # build. A later incremental build must not reseal overwritten objects
        # and pretend that they belong to this baseline.
        sdk_spec = importlib.util.spec_from_file_location('checkpoint_sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
        sdk = importlib.util.module_from_spec(sdk_spec)
        sdk_spec.loader.exec_module(sdk)
        compile_command, link_command = sdk.commands(out / 'msbuild.log')
        link_inputs = sdk.resolve_link_inputs(link_command, json.loads(environment.read_text('utf-8-sig')))
        guard.check_sources(baseline)
        for row in controls + link_inputs:
            sdk.verify(row)
        sdk.verify(receipt['artifact'])
        seal = dict(receipt=sdk.identity(receipt_path), sourceInventory=sdk.identity(baseline / 'source-inputs.json'),
                    log=sdk.identity(out / 'msbuild.log'), logEncoding=sdk.msbuild_text(out / 'msbuild.log')[1],
                    environment=sdk.identity(environment), artifact=sdk.identity(artifact),
                    compiler=sdk.identity(compile_command[0]), linker=sdk.identity(link_command[0]),
                    compileCommand=compile_command, linkCommand=link_command, linkInputs=link_inputs)
        (out / 'link-inputs.json').write_text(json.dumps(seal, indent=2) + '\n', 'utf8')
    print(json.dumps(receipt))
    return 0 if receipt['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
