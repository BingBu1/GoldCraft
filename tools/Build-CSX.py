#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the pinned CSX sources in place and test the same objects over the SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = '8fc12670ea86470a0bfed4c35db5de73d3a39e8a'
METAMOD_PIN = '18a10db686702e8ae9e0fcb5b5febf8881dc9c2d'
HLSDK_PIN = 'a0edb7792a96998d349325bebab8ea41ec5cb239'


def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.file_digest(path.open('rb'), 'sha256').hexdigest())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--resource-compiler', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build'):
        raise ValueError('Use fresh workspace compiler output')
    upstream = ROOT/'external/amxmodx'
    if subprocess.check_output(['git', '-C', str(upstream), 'rev-parse', 'HEAD']).decode().strip() != PIN:
        raise ValueError('Unreviewed AMXX source identity')
    metamod = ROOT/'external/metamod-amxx-sdk'
    if subprocess.check_output(['git', '-C', str(metamod), 'rev-parse', 'HEAD']).decode().strip() != METAMOD_PIN:
        raise ValueError('Unreviewed Metamod SDK identity')
    sdk = ROOT/'external/hlsdk-amxx-sdk'
    if subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD']).decode().strip() != HLSDK_PIN:
        raise ValueError('Unreviewed Half-Life SDK identity')
    out.mkdir(parents=True)
    env = dict(os.environ)
    for key, value in json.loads(a.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    module = upstream/'modules/cstrike/csx'
    includes = [module, upstream/'public', upstream/'public/sdk', upstream/'public/amtl',
                upstream/'third_party', upstream/'third_party/hashing', metamod/'metamod']
    includes += [sdk/d for d in ('common', 'engine', 'dlls', 'pm_shared', 'public')]
    sources = [module/(n+'.cpp') for n in ('CMisc', 'CRank', 'meta_api', 'rank', 'usermsg')]
    sources += [upstream/'public/sdk/amxxmodule.cpp']
    fixture = ROOT/'tests/csx-npc-owner/main.cpp'
    tracked = {}
    for path in [*sources, fixture, Path(__file__), a.environment, a.compiler, a.resource_compiler, module/'version.rc']:
        row = identity(path)
        tracked[row['path']] = row
    report = dict(success=False, upstream=PIN, metamodSdk=METAMOD_PIN, hlsdk=HLSDK_PIN, steps=[], sourceCopies=0, deployed=False)

    def save():
        (out/'receipt.json').write_text(json.dumps(report, indent=2)+'\n', 'utf-8')

    def run(label, command):
        r = subprocess.run([str(x) for x in command], cwd=out, env=env, capture_output=True, timeout=120)
        text = (r.stdout+r.stderr).decode('utf-8', errors='replace')
        (out/(label+'.log')).write_text(text, 'utf-8')
        report['steps'].append(dict(name=label, exit=r.returncode, command=list(map(str, command))))
        for name in re.findall(r'Note: including file:\s*([^\r\n]+)', text):
            row = identity(name.strip())
            tracked.setdefault(row['path'], row)
        save()
        print(label, r.returncode, text[-5000:] if r.returncode or label == 'test' else '', flush=True)
        if r.returncode:
            raise RuntimeError(label+' failed')

    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/MT', '/EHsc', '/utf-8',
             '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/Zi', '/Gy',
             '/DWIN32', '/D_WINDOWS', '/D_USRDLL', '/DNDEBUG', '/DJIT', '/DHAVE_STDINT_H',
             '/D_CRT_SECURE_NO_WARNINGS', '/D_HAS_AUTO_PTR_ETC=1', '/showIncludes']
    flags += ['/I'+str(path) for path in includes]
    try:
        objects = []
        for i, source in enumerate([*sources, fixture]):
            obj = out/(str(i)+'-'+source.stem+'.obj')
            run('compile-'+source.stem, [a.compiler, *flags, '/c', source, '/Fo'+str(obj)])
            if source != fixture:
                objects.append(obj)
            else:
                fixture_object = obj
        rc = a.resource_compiler.resolve(strict=True)
        run('resource', [rc, '/nologo', '/I'+str(upstream/'public'), '/fo'+str(out/'csx.res'), module/'version.rc'])
        shared = [a.compiler, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT', *objects]
        run('link-dll', [*shared, out/'csx.res', '/LD', '/Fe'+str(out/'csx_amxx.dll'), '/link', '/DEBUG',
                        '/PDB:'+str(out/'csx_amxx.pdb'), '/threads:1', '/opt:lldltojobs=1',
                        '/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8,@1', '/SECTION:.data,RW'])
        run('link-test', [*shared, fixture_object, '/Fe'+str(out/'csx-owner-test.exe'), '/link', '/DEBUG',
                         '/PDB:'+str(out/'csx-owner-test.pdb'), '/threads:1', '/opt:lldltojobs=1'])
        run('test', [out/'csx-owner-test.exe'])
        report['artifact'] = identity(out/'csx_amxx.dll')
        report['success'] = True
    finally:
        report['inputs'] = list(tracked.values())
        report['changedInputs'] = [r['path'] for r in tracked.values() if identity(r['path']) != r]
        report['inputsUnchanged'] = not report['changedInputs']
        report['success'] &= report['inputsUnchanged']
        save()
    if not report['success']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
