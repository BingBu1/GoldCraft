#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile an in-place rebuy fixture against sealed current server SDK objects.

Seal immediately after a successful full server build, then run once the
evidence-grounded fixture exists. No production source copies or VFS overlays.
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
SDK_CWD = ROOT / 'external/ReGameDLL_CS/regamedll/msvc'


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def checked(path, exists=True):
    path = Path(os.path.abspath(path))
    for parent in (path, *path.parents):
        if parent.is_symlink() or getattr(parent, 'is_junction', lambda: False)():
            raise ValueError('Symlink/junction is not an input: ' + str(parent))
    return path.resolve(strict=exists)


def identity(path):
    path = checked(path)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest(path))


def rows(document):
    if isinstance(document, list):
        return document
    for name in ('files', 'inputs', 'inputsBefore'):
        if isinstance(document.get(name), list):
            return document[name]
    raise ValueError('Expected source identity rows, not an inferred source tree')


def verify(row):
    path = Path(row['path'])
    if not path.is_absolute():
        path = ROOT / path
    path = checked(path)
    if digest(path) != row['sha256']:
        raise ValueError('Recorded input changed: ' + str(path))
    return path


def windows_arguments(command):
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    argv = parse(command.strip(), ctypes.byref(count))
    if not argv:
        raise ctypes.WinError()
    try:
        return [argv[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.LocalFree(argv)


def msbuild_text(log):
    raw = checked(log).read_bytes()
    encodings = ('utf-8-sig',) if raw.startswith(b'\xef\xbb\xbf') else ('utf-8', 'cp936')
    for encoding in encodings:
        try:
            text = raw.decode(encoding, errors='strict')
        except UnicodeDecodeError:
            continue
        if text.encode(encoding, errors='strict') == raw:
            return text, encoding
    raise ValueError('MSBuild log requires one reversible UTF-8 or CP936 encoding; raw bytes are preserved')


def commands(log):
    lines = msbuild_text(log)[0].splitlines()
    templates = [line for line in lines if 'clang-cl.exe /c ' in line and
                 re.search(r'(?:scenario_light_zombie|player)\.cpp(?:["\s]|$)|'
                           r'(?:^|[\\/])native[\\/]csol[\\/]server[\\/]'
                           r'[^"\r\n]+\.cpp(?:["\s]|$)|'
                           r'(?:^|[\\/])native[\\/]guest[\\/]cof[\\/]'
                           r'[^"\r\n]+\.cpp(?:["\s]|$)',
                           line, re.I)]
    starts = [i for i, line in enumerate(lines) if 'lld-link.exe /ERRORREPORT:' in line]
    if not templates or len(starts) != 1:
        raise ValueError('Need one complete server link and a real SDK compiler command in this build log')
    start = starts[0]
    parts = [lines[start].strip()]
    for line in lines[start + 1:]:
        if not re.fullmatch(r'\s+"[^"\r\n]+\.(?:obj|lib|res)"\s*', line, re.I):
            break
        parts.append(line.strip())
    compile_cmd = windows_arguments(templates[-1])
    link_cmd = windows_arguments(' '.join(parts))
    required = {'/std:c++20', '/fp:precise', '/clang:-o3', '/clang:-flto=thin',
                '/arch:avx2', '--target=i686-pc-windows-msvc'}
    if not required.issubset({x.lower() for x in compile_cmd}):
        raise ValueError('Compiler command lacks the required x86/C++20/O3/ThinLTO/AVX2/precise flags')
    if [x.lower() for x in compile_cmd if x.lower().startswith('/arch:')][-1] != '/arch:avx2':
        raise ValueError('Effective compiler architecture is not AVX2')
    for token in compile_cmd + link_cmd:
        low = token.lower()
        if token.startswith('@') or 'ivfsoverlay' in low or low.startswith('/force'):
            raise ValueError('Indirect/rewritten/forced command is not accepted: ' + token)
    if '/dll' not in [x.lower() for x in link_cmd]:
        raise ValueError('Recorded link is not the full server DLL')
    if '/machine:x86' not in [x.lower() for x in link_cmd]:
        raise ValueError('Recorded server link must target x86')
    return compile_cmd, link_cmd


def resolve_link_inputs(link_cmd, environment):
    dirs = [SDK_CWD]
    dirs += [Path(x[9:]) for x in link_cmd if x.lower().startswith('/libpath:')]
    dirs += [Path(x) for x in environment.get('LIB', '').split(';') if x]
    result = {}
    for token in link_cmd[1:]:
        if token.startswith('/') or Path(token).suffix.lower() not in ('.obj', '.lib', '.res'):
            continue
        path = Path(token)
        candidates = [path] if path.is_absolute() else [base / path for base in dirs]
        found = next((checked(p) for p in candidates if p.is_file()), None)
        if found is None:
            raise ValueError('Unresolved production link input: ' + token)
        result[str(found).casefold()] = identity(found)
    if not any(Path(x['path']).suffix.lower() == '.obj' for x in result.values()):
        raise ValueError('No actual production objects in recorded link')
    return sorted(result.values(), key=lambda x: x['path'].casefold())


def build_provenance(args):
    receipt_path = checked(args.receipt)
    source_path = checked(args.source_inputs or receipt_path.parent / 'sources/source-inputs.json')
    receipt = read(receipt_path)
    if not receipt.get('success') or not receipt.get('inputsUnchanged'):
        raise ValueError('A successful unchanged full server build receipt is required')
    steps = receipt.get('steps', [])
    if not steps or any(step.get('exit') != 0 for step in steps):
        raise ValueError('Build receipt contains incomplete or failed steps')
    if receipt.get('sourceCopies') != 0:
        raise ValueError('Build must explicitly record sourceCopies=0')
    artifact = receipt.get('artifact')
    if not artifact or Path(artifact['path']).name.lower() != 'mp.dll':
        raise ValueError('Missing full server mp.dll identity')
    verify(artifact)
    sources = rows(read(source_path))
    if not sources:
        raise ValueError('Empty production source inventory')
    for row in sources:
        verify(row)
    compile_cmd, link_cmd = commands(args.msbuild_log)
    out_flags = [x[5:] for x in link_cmd if x.lower().startswith('/out:')]
    if len(out_flags) != 1 or checked(out_flags[0]) != checked(artifact['path']):
        raise ValueError('Recorded link output differs from the accepted server artifact')
    environment = read(args.environment)
    current = dict(receipt=identity(receipt_path), sourceInventory=identity(source_path),
                   log=identity(args.msbuild_log), logEncoding=msbuild_text(args.msbuild_log)[1],
                   environment=identity(args.environment),
                   artifact=identity(artifact['path']), compiler=identity(compile_cmd[0]),
                   linker=identity(link_cmd[0]), compileCommand=compile_cmd, linkCommand=link_cmd,
                   linkInputs=resolve_link_inputs(link_cmd, environment))
    return current, sources, environment


def run(args):
    current, sources, environment = build_provenance(args)
    seal_path = checked(args.link_inputs, exists=False)
    if not seal_path.is_relative_to(ROOT / 'build'):
        raise ValueError('Build identity seal must stay under workspace build')
    if args.seal_only:
        if seal_path.exists():
            raise ValueError('Never overwrite a build identity seal')
        for row in sources + current['linkInputs']:
            verify(row)
        for name in ('receipt', 'sourceInventory', 'log', 'environment', 'artifact', 'compiler', 'linker'):
            verify(current[name])
        seal_path.parent.mkdir(parents=True, exist_ok=True)
        seal_path.write_text(json.dumps(current, indent=2) + '\n', encoding='utf8')
        print(json.dumps(dict(sealed=str(seal_path), linkInputs=len(current['linkInputs']), compiled=False)))
        return 0
    if read(seal_path) != current:
        raise ValueError('Build command/source/artifact/object identities differ from the post-build seal')
    fixture = checked(args.fixture)
    if not fixture.is_relative_to(ROOT / 'tests'):
        raise ValueError('Compile the actual test source in place under tests')
    out = checked(args.output, exists=False)
    if not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Fresh workspace build output required')
    resources = checked(args.resources)
    if not resources.is_relative_to(ROOT) or not resources.is_dir():
        raise ValueError('Existing workspace fixture resources required')
    groups = args.groups.split(',')
    if not args.groups or len(groups) != len(set(groups)) or any(not re.fullmatch(r'[a-zA-Z0-9_-]+', g) for g in groups):
        raise ValueError('Explicit distinct fixture groups required')
    inputs = {}

    def track(path, expected=None):
        path = checked(path)
        key = str(path).casefold()
        row = identity(path)
        if expected is not None and row['sha256'] != expected:
            raise ValueError('Production input changed before compilation: ' + str(path))
        if key in inputs and inputs[key]['sha256'] != row['sha256']:
            raise ValueError('Input changed while preparing: ' + str(path))
        inputs[key] = row

    for row in sources + current['linkInputs']:
        track(row['path'], row['sha256'])
    for path in (args.receipt, args.msbuild_log, args.environment, seal_path, __file__, fixture):
        track(path)
    track(current['sourceInventory']['path'])
    track(current['artifact']['path']); track(current['compiler']['path']); track(current['linker']['path'])
    for directory in (ROOT / 'tests', resources):
        for path in directory.rglob('*'):
            if path.is_file():
                track(path)
    compile_cmd = current['compileCommand']
    dirs = [Path(p) for p in environment.get('INCLUDE', '').split(';') if p]
    dirs += [Path(p) for p in environment.get('LIB', '').split(';') if p]
    dirs += [Path(p[2:]) for p in compile_cmd if p.startswith('/I') and len(p) > 2]
    dirs = [p if p.is_absolute() else SDK_CWD / p for p in dirs]
    dirs += [Path(p[9:]) for p in current['linkCommand'] if p.lower().startswith('/libpath:')]
    compiler_resources = subprocess.check_output([compile_cmd[0], '-print-resource-dir'], cwd=SDK_CWD, text=True).strip()
    dirs.append(Path(compiler_resources))
    if args.adapter_dir:
        dirs.append(checked(args.adapter_dir))
    for directory in set(checked(p) for p in dirs if p.is_dir()):
        for path in directory.rglob('*'):
            if path.is_file() and (not path.suffix or path.suffix.lower() in ('.h', '.hpp', '.hxx', '.inl', '.inc', '.tcc', '.lib')):
                # MSBuild bookkeeping can live below the SDK include root. It
                # is not a compiler/linker input and changes in other builds.
                # An actual dependency here still fails the unrecorded-input
                # check rather than being silently accepted.
                if any(part.lower().endswith('.tlog') for part in path.parts):
                    continue
                track(path)
    out.mkdir(); (out / 'temp').mkdir()
    receipt = dict(success=False, inputsUnchanged=False, sourceCopies=0, fullDLL=False,
                   deployed=False, liveGameplay=False, fixture=str(fixture), productionSeal=str(seal_path),
                   revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   steps=[], inputsBefore=list(inputs.values()))
    env = dict(os.environ)
    for key, value in environment.items():
        for prior in list(env):
            if prior.casefold() == key.casefold():
                del env[prior]
        env[key] = value
    env['TEMP'] = env['TMP'] = str(out / 'temp')

    def save():
        (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf8')

    try:
        compile_job = [x for x in compile_cmd if not (x.lower().endswith(('.cpp', '.c')) or
            x.lower().startswith(('/yu', '/yc', '/fo', '/fd')) or (x.lower().startswith('/fp') and not x.lower().startswith('/fp:')))]
        compile_job += ['/Y-', '/showIncludes', '/I' + str(ROOT), '/Fo' + str(out / 'fixture.obj'), '/Fd' + str(out / 'fixture.pdb')]
        if args.adapter_dir:
            compile_job += ['/I' + str(checked(args.adapter_dir))]
        compile_job += [str(fixture)]
        remove = ('/out:', '/pdb:', '/implib:', '/def:', '/ltcgout:', '/manifest', '/natvis:', '/subsystem:', '/threads:', '/opt:lldltojobs=', '/lldltocache:')
        link_job = [x for x in current['linkCommand'] if x.lower() != '/dll' and not x.lower().startswith(remove)]
        link_job += ['/SUBSYSTEM:CONSOLE', '/OUT:' + str(out / 'rebuy.exe'), '/PDB:' + str(out / 'rebuy.pdb'),
                     '/threads:1', '/opt:lldltojobs=1', '/verbose', str(out / 'fixture.obj')]
        jobs = [('compile', compile_job), ('link', link_job)] + [(g, [str(out / 'rebuy.exe'), g, str(resources)]) for g in groups]
        for name, command in jobs:
            save(); started = time.monotonic()
            launched = command
            if name in ('compile', 'link'):
                rsp = out / (name + '.rsp')
                rsp.write_text(subprocess.list2cmdline(command[1:]), encoding='utf8')
                launched = [command[0], '@' + str(rsp)]
            done = subprocess.run(launched, cwd=SDK_CWD, env=env, capture_output=True, timeout=240)
            log = out / (name + '.log'); log.write_bytes(done.stdout + done.stderr)
            text = log.read_text('utf-8-sig', errors='replace')
            step = dict(name=name, command=command, exit=done.returncode,
                        seconds=time.monotonic() - started, log=str(log), dependencies=[])
            receipt['steps'].append(step)
            save()
            observed = re.findall(r'Note: including file:\s*([^\r\n]+)', text) if name == 'compile' else []
            if name == 'link':
                archives = {}
                for archive, member in re.findall(r'^lld-link: Reading (.*?\.lib)(\([^\r\n]*\))?\s*$', text, re.M):
                    if member:
                        if len(archives.get(Path(archive).name.casefold(), set())) != 1:
                            raise ValueError('Unresolved archive member: ' + archive + member)
                    else:
                        resolved = str(checked(SDK_CWD / archive))
                        archives.setdefault(Path(archive).name.casefold(), set()).add(resolved)
                        observed.append(resolved)
            for path in observed:
                path = checked(SDK_CWD / path.strip())
                if str(path).casefold() not in inputs:
                    raise ValueError('Unrecorded compiler/linker input: ' + str(path))
                track(path)
            step['dependencies'] = observed
            save(); print(name, done.returncode, flush=True)
            if done.returncode or name not in ('compile', 'link'):
                print(text[-8000:], flush=True)
            if done.returncode:
                return 1
            if name not in ('compile', 'link'):
                matches = re.findall(r'^behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$', text, re.M)
                if len(matches) != 1 or int(matches[0][0]) <= 0 or int(matches[0][1]) <= 0 or int(matches[0][2]):
                    raise ValueError('Missing nonempty behavior/SDK result summary: ' + name)
        receipt['executable'] = identity(out / 'rebuy.exe')
        receipt['success'] = True
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        receipt['changedInputs'] = [row['path'] for row in inputs.values() if not Path(row['path']).is_file() or digest(row['path']) != row['sha256']]
        receipt['inputsUnchanged'] = not receipt['changedInputs']
        receipt['success'] &= receipt['inputsUnchanged']
        save()
    return 0 if receipt['success'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('receipt', 'msbuild-log', 'environment', 'link-inputs'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--source-inputs', type=Path)
    parser.add_argument('--seal-only', action='store_true')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--fixture', type=Path, default=ROOT / 'tests/csol-rebuy/sdk.cpp')
    parser.add_argument('--resources', type=Path)
    parser.add_argument('--adapter-dir', type=Path)
    parser.add_argument('--groups', default='')
    args = parser.parse_args()
    if not args.seal_only and (args.output is None or args.resources is None or not args.groups):
        parser.error('execution needs --output, --resources and --groups')
    return run(args)


if __name__ == '__main__':
    raise SystemExit(main())
