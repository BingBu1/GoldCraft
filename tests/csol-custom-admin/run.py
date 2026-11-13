#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build/test actual goldcraft_amxx natives against an isolated MP provider.

Only the common CMake goldcraft_amxx target is built. No game DLL, deployment,
runtime or Git changes; source is compiled in place, never snapshotted.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def plain(path, exists=True):
    path = Path(path).absolute()
    for part in (path, *path.parents):
        if part.is_symlink() or getattr(part, 'is_junction', lambda: False)():
            raise ValueError('Redirected input/output refused: ' + str(part))
    return path.resolve(strict=exists)


def identity(path):
    path = plain(path)
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def arguments(command):
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    values = parse(command, ctypes.byref(count))
    if not values:
        raise ctypes.WinError()
    try:
        return [values[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.LocalFree(values)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common-build', required=True, type=Path)
    parser.add_argument('--environment', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    common = plain(args.common_build)
    out = plain(args.output, exists=False)
    if not common.is_relative_to(ROOT / 'build') or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Use an existing workspace common build and a new build output directory')
    environment = json.loads(plain(args.environment).read_text('utf-8-sig'))
    env = dict(os.environ)
    for name, value in environment.items():
        for prior in list(env):
            if prior.casefold() == name.casefold():
                del env[prior]
        env[name] = value
    database = json.loads((common / 'compile_commands.json').read_text('utf-8'))
    commands = [entry for entry in database if Path(entry['file']).resolve() == ROOT / 'native/amxx/module.cpp']
    if len(commands) != 1:
        raise ValueError('Need the actual goldcraft_amxx module compiler command')
    command = arguments(commands[0]['command'])
    compiler = plain(command[0])
    linker = compiler.with_name('lld-link.exe')
    required = {'--target=i686-pc-windows-msvc', '/clang:-o3', '/clang:-flto=thin', '/arch:avx2', '/fp:precise', '-std:c++20'}
    if not required.issubset({x.lower() for x in command}):
        raise ValueError('Common target lacks required x86/C++20/O3/ThinLTO/AVX2/precise flags')
    inputs = {}

    def track(path):
        row = identity(path)
        key = row['path'].casefold()
        if key in inputs and inputs[key] != row:
            raise ValueError('Input changed while preparing: ' + row['path'])
        inputs[key] = row

    for path in (Path(__file__), ROOT / 'tests/csol-custom-admin/native.cpp', ROOT / 'tests/csol-custom-admin/provider.cpp',
                 ROOT / 'native/amxx/module.cpp', ROOT / 'native/amxx/effect_adapter.cpp',
                 ROOT / 'external/amxmodx/public/sdk/amxxmodule.cpp', ROOT / 'CMakeLists.txt',
                 args.environment, compiler, linker):
        track(path)
    directories = [ROOT / 'native/amxx', ROOT / 'native/include', ROOT / 'external/amxmodx/public',
                   ROOT / 'external/ReGameDLL_CS/regamedll/public']
    directories += [Path(p) for p in environment['INCLUDE'].split(';') if p]
    resource = subprocess.check_output([str(compiler), '-print-resource-dir'], text=True).strip()
    directories.append(Path(resource))
    for directory in directories:
        if directory.is_dir():
            for path in directory.rglob('*'):
                if path.is_file() and (not path.suffix or path.suffix.lower() in ('.h', '.hpp', '.inc', '.inl')):
                    track(path)
    libraries = [Path(d) / name for name in ('libcmt.lib', 'libcpmt.lib', 'libvcruntime.lib', 'libucrt.lib', 'oldnames.lib', 'kernel32.lib')
                 for d in environment['LIB'].split(';') if (Path(d) / name).is_file()]
    for path in libraries:
        track(path)
    out.mkdir()
    receipt = dict(success=False, sourceCopies=0, fullGameDLL=False, deployed=False, liveGameplay=False,
                   inputsBefore=list(inputs.values()), steps=[])

    def save():
        (out / 'receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), 'utf-8')

    def run(name, values, cwd=ROOT):
        values = [str(x) for x in values]
        step = dict(name=name, exit=None, command=values)
        receipt['steps'].append(step)
        save()
        try:
            done = subprocess.run(values, cwd=cwd, env=env, capture_output=True, timeout=240)
        except subprocess.TimeoutExpired as error:
            step['error'] = 'timeout after240 seconds'
            (out / (name + '.log')).write_bytes((error.stdout or b'') + (error.stderr or b''))
            save()
            raise
        (out / (name + '.log')).write_bytes(done.stdout + done.stderr)
        step['exit'] = done.returncode
        text = (done.stdout + done.stderr).decode('utf-8', errors='replace')
        if name.startswith('compile-'):
            dependencies = re.findall(r'Note: including file:\s*([^\r\n]+)', text)
            for path in dependencies:
                if str(plain(path.strip())).casefold() not in inputs:
                    raise ValueError('Unrecorded actual compiler input: ' + path)
            step['dependencies'] = dependencies
        save()
        print(name, done.returncode, text[-6000:], flush=True)
        if done.returncode:
            raise RuntimeError(name + ' failed')

    try:
        run('module-build', ['cmake', '--build', common, '--target', 'goldcraft_amxx', '--parallel', '1', '--verbose'])
        original_module = common / 'goldcraft_amxx.dll'
        before = identity(original_module)
        isolated_module = out / 'goldcraft_amxx.dll'
        shutil.copy2(original_module, isolated_module)
        if identity(original_module) != before or identity(isolated_module)['sha256'] != before['sha256']:
            raise ValueError('Module changed while copying its independently loaded artifact')
        receipt['module'] = identity(isolated_module)
        compile_base = [x for x in command if not (x.lower().endswith(('.cpp', '.c')) or x.lower().startswith(('/fo', '/fd')) or
                        x.lower() in ('-c', '/c', '--', '-dgoldcraft_amxx_exports'))]
        for name in ('native', 'provider'):
            run('compile-' + name, [*compile_base, '/showIncludes', '/c',
                '/Fo' + str(out / (name + '.obj')), ROOT / ('tests/csol-custom-admin/' + name + '.cpp')], common)
        run('link-provider', [linker, '/dll', '/machine:x86', '/threads:1', '/opt:lldltojobs=1', '/out:' + str(out / 'mp.dll'),
                             out / 'provider.obj', *libraries])
        run('link-native', [linker, '/subsystem:console', '/machine:x86', '/threads:1', '/opt:lldltojobs=1', '/out:' + str(out / 'native.exe'),
                           out / 'native.obj', *libraries])
        run('native-tests', [out / 'native.exe', isolated_module, out / 'mp.dll'], out)
        receipt['provider'] = identity(out / 'mp.dll')
        receipt['executable'] = identity(out / 'native.exe')
        receipt['success'] = True
    finally:
        receipt['changedInputs'] = [row['path'] for row in inputs.values() if identity(row['path']) != row]
        receipt['inputsUnchanged'] = not receipt['changedInputs']
        receipt['success'] &= receipt['inputsUnchanged']
        save()
    return 0 if receipt['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
