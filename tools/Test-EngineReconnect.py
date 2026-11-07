#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract production reconnect bodies; optionally build old/new x86 SDK controls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GROUPS = ['baseline', 'load-delay', 'response', 'clock', 'eligibility', 'repeat']
EXPECTED_OLD = {
    'baseline': [], 'eligibility': [],
    'load-delay': [
        'load: inactivation retires earlier request before loading',
        'load: no deadline exists while transmit executes',
        'load: request begins at transmit completion using advancing clock',
        'load: first frame after 32-second load does not kick freshly requested client'] * 2,
    'response': ['send: full 30 seconds remain after long server loading'],
    'clock': ['clock: same advancing send clock expires stalled-frame request',
              'clock: frame-clock discontinuity cannot consume wall-clock request allowance'],
    'repeat': ['repeat: second map has no request during loading',
               'repeat: second actual send establishes a fresh deadline',
               'repeat: previous map and second load cannot expire new request',
               'repeat: abandoned pending request retires before new load',
               'repeat: abandoned request cannot kick client after replacement send'],
}
BOUNDARY = ('Unmodified extracted SV_New_f/SV_CheckTimeouts/SV_InactivateClients/SV_BuildReconnect, '
            'actual final SV_ActivateServer_internal client loop, and SV_SendReconnect when present. '
            'Actual ReHLDS x86 client_t/netchan_t/cvar/DLL_FUNCTIONS headers. Clock/network/game-DLL '
            'edges and CGameClient owner storage are controlled; its three inline methods are exact '
            'header extracts. Not full engine execution, real packets, loading acceleration, deployment or gameplay.')


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def masked(text):
    # Preserve offsets while excluding braces in comments and quoted literals.
    return re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: re.sub(r'[^\n]', ' ', m[0]), text)


def block_end(text, start):
    stripped = masked(text)
    opening = stripped.index('{', start)
    depth = 0
    for i in range(opening, len(text)):
        if stripped[i] == '{': depth += 1
        elif stripped[i] == '}':
            depth -= 1
            if not depth: return i + 1
    raise ValueError('unclosed production block')


def extract(text, name):
    pattern = r'^(?:static )?void (?:EXT_FUNC )?' + re.escape(name) + r'\([^;\n]*\)\s*\{'
    matches = list(re.finditer(pattern, text, re.M))
    assert len(matches) == 1, name
    start = matches[0].start()
    return text[start:block_end(text, start)]


def parts(text):
    selected = {name: extract(text, name) for name in
                ['SV_New_f', 'SV_CheckTimeouts', 'SV_InactivateClients', 'SV_BuildReconnect']}
    if re.search(r'^static void SV_SendReconnect\(', text, re.M):
        selected['SV_SendReconnect'] = extract(text, 'SV_SendReconnect')
    activate = extract(text, 'SV_ActivateServer_internal')
    loop_start = activate.index('for (i = 0, cl = g_psvs.clients; i < g_psvs.maxclients; cl++, i++)')
    loop = activate[loop_start:block_end(activate, loop_start)]
    assert 'if (!cl->fakeclient && (cl->active || cl->connected))' in loop
    assert 'if (g_psvs.maxclients > 1)' in loop and 'SV_SendServerinfo(&msg, cl);' in loop
    if 'SV_SendReconnect' in selected:
        assert 'SV_SendReconnect(cl);' in loop and 'SV_BuildReconnect' not in loop
    else:
        assert 'SV_BuildReconnect(&cl->netchan.message);\n\t\t\t\tNetchan_Transmit(&cl->netchan, 0, NULL);' in loop
    selected['SV_ActivateServer_final_loop'] = loop
    selected['SV_ActivateServer_locals'] = activate[activate.index('{') + 1:activate.index('#ifndef REHLDS_FIXES')]
    for name in ['sv_timeout', 'sv_reconnect_timeout']:
        match = re.search(r'^cvar_t ' + name + r' = [^\n]*;', text, re.M)
        assert match
        selected[name] = match[0]
    return selected


def write_extracted(text, source_identity, output, owner_header):
    selected = parts(text)
    body = '// Exact source extracts, with only a fixture wrapper around the activation loop.\n'
    for name in ['sv_timeout', 'sv_reconnect_timeout', 'SV_BuildReconnect', 'SV_SendReconnect',
                 'SV_New_f', 'SV_CheckTimeouts', 'SV_InactivateClients']:
        if name in selected: body += selected[name] + '\n\n'
    body += 'void Test_ActivateReconnect() {\n' + selected['SV_ActivateServer_locals'] + '\n'
    body += selected['SV_ActivateServer_final_loop'] + '\n}\n'
    target = output / 'actual_reconnect.inc'; target.write_text(body, encoding='utf-8', newline='\n')
    header = owner_header.read_text('utf-8-sig')
    methods = []
    for name in ['SetupLocalGameTime', 'GetReconnectDeadline', 'SetReconnectDeadline']:
        match = re.search(r'\b(?:void|double) ' + name + r'\([^\n]*?\)\s*(?:const\s*)?\{', header)
        assert match, name
        methods.append(header[match.start():block_end(header, match.start())])
    owner = output / 'actual_game_client.inc'; owner.write_text('\n'.join(methods) + '\n', encoding='utf-8')
    metadata = dict(source=source_identity, extracted=identity(target), ownerHeader=identity(owner_header),
        ownerMethods=identity(owner), methods={name: dict(bytes=len(value.encode()), sha256=hashlib.sha256(value.encode()).hexdigest())
                                            for name, value in selected.items()},
        hasSendHelper='SV_SendReconnect' in selected, compiled=False, gameplay=False)
    save(output / 'extraction.json', metadata)
    return metadata, selected


