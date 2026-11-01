"""Actual WinPS5.1 transactions with all78 payloads and a reduced baseline fixture.

The delivered manifest retains all1429 omitted resources. Only this isolated
fixture reduces those pre-existing files to3 representative records; it never
claims to have applied the complete manifest or updated a real server.
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

ROOT = Path(__file__).resolve().parents[2]
STATE = '.bingsang-scenario-respawn-update'


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main(candidate, resource_plan, baseline_audit, output):
    if output.exists() or not output.is_relative_to(ROOT/'build/csol-respawn-delivery-20261101'):
        raise ValueError('Use a fresh owned test directory')
    output.mkdir(parents=True)
    package=output/'update with spaces';target=output/'server with spaces'
    shutil.copytree(candidate,package)
    production_manifest_sha=digest(candidate/'update-manifest.json')
    original_manifest=json.loads((candidate/'update-manifest.json').read_text('utf-8-sig'))
    manifest=json.loads(json.dumps(original_manifest))
    chosen={'cs/cstrike/resource/maplist.csv','cs/cstrike/bgm/Scenario_Ready.mp3','cs/cstrike/models/csol/characters.json'}
    manifest['requiredBaseline']=[row for row in manifest['requiredBaseline'] if row['path'] in chosen]
    assert len(manifest['requiredBaseline'])==3 and len(manifest['files'])==78
    manifest['testFixtureOnly']='Pre-existing resource subset; full production manifest is unchanged'
    manifest_path=package/'update-manifest.json'
    manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),'utf-8')
    resources={row['path']:row for row in json.loads(resource_plan.read_text('utf-8-sig'))['requiredBaseline']}
    prior=json.loads(baseline_audit.read_text('utf-8-sig'))
    old={row['path']:row for row in prior['effectiveFiles']}
    archive_identities={row['identity']['path']:row['identity']['sha256'] for row in prior['archives']}
    report=dict(passed=False,checks=[],calls=[],fixtureBaselineFiles=3,productionBaselineFiles=1429,
                completePayloadFiles=78,fullProductionManifestApplied=False,liveRuntimeChanged=False)
    state=target/STATE;journal_path=state/'transaction.json';saved=state/'files/cs/cstrike/dlls/mp.dll'
    mp=target/'cs/cstrike/dlls/mp.dll';engine=target/'cs/swds.dll'
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]

    def hold(path,share=0):
        handle=kernel.CreateFileW(str(path),0x80000000,share,None,3,0,None)
        if handle==ctypes.c_void_p(-1).value:raise ctypes.WinError(ctypes.get_last_error())
        return handle

    def check(condition,text):
        if not condition:raise AssertionError(text)
        report['checks'].append(text)
        print(json.dumps({'check':len(report['checks']),'passed':text}),flush=True)

    def put(name,data):
        path=target/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data.encode('utf-8') if isinstance(data,str) else data)

    def snapshot():
        return {p.relative_to(target).as_posix():digest(p) for p in target.rglob('*')
                if p.is_file() and STATE not in p.parts}

    def state_snapshot():
        return {p.relative_to(state).as_posix():(digest(p),p.stat().st_mtime_ns) for p in state.rglob('*') if p.is_file()}

    def call(*args,success=True,selected=None,cmd=False):
        env=dict(os.environ);env.pop('PSModulePath',None)
        directory=selected or target
        if cmd:
            command=f'cmd.exe /d /s /c ""{package / "Update-Server.cmd"}" -TargetDirectory "{directory}" '+ ' '.join(args)+'"'
        else:
            command=['powershell.exe','-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',
                     str(package/'Update-Server.ps1'),'-TargetDirectory',str(directory),*args]
        start=time.monotonic()
        result=subprocess.run(command,input=b'\r\n',capture_output=True,timeout=60,env=env,
                              creationflags=subprocess.CREATE_NO_WINDOW)
        log=output/f'call-{len(report["calls"])+1:02}.log';log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(args=list(args),cmd=cmd,exitCode=result.returncode,
                                   seconds=round(time.monotonic()-start,3),log=log.name))
        if (result.returncode==0)!=success:
            raise AssertionError(result.stdout.decode(errors='replace')[-2000:]+result.stderr.decode(errors='replace')[-2000:])
        return result

    def baseline_bytes(name):
        row=old[name];archive=Path(row['archive'])
        assert digest(archive)==archive_identities[str(archive)]
        with zipfile.ZipFile(archive) as package_file:data=package_file.read(row['member'])
        assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
        return data

    try:
        version=subprocess.run(['powershell.exe','-NoProfile','-Command','$PSVersionTable.PSVersion.ToString()'],
                               capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
        check(version.returncode==0 and version.stdout.startswith(b'5.1.'),'Actual Windows PowerShell5.1')
        report['powershellVersion']=version.stdout.decode().strip()
        mp_old=baseline_bytes('cs/cstrike/dlls/mp.dll');engine_fixed=baseline_bytes('cs/swds.dll')
        put('Start.cmd','original launcher; not executed');put('Stop.cmd','original stopper; not executed')
        put('cs/hlds.exe','shape-only marker; never executed')
        put('server-settings.json','{"map":"zs_deadend","csolGameMode":2,"owner":"preserve"}')
        put('cs/cstrike/dlls/mp.dll',mp_old);put('cs/swds.dll',engine_fixed)
        put('cs/cstrike/server.cfg','hostname "owner"\nrcon_password "fixture-only"\n')
        put('cs/cstrike/addons/amxmodx/configs/plugins.ini','stable-owner.amxx\n')
        put('cs/cstrike/addons/amxmodx/configs/users.ini','private-fixture')
        put('cs/launcher-public/manifest.json','existing signed manifest marker')
        put('minecraft/world/level.dat','unchanged save marker');put('.state/credentials.json','private fixture marker')
        for row in manifest['requiredBaseline']:
            source=Path(resources[row['path']]['source'])
            assert digest(source)==row['sha256']
            put(row['path'],source.read_bytes())
        initial=snapshot();report['fixtureInitialBytes']=sum((target/name).stat().st_size for name in initial)
        check(all(digest(package/name)==digest(candidate/name) for name in ['Update-Server.ps1','Files.ps1','Update-Server.cmd','Restore-Server.cmd']),
              'Fixture executes byte-identical production installer and shared atomic/rollback helpers')
        check(manifest['files']==original_manifest['files'] and len(original_manifest['requiredBaseline'])==1429,
              'All78 actual payloads retained; only fixture existing-baseline list reduced')
        call('-Plan');check(snapshot()==initial and not state.exists(),'Preview writes no target or state files')
        call('-Plan',cmd=True);check(snapshot()==initial,'Actual CMD entry works with spaces and preserves target')
        for path,original,label in [(mp,mp_old,'unknown mp.dll'),(engine,engine_fixed,'old or unknown FastDL engine')]:
            path.write_bytes(b'unknown fixture version');before=snapshot()
            call(success=False);check(snapshot()==before and not state.exists(),label+' rejects before writes')
            path.write_bytes(original)
        required=target/manifest['requiredBaseline'][0]['path'];data=required.read_bytes();required.write_bytes(b'wrong baseline')
        before=snapshot();call(success=False);check(snapshot()==before and not state.exists(),'Wrong omitted resource rejects before writes');required.write_bytes(data)
        first=next(row for row in manifest['files'] if row['path']!='cs/cstrike/dlls/mp.dll')
        payload=package/'payload'/first['path'];data=payload.read_bytes();payload.write_bytes(data+b'bad')
        call(success=False);payload.write_bytes(data)
        check(snapshot()==initial and not state.exists(),'Corrupt payload rejects before target mutation')
        extra=package/'payload/cs/cstrike/addons/amxmodx/plugins/goldcraft_scenario_respawn_test.amxx'
        extra.parent.mkdir(parents=True);extra.write_bytes(b'temporary test plugin')
        call(success=False);extra.unlink()
        check(snapshot()==initial and not state.exists(),'Unlisted temporary Pawn payload rejects')
        manifest_bytes=manifest_path.read_bytes()
        altered=json.loads(manifest_bytes);altered['files'][0]['path']='../escape.dll'
        manifest_path.write_text(json.dumps(altered),'utf-8');call(success=False);manifest_path.write_bytes(manifest_bytes)
        check(snapshot()==initial and not state.exists(),'Manifest traversal rejects without writes')
        put(first['path'],b'owner changed resource');before=snapshot();call(success=False)
        check(snapshot()==before and not state.exists(),'Different existing new resource is preserved and rejects update')
        (target/first['path']).unlink()
        handle=hold(mp,share=1)
        try:call(success=False)
        finally:kernel.CloseHandle(handle)
        journal=json.loads(journal_path.read_text('utf-8-sig'))
        check(snapshot()==initial and journal['status']=='restored' and len(journal['files'])==78,
              'Real locked final DLL causes rollback after resource writes; all original bytes restored')
        check(saved.stat().st_nlink==1 and digest(saved)==manifest['baselineMpSha256'],
              'Rollback DLL is an independent verified old binary')
        call();installed=snapshot()
        check(all(digest(target/row['path'])==row['sha256'] for row in manifest['files']),'All78 actual payload identities installed')
        expected=dict(initial);expected.update({row['path']:row['sha256'] for row in manifest['files']})
        check(installed==expected,'Only listed mp/resources change; FastDL, config, Pawn, credentials, signature and save preserved')
        check(mp.stat().st_nlink==1 and saved.stat().st_nlink==1 and not os.path.samefile(mp,package/'payload/cs/cstrike/dlls/mp.dll'),
              'Installed and backup DLLs remain independent files')
        stable_state=state_snapshot();call();call('-Plan')
        check(snapshot()==installed and state_snapshot()==stable_state,'Repeat apply/preview preserve runtime and rollback content/timestamps')
        # Only immutable resource files are linked; no DLL or configuration link.
        shared=target/first['path'];alias=output/'immutable-resource-owner.tga'
        assert digest(shared)==first['sha256'];os.link(shared,alias)
        check(os.path.samefile(shared,alias),'Verified immutable resource alias created within owned test workspace')
        call('-Restore');check(snapshot()==initial and digest(alias)==first['sha256'],'Restore removes only installed resource link and returns exact original runtime')
        before_restore=state_snapshot();call('-Restore')
        check(snapshot()==initial and state_snapshot()==before_restore,'Repeated restore is idempotent')
        handle=hold(state/'operation.lock')
        try:call(success=False)
        finally:kernel.CloseHandle(handle)
        check(snapshot()==initial,'Concurrent updater lock prevents installation')
        call();backup=saved.read_bytes();saved.write_bytes(b'corrupt backup')
        call('-Restore',success=False);saved.write_bytes(backup)
        check(snapshot()==installed,'Corrupt backup rejects restore without runtime mutation')
        changed=target/first['path'];data=changed.read_bytes();changed.write_bytes(b'user edited after update')
        before=snapshot();call('-Restore',success=False)
        check(snapshot()==before,'External resource modification blocks rollback and remains intact');changed.write_bytes(data)
        journal=json.loads(journal_path.read_text('utf-8-sig'));journal['status']='prepared'
        journal_path.write_text(json.dumps(journal),'utf-8')
        call('-Restore');check(snapshot()==initial,'Prepared interrupted commit restores exact old runtime')
        call();journal=json.loads(journal_path.read_text('utf-8-sig'));journal['status']='prepared'
        journal_path.write_text(json.dumps(journal),'utf-8');mp.write_bytes(mp_old)
        call();check(snapshot()==installed,'Prepared partial commit recovers then cleanly reapplies')
        call('-Restore')
        supervisor=subprocess.Popen(['powershell.exe','-NoProfile','-Command',
            "$marker='"+str(target/'supervisor').replace("'","''")+"'; Write-Output 'ready'; Start-Sleep -Seconds 45"],
            stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            assert supervisor.stdout.readline().strip()==b'ready'
            call(success=False)
        finally:supervisor.terminate();supervisor.wait(timeout=10)
        check(snapshot()==initial,'Running target supervisor prevents update')
        junction=output/'server-junction'
        made=subprocess.run(['cmd.exe','/d','/c','mklink','/J',str(junction),str(target)],capture_output=True,timeout=10,
                            creationflags=subprocess.CREATE_NO_WINDOW)
        check(made.returncode==0,'Owned junction fixture created')
        try:call('-Plan',success=False,selected=junction)
        finally:os.rmdir(junction)
        check(snapshot()==initial,'Junction target rejected without changing real target')
        call('-Plan',success=False,selected=package)
        check(snapshot()==initial,'Selecting the update directory as target is rejected')
        check(digest(candidate/'update-manifest.json')==production_manifest_sha,
              'Original full production manifest remains unchanged')
        report['passed']=True
        report['installerSha256']=digest(package/'Update-Server.ps1')
        report['productionManifestSha256']=digest(candidate/'update-manifest.json')
        report['fixtureManifestSha256']=digest(manifest_path)
        print(json.dumps({'passed':True,'checks':len(report['checks']),'calls':len(report['calls']),
                          'initialFixtureBytes':report['fixtureInitialBytes']}),flush=True)
    finally:
        (output/'result.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n','utf-8')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('candidate','resource-plan','baseline-audit','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    main(args.candidate.resolve(),args.resource_plan.resolve(),args.baseline_audit.resolve(),args.output.resolve())
