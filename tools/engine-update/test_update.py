"""Exercise the shipped two-DLL updater in WinPS 5.1 with real release baselines."""
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

ROOT = Path(__file__).resolve().parents[2]
STATE = '.bingsang-engine-update'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(package, output):
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    # Tests mutate only their own independent package copy.
    tested = output/'package'
    shutil.copytree(package, tested)
    target = output/'server with spaces'
    target.mkdir()
    report = dict(passed=False, checks=[], calls=[], realServerTouched=False)
    dlls = ['cs/filesystem_stdio.dll', 'cs/swds.dll']
    new = {r['path']: r['sha256'] for r in json.loads((tested/'update-manifest.json').read_text())['files']}
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]

    def put(name, data):
        p = target/name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data.encode() if isinstance(data, str) else data)

    def zip_bytes(folder, filename):
        p = next((ROOT/'server-packages'/folder).glob('*.zip'))
        with zipfile.ZipFile(p) as z:
            name = next(n for n in z.namelist() if n.endswith('/'+filename) or n == filename)
            return z.read(name)

    def check(ok, label):
        if not ok:
            raise AssertionError(label)
        report['checks'].append(label)
        print('PASS '+label, flush=True)

    def snap(root=target, state=False):
        return {p.relative_to(root).as_posix(): (sha(p), p.stat().st_mtime_ns)
                for p in root.rglob('*') if p.is_file() and (state or STATE not in p.parts)}

    def call(*args, success=True, cmd=False, selected=target):
        env = dict(os.environ)
        env.pop('PSModulePath', None)
        if cmd:
            command = ('cmd.exe /d /s /c ""'+str(tested/'Update-Server.cmd')+
                       '" -TargetDirectory "'+str(selected)+'" '+ ' '.join(args)+'"')
        else:
            command = ['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                       '-File', str(tested/'Update-Server.ps1'), '-TargetDirectory', str(selected), *args]
        start = time.monotonic()
        result = subprocess.run(command, input=b'\r\n', capture_output=True, timeout=45,
                                env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        log = output/f'call-{len(report["calls"])+1:02}.log'
        log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(args=args, cmd=cmd, exitCode=result.returncode,
                                    seconds=time.monotonic()-start, log=log.name))
        if (result.returncode == 0) != success:
            raise AssertionError(log.read_text(errors='replace')[-3000:])
        return result

    def hold(path):
        handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        return handle

    try:
        version = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()'])
        check(version.startswith(b'5.1.'), 'Actual Windows PowerShell 5.1')
        old_engine = zip_bytes('update-20261101-FastDL', 'payload/cs/swds.dll')
        old_game = zip_bytes('update-20261101-Respawn-v2', 'payload/cs/cstrike/dlls/mp.dll')
        old_provider = (ROOT/'build/csol-dependency-cache-20261103/deployment-01/old-filesystem_stdio.dll').read_bytes()
        check(hashlib.sha256(old_engine).hexdigest() == '6839933a4dfe5dc8b36185406eddaa15e4957b57008120844886de6ba00a3e3f' and
              hashlib.sha256(old_game).hexdigest() == 'cfd2e0968a7e8f303bd1ee8d037a2ba58fd8cbd7f0fed381c682fba200c827b5' and
              hashlib.sha256(old_provider).hexdigest() == '80de6898c354dd1e31b3fb99da60fe645ef3436f39b0c5a2053a3f8e431c5df7',
              'Real FastDL/Respawn-v2 DLLs and verified previous filesystem')
        put('cs/swds.dll', old_engine)
        put('cs/filesystem_stdio.dll', old_provider)
        put('cs/cstrike/dlls/mp.dll', old_game)
        for name, data in {
            'Start.cmd': 'original launcher, never execute', 'Stop.cmd': 'original stopper, never execute',
            'cs/hlds.exe': 'fixture marker, never execute',
            'server-settings.json': '{"csolGameMode":2,"map":"zs_deadend","owner":true}',
            'cs/cstrike/server.cfg': 'hostname "owner"\nrcon_password "fixture-only"\n',
            'cs/cstrike/addons/amxmodx/configs/users.ini': 'preserve admins',
            'cs/cstrike/maps/custom.bsp': 'preserve resource',
            'cs/launcher-public/manifest.json': 'preserve signed client update',
            'minecraft/world/level.dat': 'preserve save',
        }.items():
            put(name, data)
        initial = snap()
        state = target/STATE
        call('-Plan', cmd=True)
        check(snap() == initial and not state.exists(), 'CMD preview performs no writes')
        payload = tested/'payload/cs/swds.dll'
        raw = payload.read_bytes()
        payload.write_bytes(raw+b'corrupt')
        call(success=False)
        payload.write_bytes(raw)
        check(snap() == initial and not state.exists(), 'Corrupt payload rejected before writes')
        extra = tested/'payload/extra.dll'
        extra.write_bytes(b'unlisted')
        call(success=False)
        extra.unlink()
        check(snap() == initial and not state.exists(), 'Unlisted payload rejected before writes')
        engine = target/'cs/swds.dll'
        put('cs/swds.dll', b'unknown')
        call(success=False)
        check(not state.exists(), 'Unknown engine baseline rejected')
        put('cs/swds.dll', old_engine)
        original_hashes = {p: h[0] for p, h in initial.items()}
        locked = hold(engine)
        try:
            call(success=False)
        finally:
            kernel.CloseHandle(locked)
        check(not state.exists(), 'Real DLL sharing lock rejected before any transaction')
        alias = output/'old-provider-alias.dll'
        os.link(target/dlls[0], alias)
        # Inject a real replacement lock only at the second Atomic write to
        # exercise production rollback after the provider was already replaced.
        helper = tested/'Files.ps1'
        helper_before = helper.read_bytes()
        injection = r'''
$script:OriginalAtomic=${function:Atomic}
function Atomic([string]$Target,[string]$Source,[byte[]]$Bytes) {
    if($Target -eq (Join-Path $script:target 'cs/swds.dll') -and $Source -like '*payload*') {
        $held=[IO.File]::Open($Target,'Open','Read','Read')
        try { & $script:OriginalAtomic $Target $Source $Bytes } finally { $held.Dispose() }
    } else { & $script:OriginalAtomic $Target $Source $Bytes }
}
'''
        helper.write_bytes(helper_before+injection.encode())
        call(success=False)
        helper.write_bytes(helper_before)
        check({p:h[0] for p,h in snap().items()} == original_hashes,
              'Second-DLL replacement failure rolls back the first DLL')
        call(cmd=True)
        installed = snap()
        check(all(sha(target/p) == new[p] for p in dlls), 'Actual CMD installs both hash-pinned DLLs')
        check(sha(alias) == hashlib.sha256(old_provider).hexdigest() and not os.path.samefile(alias, target/dlls[0]),
              'Atomic install detaches hard link without changing its owner')
        check([p for p in original_hashes if original_hashes[p] != installed[p][0]] == ['cs/filesystem_stdio.dll','cs/swds.dll'],
              'Only two DLLs change; configuration, GameDLL, launcher, resources and Minecraft stay exact')
        all_before = snap(state, state=True)
        call(); call('-Plan')
        check(snap() == installed and snap(state, state=True) == all_before,
              'Idempotent install skips copies and preserves backup metadata')
        call('-Restore')
        check({p:h[0] for p,h in snap().items()} == original_hashes, 'Rollback restores both original DLLs')
        call('-Restore')
        check({p:h[0] for p,h in snap().items()} == original_hashes, 'Repeated rollback is safe')
        call()
        journal_path = state/'transaction.json'
        journal = json.loads(journal_path.read_text())
        put('cs/swds.dll', old_engine)
        journal['status'] = 'prepared'
        journal_path.write_text(json.dumps(journal))
        call('-Restore')
        check({p:h[0] for p,h in snap().items()} == original_hashes,
              'Interrupted paired update recovers from provider-new/engine-old state')
        # Skip one already-current file while preserving it through rollback.
        put('cs/filesystem_stdio.dll', (tested/'payload/cs/filesystem_stdio.dll').read_bytes())
        call()
        check(len(json.loads(journal_path.read_text())['files']) == 1,
              'Partial manual update copies and backs up only the remaining DLL')
        call('-Restore')
        check(sha(target/dlls[0]) == new[dlls[0]] and sha(engine) == hashlib.sha256(old_engine).hexdigest(),
              'Rollback preserves a DLL already current before installation')
        # Test acceptance of the older public Scenario pair without forcing a mode change.
        put('cs/swds.dll', zip_bytes('update-20261101-Scenario', 'payload/cs/swds.dll'))
        put('cs/cstrike/dlls/mp.dll', zip_bytes('update-20261101-Scenario', 'payload/cs/cstrike/dlls/mp.dll'))
        call('-Plan')
        check(True, 'Existing public Scenario baseline accepted without requiring earlier FastDL patch')
        call('-Plan', success=False, selected='')
        check(True, 'Explicit empty path is rejected')
        report['passed'] = True
        print(json.dumps(dict(passed=True, checks=len(report['checks']), calls=len(report['calls']))))
    finally:
        (output/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', 'utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    main(args.package.resolve(), args.output.resolve())
