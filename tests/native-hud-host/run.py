#!/usr/bin/env python3
"""Freeze and run the production native HUD Host with the matching x86 SDK."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
IDENTITIES = {
    'client': 'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2',
    'engine': '9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295',
}
SOURCES = [
    'native/client/resource_native_hud_host.cpp', 'native/client/resource_native_hud_host.hpp',
    'native/client/resource_native_hud.cpp', 'native/client/resource_native_hud.hpp',
    'tests/native/resource_native_hud_host_tests.cpp', 'tests/csol-wall-status/surface_stub.hpp',
    'tests/native-hud-host/run.py', 'tests/native-hud-host/README.md',
]
INCLUDES = [
    'MetaHook/include', 'MetaHook/include/Interface', 'MetaHook/include/VGUI',
    'MetaHook/include/Interface/VGUI', 'MetaHook/include/vgui_controls',
    'MetaHook/include/SourceSDK/vstdlib', 'MetaHook/include/HLSDK/cl_dll',
    'MetaHook/include/HLSDK/common', 'MetaHook/include/HLSDK/engine',
    'MetaHook/include/HLSDK/pm_shared', 'MetaHook/include/HLSDK/public',
    'MetaHook/include/HLSDK/dlls', 'MetaHook/include/SourceSDK',
    'MetaHook/include/SourceSDK/tier0', 'MetaHook/include/SourceSDK/tier1',
    'MetaHook/thirdparty/rapidjson/include', 'thirdparty/glew_fork/include',
    'Plugins/VGUI2Extension/include', 'Plugins/VGUI2Extension/include/Interface',
    'Plugins/Renderer/include/Interface',
]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identity(path):
    path = Path(path)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': sha(path)}


def map_pe(source, target, expected):
    raw = source.read_bytes()
    if hashlib.sha256(raw).hexdigest() != expected:
        raise ValueError(f'{source.name}: original SHA-256 mismatch')
    pe = struct.unpack_from('<I', raw, 0x3c)[0]
    machine, sections, _, _, _, optional_size, _ = struct.unpack_from('<HHIIIHH', raw, pe + 4)
    optional = pe + 24
    if raw[pe:pe + 4] != b'PE\0\0' or machine != 0x14c or struct.unpack_from('<H', raw, optional)[0] != 0x10b:
        raise ValueError('Expected matching PE32 x86 original')
    size, headers = struct.unpack_from('<II', raw, optional + 56)
    if headers > len(raw) or headers > size:
        raise ValueError('Invalid PE header range')
    image = bytearray(size)
    image[:headers] = raw[:headers]
    for number in range(sections):
        section = optional + optional_size + number * 40
        _, address, count, offset = struct.unpack_from('<IIII', raw, section + 8)
        if offset + count > len(raw) or address + count > size:
            raise ValueError('Invalid PE section range')
        image[address:address + count] = raw[offset:offset + count]
    target.write_bytes(image)
    return {'original': identity(source), 'mapped': identity(target),
            'preferredBase': struct.unpack_from('<I', raw, optional + 28)[0],
            'runtimeRelocation': 'Fixture applies IMAGE_REL_BASED_HIGHLOW at its actual VirtualAlloc base'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ['output', 'compiler', 'sdk', 'client', 'engine']:
        parser.add_argument('--' + option, type=Path, required=True)
    parser.add_argument('--environment', type=Path, help='Optional configured Windows toolchain environment JSON')
    args = parser.parse_args()
    out = args.output.resolve()
    out.relative_to(ROOT)
    args.sdk = args.sdk.resolve(strict=True)
    args.sdk.relative_to(ROOT)
    for name in ['client', 'engine']:
        path = getattr(args, name).resolve(strict=True)
        path.relative_to(ROOT)  # Only workspace copies; never map the protected installation.
        setattr(args, name, path)
    compiler = args.compiler.resolve(strict=True)
    env = dict(os.environ)
    if args.environment:
        env.update(json.loads(args.environment.read_text(encoding='utf-8-sig')))
    env['VSLANG'] = '1033'
    out.mkdir(parents=True, exist_ok=False)
    (out / 'temp').mkdir()
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    result = {'success': False, 'sources': [], 'sdkInputs': [], 'steps': [], 'outputs': [],
              'compiler': identity(compiler), 'productionModified': False,
              'boundary': 'Actual production Host and matching x86 SDK. Relocated original getter/free thunks only; no game export, lifecycle events, full DLL, game/UI or v4 activation.'}
    def save():
        (out / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    def execute(name, command):
        started = time.monotonic()
        completed = subprocess.run([str(x) for x in command], cwd=out, env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        log = out / (name + '.log'); log.write_bytes(completed.stdout)
        result['steps'].append({'name': name, 'argv': [str(x) for x in command],
            'exit': completed.returncode, 'seconds': time.monotonic() - started, 'log': identity(log)})
        save()
        decoded = completed.stdout.decode('utf-8', errors='replace')
        print(name, completed.returncode, flush=True)
        if completed.returncode:
            print(decoded[-6000:], flush=True)
            raise RuntimeError(name + ' failed')
        return decoded
    try:
        for relative in SOURCES:
            source, target = ROOT / relative, out / 'source' / relative
            target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(source.read_bytes())
            result['sources'].append({'original': identity(source), 'frozen': identity(target)})
        result['images'] = {name: map_pe(getattr(args, name), out / (name + '-mapped.bin'), expected)
                            for name, expected in IDENTITIES.items()}
        flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/MT', '/EHsc',
                 '/O2', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise',
                 '/utf-8', '/Gy', '/Gw', '/DWIN32', '/DNOMINMAX', '/DNO_MALLOC_OVERRIDE',
                 '/DGLEW_STATIC', '/DGLEW_NO_GLU', '/D_CRT_SECURE_NO_WARNINGS', '/showIncludes']
        units = ['native/client/resource_native_hud_host.cpp', 'native/client/resource_native_hud.cpp',
                 'tests/native/resource_native_hud_host_tests.cpp']
        discovered = set()
        pattern = re.compile(r'including file:\s*(.+)$', re.IGNORECASE)
        # Serial syntax scans identify the actual header closure; compile only
        # from frozen workspace headers, with external toolchain headers hashed.
        for relative in units:
            text = execute('discover-' + Path(relative).stem, [compiler, *flags,
                *['/I' + str(args.sdk / x) for x in INCLUDES], '/Zs', out / 'source' / relative])
            for line in text.splitlines():
                match = pattern.search(line)
                if match: discovered.add(Path(match.group(1).strip()).resolve(strict=True))
        if not discovered: raise RuntimeError('No actual SDK include evidence')
        for path in sorted(discovered):
            if path.is_relative_to(out): continue
            row = {'original': identity(path)}
            if path.is_relative_to(ROOT):
                target = out / 'source' / path.relative_to(ROOT)
                target.parent.mkdir(parents=True, exist_ok=True); target.write_bytes(path.read_bytes())
                row['frozen'] = identity(target)
            result['sdkInputs'].append(row)
        sdk_frozen = out / 'source' / args.sdk.relative_to(ROOT)
        frozen_paths = {Path(row.get('frozen', row['original'])['path']).resolve() for row in result['sdkInputs']}
        source_paths = {Path(row['frozen']['path']).resolve() for row in result['sources']}
        objects = []
        for relative in units:
            obj = out / (Path(relative).stem + '.obj')
            text = execute('compile-' + Path(relative).stem, [compiler, *flags,
                *['/I' + str(sdk_frozen / x) for x in INCLUDES], '/Fo' + str(obj), '/c', out / 'source' / relative])
            actual = {Path(match.group(1).strip()).resolve(strict=True) for line in text.splitlines()
                      if (match := pattern.search(line))}
            if not actual or not actual.issubset(frozen_paths | source_paths):
                raise RuntimeError('Compilation used an unrecorded include: ' + str(actual - frozen_paths - source_paths))
            result['outputs'].append(identity(obj)); objects.append(obj)
        exe = out / 'native-hud-host-tests.exe'
        execute('link', [compiler, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT', *objects,
                        '/Fe' + str(exe), '/link', '/threads:1', '/OPT:REF', '/OPT:ICF'])
        result['outputs'].append(identity(exe))
        execute('test', [exe, out / 'client-mapped.bin', out / 'engine-mapped.bin'])
        for row in result['sources'] + result['sdkInputs']:
            for key in ['original', 'frozen']:
                if key in row and sha(row[key]['path']) != row[key]['sha256']:
                    raise RuntimeError('Input changed: ' + row[key]['path'])
        result['inputsUnchanged'] = True
        result['success'] = True
    except Exception as failure:
        result['error'] = str(failure)
        raise
    finally:
        save()
    print(json.dumps({'result': identity(out / 'result.json'), 'test': (out / 'test.log').read_text().strip()}, ensure_ascii=False))


if __name__ == '__main__':
    main()
