"""Run the shipped resource updater on the actual composite ZIP baseline."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import shutil
import subprocess
import time
import zipfile
from package import plain, sha
from package_resources import index


def run(package, receipt_path, output, reuse, previous_patch=None):
    package, receipt_path, output = map(plain, (package, receipt_path, output))
    output.mkdir(parents=True, exist_ok=False)
    server = output/'server'
    manifest = json.loads((package/'update-manifest.json').read_text('utf-8-sig'))
    receipt = json.loads(receipt_path.read_text('utf-8-sig'))
    merged = index(receipt['archives'])
    result = dict(passed=False, checks=[], calls=[], reusedFiles=0, reusedLogicalBytes=0)

    def check(ok, text):
        if not ok:
            raise AssertionError(text)
        result['checks'].append(text)

    def put(name, content):
        p = server/name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(content.encode('utf-8') if isinstance(content, str) else content)

    def snapshot():
        return {p.relative_to(server).as_posix().lower(): sha(p) for p in server.rglob('*') if p.is_file()
                and '.bingsang-resource-updates' not in p.parts}

    def call(*args, success=True):
        env = dict(os.environ)
        env.pop('PSModulePath', None)
        started = time.monotonic()
        proc = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
            '-File', str(package/'Update-Server.ps1'), '-TargetDirectory', str(server), *args],
            env=env, capture_output=True, timeout=180)
        log = output/f'call-{len(result["calls"])+1:02}.log'
        log.write_bytes(proc.stdout+proc.stderr)
        result['calls'].append(dict(args=args, seconds=round(time.monotonic()-started, 3),
                                   exitCode=proc.returncode, log=str(log)))
        if (proc.returncode == 0) != success:
            raise AssertionError((proc.stdout+proc.stderr).decode(errors='replace')[-2200:])
        return proc.stdout.decode(errors='replace')

    opened = {}
    try:
        def seed(row):
            target = server/row['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            # Only reuse the known frozen/payload pair from a completed package.
            # Never link live servers or links with additional unknown owners.
            source = plain(reuse/'frozen'/row['path']) if reuse else None
            peer = plain(reuse/'冰桑-远程灾变更新/payload'/row['path']) if reuse else None
            if (source and source.suffix.lower() in {'.tga', '.bmp', '.mdl', '.wav', '.spr', '.cso'}
                    and source.is_file() and peer.is_file() and source.stat().st_nlink == 2
                    and os.path.samestat(source.stat(), peer.stat()) and sha(source) == row['sha256']):
                os.link(source, target)
                check(os.path.samestat(source.stat(), target.stat()), 'frozen file identity reused: '+row['path'])
                result['reusedFiles'] += 1
                result['reusedLogicalBytes'] += row['bytes']
            else:
                old = merged[row['path'].lower()]
                archive = opened.get(old['archive'])
                if archive is None:
                    archive = opened[old['archive']] = zipfile.ZipFile(old['archive'])
                with archive.open(old['member']) as stream, target.open('xb') as dest:
                    shutil.copyfileobj(stream, dest)
            if target.stat().st_size != row['bytes'] or sha(target) != row['sha256']:
                raise AssertionError('Seed does not match actual baseline')

        for row in manifest['requiredBaseline']:
            seed(row)
        for row in receipt['previous']:
            seed(row)
        settings = json.dumps(dict(map='zs_deadend', csolGameMode=2, csPort=27015, privateOwner='keep'))
        protected = {'Start.cmd':'existing original start', 'cs/hlds.exe':'fixture; never execute',
            'server-settings.json':settings, 'scripts/Start-Servers.ps1':'existing native branch',
            'scripts/Common.ps1':'existing helpers', 'scripts/Run-Server.ps1':'existing native arguments',
            'cs/cstrike/server.cfg':'rcon_password "fixture-private"\nmc_http_port 27080\n',
            'cs/cstrike/addons/amxmodx/configs/plugins.ini':'goldcraft_game_mode.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/users.ini':'fixture-admin',
            '.bingsang-updates/latest.json':'preserve earlier mode rollback',
            'minecraft/world/level.dat':'preserve world'}
        for name, value in protected.items():
            put(name, value)
        prior_payload = None
        if previous_patch:
            previous_patch = plain(previous_patch)
            prior_payload = index([dict(path=str(previous_patch), bytes=previous_patch.stat().st_size,
                                        sha256=sha(previous_patch))])
        same = next(r for r in manifest['files'] if r['path'] not in {x['path'] for x in receipt['previous']}
                    and (prior_payload is None or r['path'].lower() in prior_payload))
        put(same['path'], (package/'payload'/same['path']).read_bytes())
        initial = snapshot()
        old_model = next(r for r in receipt['previous'] if r['path'].endswith('.mdl'))
        alias = output/'old-model-owner.mdl'
        os.link(server/old_model['path'], alias)
        call('-Plan')
        check(snapshot() == initial, f'full {len(manifest["requiredBaseline"])}-file baseline plan changes no server files')
        music = next((r for r in manifest['files'] if r['path'].endswith('.mp3')), None)
        if music:
            p = package/'payload'/music['path']
            original = p.read_bytes()
            temporary = p.with_name(p.name+'.test-independent')
            try:
                temporary.write_bytes(b'incomplete MP3')
                os.replace(temporary, p)
                call('-Plan', success=False)
                check(snapshot() == initial, 'corrupted MP3 payload rejected before any server write')
            finally:
                temporary.write_bytes(original)
                os.replace(temporary, p)
            check(sha(p) == music['sha256'], 'MP3 fixture restored without modifying its frozen hardlink peer')
        baseline = next(r for r in manifest['requiredBaseline'] if r['bytes'] < 2048)
        p = server/baseline['path']
        original = p.read_bytes()
        temp = p.with_name(p.name+'.test-independent')
        temp.write_bytes(b'bad baseline');os.replace(temp, p)
        call('-Plan', success=False)
        temp.write_bytes(original);os.replace(temp, p)
        check(snapshot() == initial, 'bad omitted baseline rejected before writes')
        # Real Win32 sharing violation on a replaced file late in the transaction.
        locked = next(r for r in receipt['previous'] if r['path'].endswith('characters.txt'))
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.CreateFileW(str(server/locked['path']),0x80000000,1,None,3,0,None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            call(success=False)
        finally:
            kernel.CloseHandle(handle)
        check(snapshot() == initial, 'actual late write failure rolls back new and replaced resources')
        call()
        installed = snapshot()
        check(all(installed[r['path'].lower()] == r['sha256'] for r in manifest['files']), f'all {len(manifest["files"])} final resource hashes installed')
        check(all(installed[r['path'].lower()] == r['sha256'] for r in manifest['requiredBaseline']), f'all {len(manifest["requiredBaseline"])} required baseline resources and stable anchors preserved')
        check(all(installed[name.lower()] == initial[name.lower()] for name in protected), 'native mode, ports, credentials, admins, MC and earlier mode rollback unchanged')
        check(sha(alias) == old_model['sha256'] and sha(server/old_model['path']) != sha(alias), 'shared old model detached by atomic replacement')
        check('Already current' in call(), 'second update skips every identical payload file')
        check(snapshot() == installed, 'second update has zero content changes')
        call('-Restore')
        check(snapshot() == initial, 'resource-only restore returns original bytes and mode rollback stays intact')
        if previous_patch:
            previous_patch = plain(previous_patch)
            previous = index([dict(path=str(previous_patch), bytes=previous_patch.stat().st_size,
                                   sha256=sha(previous_patch))])
            final = {r['path'].lower(): r for r in manifest['files']}
            with zipfile.ZipFile(previous_patch) as archive:
                for key, row in previous.items():
                    if key not in final or final[key]['sha256'] != row['sha256']:
                        raise AssertionError('Cumulative patch does not retain the previous payload')
                    content = archive.read(row['member'])
                    if len(content) != row['bytes'] or hashlib.sha256(content).hexdigest() != row['sha256']:
                        raise AssertionError('Previous resource ZIP bytes mismatch')
                    dest = server/row['path']
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    temporary = dest.with_name(dest.name+'.test-independent')
                    temporary.write_bytes(content)
                    os.replace(temporary, dest)
            prior_installed = snapshot()
            plan_output = call('-Plan')
            plan = json.loads(plan_output[plan_output.index('{'):])
            check(plan['changed'] == len(final) - len(previous) and plan['unchanged'] == len(previous),
                  'already installed previous patch skips its entire payload and plans only new resources')
            call()
            updated = snapshot()
            check(all(updated[r['path'].lower()] == r['sha256'] for r in manifest['files']),
                  'cumulative update also installs correctly over the previous resource patch')
            check(all(updated[name.lower()] == initial[name.lower()] for name in protected),
                  'cumulative upgrade preserves native mode, settings and earlier rollback')
            call('-Restore')
            check(snapshot() == prior_installed, 'cumulative upgrade restores the previous patch exactly')
            result['previousPatch'] = dict(path=str(previous_patch), sha256=sha(previous_patch), files=len(previous))
        result['passed'] = True
        # Release only baseline files created by this test after their final hash
        # check. ZIPs and reusable frozen resources remain intact.
        released = 0
        for row in manifest['requiredBaseline']:
            p = plain(server/row['path'])
            if not p.is_relative_to(server) or sha(p) != row['sha256']:
                raise AssertionError('Refusing to clean an altered fixture file')
            released += p.stat().st_size if p.stat().st_nlink == 1 else 0
            p.unlink()
        result['temporaryIndependentBytesRemoved'] = released
        result['fixtureBaselineRemovedAfterPass'] = True
        print(json.dumps({k: v for k, v in result.items() if k != 'checks'}, indent=2))
        print('Installer behavior checks:', sum(not x.startswith('frozen file identity reused:') for x in result['checks']))
    finally:
        for archive in opened.values():
            archive.close()
        (output/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', 'utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    for name in ['package', 'receipt', 'output']:
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--reuse', type=Path)
    p.add_argument('--previous-patch', type=Path)
    a = p.parse_args()
    run(a.package, a.receipt, a.output, a.reuse, a.previous_patch)
