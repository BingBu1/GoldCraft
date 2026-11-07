"""Run the delivered updater in WinPS 5.1 against real issued payload bytes.

Only this test's directory is a server target. No game or server is started.
"""
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

ROOT=Path(__file__).resolve().parents[2]
STATE='.bingsang-current-update'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main(package,plan_path,output,fixture=None,continuation=False):
    package,plan_path,output=map(lambda p:p.resolve(),(package,plan_path,output))
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh owned test directory')
    output.mkdir(parents=True)
    target=fixture.resolve() if fixture else output/'server with spaces'
    if not target.is_relative_to(ROOT/'build') or target.is_symlink() or target.is_junction():
        raise ValueError('Fixture must remain in the workspace build directory')
    target.mkdir(exist_ok=True)
    plan=json.loads(plan_path.read_text('utf-8'))
    manifest=json.loads((package/'update-manifest.json').read_text('utf-8'))
    report=dict(passed=False,checks=[],calls=[],realRuntimeModified=False,continuation=continuation,
                inputHashes={name:sha(package/name) for name in ('Update-Server.ps1','Files.ps1','Scenario.ps1','Configuration.ps1','Publication.ps1','update-manifest.json')})
    archives={}

    def put(name,data):
        path=target/name;path.parent.mkdir(parents=True,exist_ok=True)
        temp=path.with_name(path.name+'.fixture.tmp')
        temp.write_bytes(data.encode() if isinstance(data,str) else data)
        os.replace(temp,path)

    def old_bytes(name,digest):
        row=plan['history'][name.lower()][digest]
        if row['archive'] not in archives:archives[row['archive']]=zipfile.ZipFile(row['archive'])
        z=archives[row['archive']]
        data=z.read(row['member'])
        if hashlib.sha256(data).hexdigest()!=digest:
            raise ValueError('Issued baseline payload changed')
        return data

    def check(value,label):
        if not value: raise AssertionError(label)
        report['checks'].append(label);print('PASS '+label,flush=True)

    def snapshot():
        return {p.relative_to(target).as_posix():(sha(p),p.stat().st_mtime_ns)
                for p in target.rglob('*') if p.is_file() and STATE not in p.parts}

    def run(*args,success=True,cmd=False,root=target):
        env=dict(os.environ);env.pop('PSModulePath',None)
        if cmd:
            command='cmd.exe /d /s /c ""'+str(package/'Update-Server.cmd')+'" -TargetDirectory "'+str(root)+'" '+' '.join(args)+'"'
        else:
            command=['powershell.exe','-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',str(package/'Update-Server.ps1'),'-TargetDirectory',str(root),*args]
        start=time.monotonic()
        result=subprocess.run(command,input=b'\r\n',capture_output=True,timeout=600,env=env,creationflags=subprocess.CREATE_NO_WINDOW)
        log=output/f'call-{len(report["calls"])+1:02}.log';log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(args=args,cmd=cmd,seconds=round(time.monotonic()-start,3),exitCode=result.returncode,log=log.name))
        print(json.dumps(report['calls'][-1]),flush=True)
        if (result.returncode==0)!=success:
            raise AssertionError(log.read_text(errors='replace')[-3500:])
        return result.stdout.decode(errors='replace')

    try:
        version=subprocess.check_output(['powershell.exe','-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()'])
        check(version.startswith(b'5.1.'),'Actual Windows PowerShell 5.1')
        for i,row in enumerate(manifest['requiredBaseline'],1):
            if not (target/row['path']).is_file() or sha(target/row['path'])!=row['sha256']:
                put(row['path'],old_bytes(row['path'],row['sha256']))
            if i%500==0: print('Seeded baseline '+str(i),flush=True)
        for row in manifest['files']:
            previous=plan['latest'].get(row['path'].lower())
            if previous:
                put(row['path'],old_bytes(row['path'],previous['sha256']))
        settings=dict(map='zs_deadend',botTotalPlayers=0,csolGameMode=2,csPort=27015,
                      csPublicPort=46344,steamPort=26900,bridgePort=25577,
                      httpPublicUrl='http://owner.example.invalid:28080/goldcraft/',
                      ownerExtension={'preserve':True})
        fixture={
            'Start.cmd':'fixture launcher, never execute',
            'Stop.cmd':'fixture stop, never execute',
            'cs/hlds.exe':'fixture marker, never execute',
            'server-settings.json':json.dumps(settings),
            'cs/cstrike/server.cfg':'hostname "Owner"\nrcon_password "fixture-only"\nmc_http_public_url "http://owner.example.invalid:28080/goldcraft/"\n',
            'cs/cstrike/game_init.cfg':'// owner comment\n',
            'cs/cstrike/addons/sypb/sypb.cfg':'// owner\nsypb_quota 0\n',
            'cs/cstrike/addons/amxmodx/configs/plugins.ini':'; owner note\ncustom.amxx debug\ngoldcraft.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/plugins-owner.ini':'addon_floating_damage.amxx\nowner_extra.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/goldcraft/game_modes.ini':'; owner policy\ncustom.amxx 7\n',
            'cs/cstrike/addons/amxmodx/configs/modules.ini':'reapi\n',
            'cs/cstrike/addons/amxmodx/configs/users.ini':'private-admin-fixture',
            '.state/secrets.json':'{"token":"private-fixture"}',
            '.state/database.env':'private-database-fixture',
            'minecraft/world/level.dat':'preserve save',
            'minecraft/mods/owner.jar':'preserve mod',
        }
        for name,data in fixture.items():put(name,data)
        for name in ('Run-Server.ps1','Start-Servers.ps1','Common.ps1'):
            put('scripts/'+name,(ROOT/'tools/server-package/scripts'/name).read_bytes())
        initial=snapshot()
        check(len(initial)>1850,'Complete omitted baseline reconstructed from issued ZIP bytes')
        if not continuation:
            run('-Plan',cmd=True)
            check(snapshot()==initial and not (target/STATE).exists(),'CMD preview makes no writes')
            # Missing baseline must fail explicitly before any backup or mutation.
            missing=target/manifest['requiredBaseline'][0]['path'];saved=missing.read_bytes();missing.unlink()
            text=run('-Plan',success=False)
            check(not (target/STATE).exists(),'Missing omitted baseline refuses before writes')
            put(manifest['requiredBaseline'][0]['path'],saved)
            # An existing unknown DLL may never be silently overwritten.
            old_engine=(target/'cs/swds.dll').read_bytes();put('cs/swds.dll',b'unknown engine')
            run(success=False);check(not (target/STATE).exists(),'Unknown engine baseline refused')
            put('cs/swds.dll',old_engine)
            kernel=ctypes.WinDLL('kernel32',use_last_error=True)
            kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
            kernel.CreateFileW.restype=wintypes.HANDLE;kernel.CloseHandle.argtypes=[wintypes.HANDLE]
            locked=kernel.CreateFileW(str(target/'cs/swds.dll'),0x80000000,1,None,3,0,None)
            if locked==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
            try:run(success=False)
            finally:kernel.CloseHandle(locked)
            check(not (target/STATE).exists(),'Real Windows DLL lock refuses before writes')
            # All corruption is confined to an independent mutable payload DLL.
            dll=package/'payload/cs/swds.dll';raw=dll.read_bytes();check(dll.stat().st_nlink==1,'Mutable package DLL is independent')
            try:
                dll.write_bytes(raw+b'corrupt');run('-Plan',success=False)
                check(not (target/STATE).exists(),'Corrupt payload refused')
            finally:dll.write_bytes(raw)
            unlisted=package/'payload/unlisted.txt'
            try:
                unlisted.write_text('fixture');run('-Plan',success=False)
                check(not (target/STATE).exists(),'Unlisted payload refused')
            finally:unlisted.unlink(missing_ok=True)
            mf=package/'update-manifest.json';original_manifest=mf.read_bytes()
            try:
                invalid=json.loads(original_manifest);invalid['files'][0]['path']='cs/../outside.dll'
                mf.write_text(json.dumps(invalid),'utf-8');run('-Plan',success=False)
                check(not (target/STATE).exists(),'Traversal path refused before any write')
            finally:mf.write_bytes(original_manifest)
        old_engine=(target/'cs/swds.dll').read_bytes()
        alias=output/'old-engine-alias.dll';os.link(target/'cs/swds.dll',alias)
        initial=snapshot()
        run(cmd=True)
        installed=snapshot()
        check(all(sha(target/r['path'])==r['sha256'] for r in manifest['files']),'Every installed payload matches its manifest SHA')
        protected=[n for n in fixture if n.startswith(('minecraft/','.state/')) or n.endswith('users.ini') or n in ('Start.cmd','Stop.cmd')]
        check(all(installed[n]==initial[n] for n in protected),'Accounts, database, admins, Minecraft and original launchers preserved')
        current=json.loads((target/'server-settings.json').read_text('utf-8-sig'))
        check(current==settings,'Public addresses, ports, HTTP and owner settings preserved')
        config=(target/'cs/cstrike/server.cfg').read_text('utf-8-sig')
        check('fixture-only' in config and 'owner.example.invalid' in config,'RCON and HTTP cvars retained')
        check('addon_floating_damage.amxx disabled' in (target/'cs/cstrike/addons/amxmodx/configs/plugins-owner.ini').read_text() and 'owner_extra.amxx' in (target/'cs/cstrike/addons/amxmodx/configs/plugins-owner.ini').read_text(),'Retired damage plugin disabled without losing owner plugins')
        check((target/'cs/cstrike/addons/amxmodx/configs/plugins.ini').read_text().startswith('goldcraft_game_mode.amxx'),'Mode gate precedes scenario plugins')
        check('-csol_game_mode 2 +csol_weapons 1' in (target/'scripts/Run-Server.ps1').read_text('utf-8-sig'),'Mode 2 configured before first map')
        check(sha(alias)==hashlib.sha256(old_engine).hexdigest() and os.stat(alias).st_ino!=os.stat(target/'cs/swds.dll').st_ino,'Atomic replacement leaves the other hard-link owner unchanged')
        journal=target/STATE/'20261107/transaction.json';journal_before=journal.read_bytes()
        if not continuation:
            run()
            check(snapshot()==installed and journal.read_bytes()==journal_before,'Repeated installation is idempotent, including backup journal')
            settings_path=target/'server-settings.json';installed_settings=settings_path.read_bytes()
            changed_settings=json.loads(installed_settings);changed_settings['map']='owner_map';changed_settings['botTotalPlayers']=4
            put('server-settings.json',json.dumps(changed_settings))
            run(success=False)
            check(journal.read_bytes()==journal_before and json.loads(settings_path.read_text())==changed_settings,'Reapplying after an operator settings edit refuses and preserves the first complete rollback')
            run('-Restore',success=False)
            check(journal.read_bytes()==journal_before,'Restore protects operator settings until explicitly reconciled')
            put('server-settings.json',installed_settings)
            # Refuse restoration over an operator edit, then restore all real files.
            changed=target/'cs/cstrike/server.cfg';saved=changed.read_bytes();put('cs/cstrike/server.cfg',saved+b'// operator edit\n')
            run('-Restore',success=False)
            check(changed.read_bytes().endswith(b'// operator edit\n'),'Rollback refuses to overwrite later operator edits')
            put('cs/cstrike/server.cfg',saved)
        run('-Restore')
        restored=snapshot()
        check({k:v[0] for k,v in restored.items()}=={k:v[0] for k,v in initial.items()},'Rollback restores all original bytes and removes added files')
        # Model interruption immediately after the first replacement, using the
        # actual installed journal and independent backups produced above.
        interrupted=json.loads(journal_before);interrupted['status']='prepared'
        journal.write_text(json.dumps(interrupted),'utf-8')
        first=next(r for r in interrupted['files'] if r['kind']=='payload')
        put(first['path'],(package/'payload'/first['path']).read_bytes())
        run()
        check(all(sha(target/r['path'])==r['sha256'] for r in manifest['files']),'Interrupted prepared transaction recovers and reapplies')
        run('-Restore')
        check({k:v[0] for k,v in snapshot().items()}=={k:v[0] for k,v in initial.items()},'Recovered transaction remains fully restorable')
        report['passed']=True
    finally:
        for archive in archives.values():archive.close()
        (output/'receipt.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps({'passed':report['passed'],'checks':len(report['checks']),'calls':len(report['calls'])}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True);p.add_argument('--plan',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--fixture',type=Path)
    p.add_argument('--continuation',action='store_true')
    a=p.parse_args();main(a.package,a.plan,a.output,a.fixture,a.continuation)