def git_baseline(reference, output, sdk_root):
    """Apply the published patch in an isolated Git index; never write a source tree."""
    patch_spec = reference + ':patches/rehlds-goldcraft.patch'
    patch = subprocess.check_output(['git', '-C', str(ROOT), 'show', patch_spec])
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', reference + '^{commit}'], text=True).strip()
    patch_blob = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', patch_spec], text=True).strip()
    upstream = subprocess.check_output(['git', '-C', str(sdk_root), 'rev-parse', 'HEAD'], text=True).strip()
    handle, name = tempfile.mkstemp(prefix='reconnect-index-', dir=output)
    os.close(handle)
    index = Path(name)
    index.unlink()  # git read-tree requires an absent or valid index, not an empty file.
    environment = dict(os.environ, GIT_INDEX_FILE=str(index))
    def git(*arguments, data=None):
        return subprocess.check_output(['git', '-C', str(sdk_root), *arguments], input=data, env=environment)
    try:
        git('read-tree', upstream)
        # Published patches normalize CRLF. Match that normalization in the
        # temporary index, leaving both the upstream blob and working tree intact.
        original = git('show', upstream + ':rehlds/engine/sv_main.cpp')
        normalized = original.replace(b'\r\n', b'\n')
        base_blob = git('hash-object', '-w', '--stdin', data=normalized).decode().strip()
        git('update-index', '--cacheinfo', '100644', base_blob, 'rehlds/engine/sv_main.cpp')
        git('apply', '--cached', '--whitespace=nowarn', '--include=rehlds/engine/sv_main.cpp', '-', data=patch)
        raw = git('show', ':rehlds/engine/sv_main.cpp')
    finally:
        # This is only the temporary Git index created by this function.
        index.unlink(missing_ok=True)
        Path(str(index) + '.lock').unlink(missing_ok=True)
    return raw.decode('utf-8-sig').replace('\r\n', '\n'), dict(
        kind='git-patch-new-side', commit=commit, patchBlob=patch_blob,
        upstreamCommit=upstream, normalizedBaseBlob=base_blob, path='rehlds/engine/sv_main.cpp',
        bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(),
        patchSha256=hashlib.sha256(patch).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'external/ReHLDS/rehlds/engine/sv_main.cpp')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/engine-reconnect-tests')
    parser.add_argument('--old-source', type=Path, help='optional explicit legacy source; default comes from Git')
    parser.add_argument('--old-ref', default='1987898', help='published root Git revision used for the old patch new-side')
    parser.add_argument('--compare-source', type=Path)
    parser.add_argument('--compiler', type=Path)
    parser.add_argument('--environment', type=Path)
    parser.add_argument('--sdk-root', type=Path, default=ROOT / 'external/ReHLDS')
    args = parser.parse_args()
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / 'build')
    out.mkdir(parents=True, exist_ok=True)
    owner_header = args.sdk_root / 'rehlds/rehlds/rehlds_interfaces_impl.h'
    if not args.compiler:
        write_extracted(args.source.read_text('utf-8-sig'), identity(args.source), out, owner_header)
        return
    result = dict(success=False, boundary=BOUNDARY, inputs=[], steps=[], tests=[], sources=[], actualIncludes=[])
    try:
        inputs = result['inputs']
        inputs += [identity(p) for p in [__file__, args.compiler, owner_header,
                   ROOT / 'tests/engine-reconnect/main.cpp', args.source]]
        environment = dict(os.environ)
        if args.environment:
            inputs.append(identity(args.environment))
            environment.update(json.loads(args.environment.read_text('utf-8-sig')))
        if args.old_source:
            old_text, old_identity = args.old_source.read_text('utf-8-sig'), identity(args.old_source)
            inputs.append(old_identity)
        else:
            old_text, old_identity = git_baseline(args.old_ref, out, args.sdk_root)
        (out / 'temp').mkdir(exist_ok=True)
        environment['TMP'] = environment['TEMP'] = str(out / 'temp'); environment['VSLANG'] = '1033'
        fixture = ROOT / 'tests/engine-reconnect/main.cpp'
        roots = [args.sdk_root / 'rehlds' / p for p in
                 ['', 'public', 'public/rehlds', 'common', 'engine', 'pm_shared', 'rehlds', 'metamod/include']]
        flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/O2',
            '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/clang:-ffp-contract=off',
            '/Gy', '/Gw', '/utf-8', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/DREHLDS_FIXES',
            '/DREHLDS_API', '/DREHLDS_SELF', '/DREHLDS_SSE', '/DREHLDS_OPT_PEDANTIC',
            '/DDEDICATED', '/DSWDS', '/showIncludes', '/I' + str(ROOT / 'native/engine'), *['/I' + str(p.resolve()) for p in roots]]

        def execute(name, command, timeout=120):
            start = time.monotonic()
            done = subprocess.run(command, cwd=out, env=environment, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
            output = done.stdout.decode('utf-8', 'replace')
            log = out / (name + '.log'); log.write_bytes(done.stdout)
            included = [identity(p.strip()) for p in sorted(set(re.findall(r'Note: including file:\s*(.+)', output)))]
            row = dict(name=name, command=command, exit=done.returncode, seconds=time.monotonic() - start,
                       log=identity(log), includes=included)
            result['steps'].append(row); save(out / 'result.json', result)
            print(name, done.returncode, flush=True)
            return row, output

        jobs = []
        for role, text, source_identity in [('new', args.source.read_text('utf-8-sig'), identity(args.source)), ('old', old_text, old_identity)]:
            folder = out / role; folder.mkdir(exist_ok=True)
            extracted, selected = write_extracted(text, source_identity, folder, owner_header)
            result['sources'].append(extracted)
            inputs += [identity(folder / name) for name in ['actual_reconnect.inc', 'actual_game_client.inc', 'extraction.json']]
            if role == 'new' and args.compare_source:
                reference = parts(args.compare_source.read_text('utf-8-sig'))
                assert selected == reference, 'root/candidate extracted functions differ'
                result['rootFunctionEquality'] = dict(source=identity(args.compare_source), equal=True)
                inputs.append(identity(args.compare_source))
            command = [str(args.compiler.resolve()), *flags, '/I' + str(folder)]
            step, log = execute(role + '-dependencies', [*command, '/Zs', str(fixture)])
            if step['exit']:
                print(log[-9000:]); raise RuntimeError(role + ' dependency compile failed')
            jobs.append((role, folder, command, step['includes']))
        result['actualIncludes'] = list({row['path']: row for job in jobs for row in job[3]}.values())
        inputs += result['actualIncludes']

        def verify():
            changed = [row['path'] for row in inputs if identity(row['path']) != row]
            result['changed'] = changed
            assert not changed, 'input drift: ' + repr(changed)

        verify(); save(out / 'plan.json', result)
        result['outputs'] = []
        for role, folder, command, known in jobs:
            verify()
            obj, executable = folder / 'main.obj', folder / 'engine-reconnect.exe'
            step, log = execute(role + '-compile', [*command, '/c', '/Fo' + str(obj), str(fixture)])
            if step['exit']: print(log[-9000:]); raise RuntimeError(role + ' compile failed')
            assert step['includes'] == known
            result['outputs'].append(identity(obj)); verify()
            step, log = execute(role + '-link', [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc',
                '-fuse-ld=lld', '/MT', str(obj), '/Fe' + str(executable), '/link', '/OPT:REF', '/threads:1'])
            if step['exit']: print(log[-9000:]); raise RuntimeError(role + ' link failed')
            raw = executable.read_bytes(); pe = struct.unpack_from('<I', raw, 0x3c)[0]
            assert struct.unpack_from('<H', raw, pe + 4)[0] == 0x14c
            result['outputs'].append(identity(executable))
            for group in GROUPS:
                step, log = execute(role + '-test-' + group, [str(executable), group], 20)
                expected = EXPECTED_OLD[group] if role == 'old' else []
                failed = [line.strip() for line in re.findall(r'^FAIL (.*)$', log, re.M)]
                summary = re.search(r'group=\S+ checks=(\d+) failures=(\d+) client_t=(\d+) netchan_t=(\d+)', log)
                passed = bool(summary) and failed == expected and step['exit'] == bool(expected)
                result['tests'].append(dict(role=role, group=group, passed=passed, exit=step['exit'],
                    checks=int(summary[1]) if summary else None, failures=failed, expectedFailures=expected,
                    clientBytes=int(summary[3]) if summary else None, netchanBytes=int(summary[4]) if summary else None))
                print(log.strip(), flush=True)
        verify()
        result['success'] = len(result['tests']) == 12 and all(test['passed'] for test in result['tests'])
        result['summary'] = {role: dict(checks=sum(t['checks'] for t in result['tests'] if t['role']==role), failures=sum(len(t['failures']) for t in result['tests'] if t['role']==role)) for role in ('new','old')}
        result['directSource']=True
        result['inputDrift']=False
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        save(out / 'result.json', result)
        print(json.dumps(dict(success=result['success'], result=identity(out / 'result.json'))), flush=True)
    if not result['success']: raise SystemExit(1)


if __name__ == '__main__':
    main()
