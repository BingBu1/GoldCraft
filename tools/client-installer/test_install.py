"""Run the real official installer in a new, non-playable fixture; never start CS.

Needs a separately prepared package. Copies three identity files from a verified
runtime, writes only under --fixture, and retains every test artifact for review.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import time

from package import digest, safe, validate_publish


def check_package_plan(plan_path, output):
    import copy
    plan = json.loads(plan_path.read_text('utf-8'))
    rejected = []
    for name in ('../escaped.txt', '/absolute.txt', 'x/../../escaped.txt', 'C:/escaped.txt',
                 'x\\escaped.txt', 'x//escaped.txt', 'package.json'):
        changed = copy.deepcopy(plan)
        changed['files'][0]['path'] = name
        try:
            validate_publish(changed, output)
        except ValueError:
            rejected.append(name)
        else:
            raise AssertionError('Unsafe path accepted: ' + name)
    duplicate = copy.deepcopy(plan)
    duplicate['files'].append(dict(duplicate['files'][0], path=duplicate['files'][0]['path'].upper()))
    try:
        validate_publish(duplicate, output)
    except ValueError:
        rejected.append('case-insensitive duplicate')
    else:
        raise AssertionError('Duplicate path accepted')
    post_path = 'install/output/svencoop/' + next(iter(plan['postEffects']['resources']))
    for case in ('missing shader', 'wrong shader hash', 'missing contract', 'stale contract'):
        changed = copy.deepcopy(plan)
        if case == 'missing shader':
            changed['files'] = [row for row in changed['files'] if row['path'] != post_path]
        elif case == 'wrong shader hash':
            next(row for row in changed['files'] if row['path'] == post_path)['sha256'] = '0' * 64
        elif case == 'missing contract':
            del changed['postEffects']
        else:
            changed['postEffects']['resources'].pop(next(iter(changed['postEffects']['resources'])))
        try:
            validate_publish(changed, output)
        except ValueError:
            rejected.append(case)
        else:
            raise AssertionError('Incomplete post resources accepted: ' + case)
        assert not output.exists() and not output.with_suffix('.zip').exists()
    try:
        validate_publish(plan, Path(plan['files'][0]['source']).parent)
    except ValueError:
        rejected.append('output overlaps existing source directory')
    else:
        raise AssertionError('Source/output overlap accepted')
    assert validate_publish(plan, output) == output.absolute()
    assert not output.exists() and not output.with_suffix('.zip').exists()
    print(json.dumps(dict(negativeCasesRejected=rejected, validPlanAccepted=True, filesWritten=False)))


def snapshot(root):
    return {p.relative_to(root).as_posix(): digest(p) for p in root.rglob('*')
            if p.is_file() and '.goldcraft-installer' not in p.parts}


def run(package, game, args=(), expected=0):
    command = ['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
               '-File', str(package / 'Install-GoldCraft.ps1'), '-GameDirectory', str(game), *args]
    start = time.monotonic()
    environment = dict(os.environ)
    environment.pop('PSModulePath', None)
    completed = subprocess.run(command, cwd=package, capture_output=True, timeout=120, env=environment)
    result = dict(arguments=list(args), exitCode=completed.returncode,
                  elapsedSeconds=time.monotonic() - start,
                  stdout=completed.stdout.decode('utf-8', errors='replace'),
                  stderr=completed.stderr.decode('utf-8', errors='replace'))
    if completed.returncode != expected:
        raise AssertionError(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--check-package-plan', type=Path)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    if args.check_package_plan:
        check_package_plan(args.check_package_plan, args.fixture)
        return
    if not args.package or not args.runtime:
        parser.error('--package and --runtime are required for actual installation tests')
    package, runtime, fixture = map(safe, (args.package, args.runtime, args.fixture))
    if fixture.exists() or fixture == runtime or runtime.is_relative_to(fixture) or fixture.is_relative_to(runtime):
        raise ValueError('Use a new independent fixture outside the runtime')
    game = fixture / 'Half-Life'
    game.mkdir(parents=True)
    report = dict(passed=False, gameStarted=False, productionInstalled=False, steps=[], checks=[])

    def check(value, label):
        if not value:
            raise AssertionError(label)
        report['checks'].append(label)

    def write(relative, text):
        target = safe(game / relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8')

    started = time.monotonic()
    try:
        inputs = ('hw.dll', 'cstrike/cl_dlls/client.dll', 'cstrike/liblist.gam')
        before_inputs = {name: digest(runtime / name) for name in inputs}
        for name in inputs:
            target = game / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((runtime / name).read_bytes())
        write('cstrike/config.cfg', 'bind "e" "+use"\nbind "r" "+reload"\n')
        write('cstrike/metahook/configs/plugins.lst', '// personal comment\nCustomBefore.dll\nRenderer.dll\nCustomAfter.dll\n')
        write('cstrike/metahook/configs/dllpaths.lst', 'PersonalLibraries\n')
        write('cstrike/metahook/configs/plugins_goldsrc.lst', 'OldTemplate.dll\n')
        manifest = json.loads((package / 'package.json').read_text('utf-8'))
        candidates = set()
        for name in manifest['plugins']:
            stem = name[:-4]
            base = re.sub(r'_(AVX2|AVX|SSE2|SSE)$', '', stem)
            for prefix in (base, stem):
                for suffix in ('_AVX2.dll','_AVX.dll','_SSE2.dll','_SSE.dll','.dll'):
                    candidates.add('cstrike/metahook/plugins/' + prefix + suffix)
        for name in sorted(candidates):
            write(name, 'old CPU variant sentinel: ' + name)
        baseline = snapshot(game)
        report['steps'].append(run(package, game, ['-Plan']))
        check(snapshot(game) == baseline, 'Plan leaves all target files unchanged')
        report['steps'].append(run(package, game))
        installed = snapshot(game)
        check(installed['cstrike/config.cfg'] == baseline['cstrike/config.cfg'], 'Native key bindings preserved')
        check(installed['hw.dll'] == baseline['hw.dll'], 'Engine remains unchanged')
        check(installed['cstrike/cl_dlls/client.dll'] == baseline['cstrike/cl_dlls/client.dll'], 'Game DLL remains unchanged')
        check((game / 'MetaHook for Counter-Strike.lnk').is_file(), 'Real upstream shortcut generated')
        check(not (game / 'cstrike/metahook/plugins/GoldCraft_AVX2.dll').exists(), 'Stale preferred GoldCraft alias removed')
        text = (game / 'cstrike/metahook/configs/plugins.lst').read_text('utf-8')
        check('CustomBefore.dll' in text and 'CustomAfter.dll' in text and '// personal comment' in text, 'Existing plugins and comments retained')
        check('Renderer_AVX2.dll\nGoldCraft.dll' in text, 'Renderer and GoldCraft are adjacent')
        packaged_plugins = {'cstrike/' + row['path'][len('install/output/svencoop/'):]
                            for row in manifest['files'] if row['path'].startswith('install/output/svencoop/metahook/plugins/')}
        for name in sorted(candidates - packaged_plugins):
            check(not (game / name).exists(), 'Stale loader candidate removed: ' + name)
        report['oldCpuCandidatesCovered'] = len(candidates)
        payload_verified = 0
        for row in manifest['files']:
            if not row['path'].startswith('install/output/'):
                continue
            path = row['path'][len('install/output/'):]
            if path.startswith('svencoop/'):
                path = 'cstrike/' + path[len('svencoop/'):]
            if path in ('SDL2.dll', 'SDL3.dll') and not (game / path).exists():
                continue
            check(digest(game / path) == row['sha256'], 'Exact installed payload: ' + path)
            payload_verified += 1
        report['payloadFilesVerified'] = payload_verified
        # Startup dependencies are checked independently of package.json: a
        # self-consistent manifest used to omit Renderer localization entirely.
        for relative in ('renderer/gameui_english.txt', 'renderer/gameui_schinese.txt',
                         'renderer/gameui_koreana.txt', 'renderer/OptionsSubVideoAdvancedDlg.res',
                         'renderer/default_entity.txt', 'renderer/decal_textures.txt',
                         'captionmod/gameui_english.txt', 'bulletphysics/gameui_english.txt'):
            resource = game / 'cstrike' / relative
            check(resource.is_file() and resource.stat().st_size > 0,
                  'Plugin startup resource present: ' + relative)
        # Independently require the native shader contract, even if an omitted
        # file would leave package.json internally consistent.
        from package import runtime_resources
        runtime_resources.verify_post_runtime(game, safe)
        check(manifest.get('postEffects') == runtime_resources.post_contract(), 'Current native post contract installed')
        write('cstrike/metahook/configs/dllpaths.lst', 'changed after installation\n')
        changed = snapshot(game)
        report['steps'].append(run(package, game, ['-Restore'], expected=1))
        check(snapshot(game) == changed, 'Manual restore rejects modified files without partial writes')
        (game / 'cstrike/metahook/configs/dllpaths.lst').write_bytes(b'PersonalLibraries\r\nFreeImage\r\n')
        report['steps'].append(run(package, game, ['-Restore']))
        check(snapshot(game) == baseline, 'Successful restore matches exact prior files including removed aliases/templates')
        # Force a real File.Copy failure after backup has read this existing file.
        # FILE_SHARE_READ lets hashing/backup succeed but prevents overwrite.
        lock_path = game / 'cstrike/renderer/shader/goldcraft.vert.glsl'
        write('cstrike/renderer/shader/goldcraft.vert.glsl', 'locked old shader sentinel')
        fault_baseline = snapshot(game)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                      ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        handle = kernel.CreateFileW(str(lock_path), 0x80000000, 1, None, 3, 0, None)
        if handle == wintypes.HANDLE(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            failure = run(package, game, expected=1)
            report['steps'].append(failure)
            # Windows PowerShell wraps error text to the host width, including
            # in the middle of words. Use persisted transaction state as proof.
            transactions = [json.loads(p.read_text('utf-8')) for p in
                            (game / '.goldcraft-installer/backups').glob('*/transaction.json')]
            check('goldcraft.vert.glsl' in failure['stderr'] and
                  any(t['status'] == 'rolled-back' for t in transactions),
                  'Real official installer failure reaches rollback')
            check(snapshot(game) == fault_baseline, 'Failed official installation rolls back exact prior content')
        finally:
            kernel.CloseHandle(handle)
        report['steps'].append(run(package, game))
        first_list = (game / 'cstrike/metahook/configs/plugins.lst').read_bytes()
        report['steps'].append(run(package, game))
        check((game / 'cstrike/metahook/configs/plugins.lst').read_bytes() == first_list, 'Real repeated installation keeps an identical plugin list')
        check({name: digest(runtime / name) for name in inputs} == before_inputs, 'Read-only runtime identity inputs unchanged')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        report['elapsedSeconds'] = time.monotonic() - started
        (fixture / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('steps', 'checks')}, ensure_ascii=False))
    print('checks=' + str(len(report['checks'])))


if __name__ == '__main__':
    main()
