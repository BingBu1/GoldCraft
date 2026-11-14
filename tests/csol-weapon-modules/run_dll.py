#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the exact linked client's CRT weapon registry using its matching PDB."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve(strict=True)
    with path.open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha)


def split(text):
    count = ctypes.c_int()
    shell = ctypes.windll.shell32
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    args = shell.CommandLineToArgvW(text, ctypes.byref(count))
    if not args:
        raise ctypes.WinError()
    try:
        return [args[i] for i in range(count.value)]
    finally:
        release = ctypes.windll.kernel32.LocalFree
        release.argtypes = [ctypes.c_void_p]
        release(args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('dll', 'pdb', 'client-build', 'environment', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    out, build = args.output.resolve(), args.client_build.resolve(strict=True)
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build receipt directory')
    for path in (args.dll, args.pdb, build, args.environment):
        if not path.resolve(strict=True).is_relative_to(ROOT):
            raise ValueError('Only workspace candidates can be loaded')
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text('utf-8-sig')).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    inspector = ROOT / 'tools/Inspect-MetaHookRuntime.py'
    spec = importlib.util.spec_from_file_location('registry_symbols', inspector)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    pe = module.PE(args.dll)
    symbols = module.Symbols(args.pdb, pe)
    public = symbols.dump('-publics')
    matches = re.findall(r'S_PUB32[^\n]*`(\?registry@\?1\?\?Modules@[^\n]+)`\s*\n'
                         r'[^\n]*addr = (\d+):(\d+)', public)
    if len(matches) != 1:
        raise ValueError('Expected one exact client weapon registry symbol')
    _, section, offset = matches[0]
    rva = pe.sections[int(section) - 1]['rva'] + int(offset)
    registry = symbols.layout("csol::client::`anonymous namespace'::Registry")
    descriptor = symbols.layout('csol::client::WeaponModule')
    layout = [rva, *(registry['members'][key] for key in ('ordered', 'count', 'invalid')),
              registry['size'], descriptor['size']]
    cache = (build / 'CMakeCache.txt').read_text('utf-8-sig')
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M)[1].strip()
    database = json.loads(subprocess.check_output([ninja, '-C', str(build), '-t', 'compdb'], env=env))
    row = next(row for row in database if row['file'].replace('\\', '/').endswith('/client/weapons/weapon_module.cpp'))
    original, flags, skip = split(row['command']), [], False
    for value in original[1:]:
        if skip:
            skip = False
            continue
        if value.lower() in ('-o', '-mf', '-mt'):
            skip = True
            continue
        if value in ('/c', '-c', '-MD', '--') or value.lower().endswith('.cpp') or value.lower().startswith(('/fo', '/fd')):
            continue
        flags.append(value)
    source = ROOT / 'tests/csol-weapon-modules/dll_registry.cpp'
    paths = [args.dll, args.pdb, args.environment, source, Path(__file__), inspector,
             Path(original[0]), Path(symbols.tool), build / 'CMakeCache.txt', build / 'build.ninja']
    before = [identity(path) for path in paths]
    out.mkdir(parents=True)
    steps, dependencies, success = [], {}, False

    def run(name, command):
        result = subprocess.run(list(map(str, command)), cwd=build, env=env,
                                capture_output=True, timeout=120)
        data = result.stdout + result.stderr
        (out / (name + '.log')).write_bytes(data)
        for path in re.findall(r'Note: including file:\s*([^\r\n]+)', data.decode('utf8', 'replace')):
            path = Path(path.strip()).resolve(strict=True)
            dependencies[str(path)] = identity(path)
        steps.append(dict(name=name, command=list(map(str, command)), exit=result.returncode))
        print(name, result.returncode, flush=True)
        if result.returncode:
            print(data.decode('utf8', 'replace')[-3000:])
            raise RuntimeError(name)

    try:
        obj, exe = out / 'registry.obj', out / 'registry.exe'
        run('compile', [original[0], *flags, '/showIncludes', '/c', '/Fo' + str(obj), source])
        run('link', [original[0], '/nologo', '--target=i686-pc-windows-msvc', '-fuse-ld=lld',
                     '/MT', obj, '/Fe' + str(exe), '/link', '/machine:x86', '/threads:1', '/OPT:REF'])
        run('registry', [exe, args.dll.resolve(), *map(str, layout)])
        success = True
    finally:
        stable = before == [identity(path) for path in paths] and all(identity(path) == row for path, row in dependencies.items())
        receipt = dict(success=success and stable, inputsUnchanged=stable, inputs=before,
                       dependencies=list(dependencies.values()), pdb=symbols.identity, layout=layout, steps=steps,
                       boundary='Exact DLL CRT registration, unload/reload and matched SDK/PDB layout; no engine callbacks or gameplay')
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    if not receipt['success']:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
