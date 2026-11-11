#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual SDK writers; compare private original packets and API edges.

Windows Python from Git Bash. Inputs are read in place, never copied. This is
a narrow executable, not a complete server build, deployment or gameplay test.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'external/ReGameDLL_CS/regamedll/msvc'


def no_links(path):
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        if part.is_symlink() or getattr(part, 'is_junction', lambda: False)():
            raise ValueError('Symlink/junction is not permitted: ' + str(part))
    return path


def identity(path):
    path = no_links(path).resolve(strict=True)
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def compiler_flags(log):
    lines = log.read_text('utf-8-sig').splitlines()
    command = next((line.strip() for line in lines if 'clang-cl.exe /c ' in line and
                    re.search(r'scenario_(?:messages|light_zombie)\.cpp(?:\s|$)', line)), None)
    if command is None:
        raise ValueError('Recorded actual scenario_messages/light_zombie compiler command required')
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    argv = parse(command, ctypes.byref(count))
    if not argv:
        raise ctypes.WinError()
    try:
        tokens = [argv[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.LocalFree(argv)
    lower = [token.lower() for token in tokens]
    required = ['/std:c++20', '/fp:precise', '/clang:-o3', '/clang:-flto=thin',
                '--target=i686-pc-windows-msvc']
    arch = [token for token in lower if token.startswith('/arch:')]
    if any(flag not in lower for flag in required) or not arch or arch[-1] != '/arch:avx2':
        raise ValueError('Recorded command lacks required precise x86 C++20/O3/ThinLTO/AVX2 flags')
    if any('ivfsoverlay' in token or token.startswith('@') for token in lower):
        raise ValueError('Indirect source overlays/response files are not supported')
    flags = []
    for token in tokens[1:]:
        low = token.lower()
        if low.endswith(('.cpp', '.c')) or low.startswith(('/fo', '/fd', '/yu', '/yc', '/arch:', '/jmc')):
            continue
        if low.startswith('/fp') and not low.startswith('/fp:'):
            continue
        flags.append(token)
    return tokens, flags + ['/Y-', '/arch:AVX2', '/showIncludes', '/I' + str(ROOT),
                            '/I' + str(ROOT / 'native/csol/server')]


def linker_libraries(text, cwd, library_dirs):
    libraries = {}
    for line in text.splitlines():
        match = re.fullmatch(r'lld-link: Reading (.+?\.lib)(\([^\r\n]*\))?\s*', line, re.I)
        if not match or match[2]:
            continue
        archive = Path(match[1])
        candidates = [archive] if archive.is_absolute() else [d / archive for d in (cwd, *library_dirs)]
        path = next((candidate for candidate in candidates if candidate.is_file()), None)
        if path is None:
            raise ValueError('Cannot resolve actual linked library: ' + str(archive))
        libraries[str(no_links(path).resolve(strict=True))] = str(path.resolve())
    return list(libraries.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('output', 'build-log', 'environment', 'oracle'):
        parser.add_argument('--' + name, required=True, type=Path)
    args = parser.parse_args()
    out = no_links(args.output).resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        parser.error('Use a fresh workspace build output directory')
    proof_path = args.oracle.parent / 'result.json'
    proof = json.loads(proof_path.read_text('utf8'))
    oracle = identity(args.oracle)
    if not proof.get('success') or not proof.get('inputsUnchanged'):
        raise ValueError('Successful unchanged original-instruction fixture required')
    if len(proof.get('cases', [])) != 21 or int(args.oracle.read_text('ascii').splitlines()[0]) != 21:
        raise ValueError('Expected all 21 original event cases')
    rows = ['21']
    for case in proof['cases']:
        fields = [case['event'], case['variant'], int(case['inputs']['created']),
                  case['inputs']['feet'], case['model']]
        fields += [value for center in case['inputs']['centers'] for value in center]
        fields += ([case['packets'][i]['payload'] for i in (0, 1, 3)]
                   if case['packets'] else ['-', '-', '-'])
        rows.append(' '.join(map(str, fields)))
    if args.oracle.read_text('utf8') != '\n'.join(rows) + '\n':
        raise ValueError('Fixture differs from original instruction results')
    original, flags = compiler_flags(args.build_log)
    compiler = no_links(original[0]).resolve(strict=True)
    linker = no_links(compiler.with_name('lld-link.exe')).resolve(strict=True)
    saved = json.loads(args.environment.read_text('utf-8-sig'))
    env = dict(os.environ)
    for key, value in saved.items():
        for existing in list(env):
            if existing.casefold() == key.casefold():
                del env[existing]
        env[key] = value
    env['VSLANG'] = '1033'
    sources = [ROOT / 'native/csol/server/scenario/core/scenario_messages.cpp',
               Path(__file__).with_name('blackhole_api.cpp')]
    controls = [Path(__file__), args.build_log, args.environment, args.oracle, proof_path, compiler, linker]
    watched = {row['path']: row for row in map(identity, [*sources, *controls])}
    for row in proof['inputs']:
        if identity(row['path']) != row:
            raise ValueError('Original evidence changed: ' + row['path'])
        watched[row['path']] = row
    resource = subprocess.check_output([str(compiler), '-print-resource-dir'], env=env, text=True).strip()
    roots = [ROOT / 'native', ROOT / 'external/ReGameDLL_CS', Path(resource)]
    roots += [Path(p) for key in ('INCLUDE', 'LIB') for p in saved.get(key, '').split(';') if p]
    roots += [Path(token[2:]) for token in flags if token.startswith('/I') and len(token) > 2
              and Path(token[2:]).resolve() != ROOT]
    for folder in roots:
        if not folder.is_dir():
            continue
        for current, children, names in os.walk(no_links(folder), followlinks=False):
            for child in children:
                no_links(Path(current) / child)
            for name in names:
                path = no_links(Path(current) / name)
                if path.suffix.lower() in ('', '.h', '.hpp', '.hxx', '.inl', '.inc', '.tcc', '.lib'):
                    key = str(path.resolve())
                    if key not in watched:
                        watched[key] = identity(path)
    known = {key.casefold(): row for key, row in watched.items()}
    actual = {str(path.resolve()) for path in [*sources, *controls]}
    actual.update(row['path'] for row in proof['inputs'])
    out.mkdir(); (out / 'temp').mkdir()
    env['TEMP'] = env['TMP'] = str(out / 'temp')
    result = dict(success=False, sourceCopies=0, fullDLL=False, deployed=False, liveGameplay=False,
                  revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  originalEvents=21, steps=[])
    (out / 'inputs-before.json').write_text(json.dumps(list(watched.values()), indent=2), 'utf8')
    exe = out / 'blackhole-api.exe'
    commands = [(source.stem, [str(compiler), *flags, str(source),
                              '/Fo' + str(out / (source.stem + '.obj'))]) for source in sources]
    commands += [('link', [str(linker), '/NOLOGO', '/MACHINE:X86', '/SUBSYSTEM:CONSOLE', '/OPT:REF',
                           '/OPT:ICF', '/INCREMENTAL:NO', '/threads:1', '/opt:lldltojobs=1', '/verbose',
                           '/OUT:' + str(exe), *[str(out / (s.stem + '.obj')) for s in sources]]),
                 ('api', [str(exe)]), ('original', [str(exe), str(args.oracle.resolve())])]
    lines = args.oracle.read_text('ascii').splitlines()
    for name, data in [('empty', ''), ('truncated', '\n'.join(lines[:-1]) + '\n'),
                       ('partial', '21\n1 0\n'), ('extra', '\n'.join(lines) + '\nextra\n')]:
        fixture = out / (name + '.txt'); fixture.write_text(data, 'ascii')
        commands.append(('reject-' + name, [str(exe), str(fixture)]))
    try:
        for name, command in commands:
            started = time.monotonic(); invocation = command
            if name in ('scenario_messages', 'blackhole_api', 'link'):
                rsp = out / (name + '.rsp'); rsp.write_text(subprocess.list2cmdline(command[1:]), 'utf8')
                invocation = [command[0], '@' + str(rsp)]
            done = subprocess.run(invocation, cwd=SDK, env=env, capture_output=True, timeout=240)
            log = out / (name + '.log'); log.write_bytes(done.stdout + done.stderr)
            text = log.read_text('utf-8-sig', errors='replace')
            dependencies = []
            if name in ('scenario_messages', 'blackhole_api'):
                dependencies = re.findall(r'Note: including file:\s*([^\r\n]+)', text)
            elif name == 'link':
                dependencies = linker_libraries(text, SDK, [Path(p) for p in saved.get('LIB', '').split(';') if p])
            if name in ('scenario_messages', 'blackhole_api', 'link') and done.returncode == 0 and not dependencies:
                raise ValueError('Missing actual compiler/linker dependency report')
            for dependency in dependencies:
                path = str((SDK / dependency.strip()).resolve())
                if path.casefold() not in known:
                    raise ValueError('Unrecorded dependency: ' + path)
                actual.add(known[path.casefold()]['path'])
            rejection = name.startswith('reject-')
            passed = done.returncode == (1 if rejection else 0)
            if rejection:
                passed &= text.startswith('ERROR ')
            result['steps'].append(dict(name=name, command=command, exit=done.returncode, passed=passed,
                                        seconds=time.monotonic() - started, log=identity(log)))
            print(name, done.returncode, text[-1500:] if name not in ('scenario_messages', 'blackhole_api', 'link') else '', flush=True)
            if not passed:
                raise ValueError('Failed step: ' + name)
        result['success'] = True
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['actualDependencies'] = [watched[path] for path in sorted(actual)]
        result['changedInputs'] = [path for path in sorted(actual) if identity(path) != watched[path]]
        result['inputsUnchanged'] = not result['changedInputs']
        result['success'] &= result['inputsUnchanged']
        (out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', 'utf8')
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
