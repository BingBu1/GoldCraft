"""Exercise the real Windows PowerShell updater on an isolated remote-like fixture."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import re
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main(package, inputs, output, scripts):
    output.mkdir(parents=True, exist_ok=False)
    target = output / 'server'
    manifest = json.loads((package / 'update-manifest.json').read_text('utf-8-sig'))
    source = {x['path']: Path(x['source']) for x in json.loads(inputs.read_text('utf-8'))['files']}
    result = {'passed': False, 'checks': [], 'calls': []}

    def put(name, value):
        p = target / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(value.encode('utf-8') if isinstance(value, str) else value)

    def check(condition, description):
        if not condition:
            raise AssertionError(description)
        result['checks'].append(description)

    def snapshot():
        return {p.relative_to(target).as_posix(): digest(p) for p in target.rglob('*') if p.is_file()
                and '.bingsang-updates' not in p.parts}

    def run(*args, success=True):
        command = ['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                   str(package / 'Update-Server.ps1'), '-TargetDirectory', str(target), *args]
        env = dict(os.environ)
        env.pop('PSModulePath', None)
        start = time.monotonic()
        call = subprocess.run(command, capture_output=True, timeout=180, env=env)
        log = output / f'call-{len(result["calls"])+1:02}.log'
        log.write_bytes(call.stdout + call.stderr)
        result['calls'].append(dict(args=args, exitCode=call.returncode, seconds=round(time.monotonic()-start, 3), log=str(log)))
        if (call.returncode == 0) != success:
            raise AssertionError(call.stdout.decode(errors='replace')[-2500:] + call.stderr.decode(errors='replace')[-2500:])
        return call.stdout.decode(errors='replace')

    try:
        values = {
            'Start.cmd': 'fixture original launcher; never execute',
            'cs/hlds.exe': 'fixture server; never execute',
            'cs/swds.dll': 'old engine',
            'cs/cstrike/dlls/mp.dll': 'old game DLL',
            'server-settings.json': json.dumps(dict(map='cs_assault', botTotalPlayers=25, csPort=27015,
                csPublicPort=46344, bindAddress='0.0.0.0', httpPublicUrl='http://public.example.invalid:28080/goldcraft/',
                steamPort=26900, bridgePort=25577, publicListing=True, ownerExtension={'keep': 'yes'})),
            'maps.json': json.dumps([dict(map='cs_assault', bspSha256='a'*64, crc32='12345678', dimension='goldcraft:cs_assault_12345678')]),
            'scripts/Run-Server.ps1': (scripts / 'Run-Server.ps1').read_bytes(),
            'scripts/Start-Servers.ps1': (scripts / 'Start-Servers.ps1').read_bytes(),
            'scripts/Common.ps1': (scripts / 'Common.ps1').read_bytes(),
            'cs/cstrike/server.cfg': 'hostname "Owner name"\nrcon_password "fixture-password"\nmc_http_public_url "http://public.example.invalid:28080/goldcraft/"\ncsol_game_mode 1\ncsol_weapons 0\n',
            'cs/cstrike/game_init.cfg': '// owner comment\ncsol_game_mode 1\n',
            'cs/cstrike/addons/amxmodx/configs/plugins.ini': '; owner note\ncustom.amxx debug\ngoldcraft.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/plugins-zp.ini': 'zp50_core.amxx\ngoldcraft_game_mode.amxx disabled\ngoldcraft_scenario_admin.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/goldcraft/game_modes.ini': '; owner policy\ncustom.amxx 7\nzp50_* 7\n',
            'cs/cstrike/addons/amxmodx/configs/modules.ini': 'reapi\n',
            'cs/cstrike/addons/amxmodx/configs/users.ini': 'private-admin-fixture',
            'cs/cstrike/addons/sypb/sypb.cfg': '// owner\nsypb_auto_players 25\nsypb_quota 24\n',
            'minecraft/mods/owner.jar': 'preserve jar',
            'minecraft/goldcraft-worlds/level.dat': 'preserve save',
            '.state/secrets.json': '{"token":"private-fixture"}',
        }
        for name, value in values.items():
            put(name, value)
        for row in manifest['requiredBaseline']:
            put(row['path'], source[row['path']].read_bytes())
        initial = snapshot()
        protected = ['Start.cmd', 'cs/cstrike/addons/amxmodx/configs/users.ini', 'minecraft/mods/owner.jar',
                     'minecraft/goldcraft-worlds/level.dat', '.state/secrets.json', 'maps.json', 'scripts/Common.ps1']
        run('-Plan')
        check(snapshot() == initial, 'plan does not change server files')
        absent = target / manifest['requiredBaseline'][0]['path']
        original = absent.read_bytes()
        absent.write_bytes(b'mismatched baseline')
        expected = snapshot()
        run('-Plan', success=False)
        check(snapshot() == expected, 'wrong omitted baseline rejects before any write')
        absent.write_bytes(original)
        unlisted = package / 'payload/unlisted.txt'
        unlisted.write_text('fixture')
        try:
            run('-Plan', success=False)
            check(snapshot() == initial, 'unlisted payload rejects without mutation')
        finally:
            unlisted.unlink()
        # Hold the last script against replacement, causing an actual mid-commit
        # sharing violation after DLL/resource writes. No injected production hook.
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.CreateFileW(str(target/'scripts/Run-Server.ps1'),0x80000000,1,None,3,0,None)
        if handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            run(success=False)
        finally:
            kernel.CloseHandle(handle)
        check(snapshot() == initial, 'actual mid-transaction write failure fully rolls back')
        run()
        installed = snapshot()
        check(all(installed[p] == initial[p] for p in protected), 'credentials, admins, mods, saves and original launcher preserved')
        settings = json.loads((target/'server-settings.json').read_text('utf-8-sig'))
        old = json.loads(values['server-settings.json'])
        check(all(settings[k] == v for k,v in old.items() if k not in ('map','botTotalPlayers')), 'all public addresses, ports and unknown owner settings preserved')
        check(settings['map']=='zs_deadend' and settings['botTotalPlayers']==0 and settings['csolGameMode']==2, 'scenario map, native branch and bot settings configured')
        startup=(target/'scripts/Run-Server.ps1').read_text('utf-8-sig')
        line=next(x for x in startup.splitlines() if '$info.Arguments =' in x and '-game cstrike' in x)
        check(line.index('-csol_game_mode 2')<line.index('+map') and '+csol_weapons 1' in line, 'mode configured before first map precache')
        configs=target/'cs/cstrike/addons/amxmodx/configs'
        mainlist=(configs/'plugins.ini').read_text('utf-8-sig')
        check(mainlist.splitlines()[0]=='goldcraft_game_mode.amxx' and 'custom.amxx debug' in mainlist, 'mode gate first and owner plugin preserved')
        check('goldcraft_game_mode' not in (configs/'plugins-zp.ini').read_text() and 'zp50_core.amxx' in (configs/'plugins-zp.ini').read_text(), 'duplicate gates removed; ZP bytecode retained for mode policy')
        check((configs/'goldcraft/game_modes.ini').read_text().startswith('zp50_* 2\n') and 'goldcraft_scenario_admin.amxx 4' in (configs/'goldcraft/game_modes.ini').read_text(), 'scenario mask 4 and ZP restrictions applied')
        check('fixture-password' in (target/'cs/cstrike/server.cfg').read_text() and 'public.example.invalid' in (target/'cs/cstrike/server.cfg').read_text(), 'server CFG credentials and endpoint retained')
        check(all(digest(target/r['path'])==r['sha256'] for r in manifest['files']), 'all actual payload files installed with expected hashes')
        start_script=(target/'scripts/Start-Servers.ps1').read_text('utf-8-sig')
        legacy=re.sub(r'(?ms)^# BEGIN BINGSANG SCENARIO\n.*?^# END BINGSANG SCENARIO\n','',start_script)
        check(legacy==(scripts/'Start-Servers.ps1').read_text('utf-8-sig'), 'original dual-server branch retained verbatim')
        check(not any(r['path'].startswith('minecraft/') for r in manifest['files']), 'no generated Minecraft dimension or Minecraft payload')
        env=dict(os.environ);env.pop('PSModulePath',None)
        native=subprocess.run(['powershell.exe','-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',
            str(target/'scripts/Start-Servers.ps1'),'-CheckOnly'],capture_output=True,timeout=20,env=env)
        (output/'native-check.log').write_bytes(native.stdout+native.stderr)
        check(native.returncode==0 and b'Native scenario configuration valid' in native.stdout,
              'original startup selects native branch without Minecraft settings, runtime or dimension')
        check(snapshot()==installed, 'native CheckOnly launches nothing and writes nothing')
        run()
        check(snapshot()==installed, 'second full update copies zero files and changes no settings')
        run('-Restore')
        check(snapshot()==initial, 'explicit restore returns all bytes and original ZP mode')
        # Installation targets may be shared by a resource deduplication policy.
        alias=output/'immutable-owner-engine.dll'
        os.link(target/'cs/swds.dll',alias)
        run()
        check(alias.read_text()=='old engine' and digest(target/'cs/swds.dll')!=digest(alias), 'linked destination atomically detached without altering other owner')
        result['passed']=True
        print(json.dumps({k:v for k,v in result.items() if k!='checks'},indent=2))
        print(len(result['checks']), 'installer behavior checks passed')
    finally:
        (output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n','utf-8')


if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('package','inputs','output','scripts'):
        p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    main(a.package.resolve(),a.inputs.resolve(),a.output.resolve(),a.scripts.resolve())
