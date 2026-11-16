#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold-check a scenario map and its HTTP resources on the isolated server."""
import argparse
import csv
import hashlib
import io
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.parse

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from win32_processes import processes
import csol_hud_resources
from csol_server_table_closure import runtime_resource_paths


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('mp','engine','output','resource-plan'):parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--pdb',type=Path)
    parser.add_argument('--settings-file',type=Path,
                        help='Optional existing workspace gameplay JSON for the isolated test; original runtime file is restored')
    parser.add_argument('--challenge-environment',action='store_true',
                        help='Read the actual environment service/profile through its matching PDB')
    parser.add_argument('--live-time-controls',action='store_true',
                        help='Start time disabled, then verify real CVAR edits in this same no-player map via matching PDB')
    parser.add_argument('--map',default='zs_deadend')
    parser.add_argument('--map-id',type=int,
                        help='Explicit current catalog id when multiple rows share one filename')
    parser.add_argument('--http-resource',action='append',default=[],
                        help='Additional map resource whose actual HTTP bytes must match the server')
    args=parser.parse_args();out=args.output.resolve();game=ROOT/'sandbox/cs-dione-probe/Half-Life';cstrike=game/'cstrike'
    if (args.challenge_environment or args.live_time_controls) and not args.pdb:
        raise ValueError('Native state inspection requires the exact candidate PDB')
    if not re.fullmatch(r'[a-z0-9_]{1,63}',args.map):raise ValueError('Invalid scenario map basename')
    if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('fresh workspace receipt required')
    if any(p.get('image')and str(game).casefold()in p['image'].casefold()for p in processes(('hlds.exe',))):
        raise ValueError('isolated server is in use')
    for path in (game,args.mp.resolve(strict=True),args.engine.resolve(strict=True)):
        if not path.resolve().is_relative_to(ROOT):raise ValueError('workspace-only cold test')
        if any(p.exists()and p.lstat().st_file_attributes&0x400 for p in (path,*path.parents)):
            raise ValueError('reparse path')
    out.mkdir();rollback=out/'rollback';rollback.mkdir();changes=[];child=None
    receipt=dict(success=False,sourceCopies=0,gameplay=False,guiInput=False,mainRuntimeTouched=False,checks={},responses={})
    catalog=cstrike/'resource/maplist.csv'
    rows=list(csv.reader(io.StringIO(catalog.read_bytes().removeprefix(b'\xef\xbb\xbf').decode('latin1'),newline='')))
    assert rows[0][0].strip()==';map_id'and rows[0][2].strip()=='map_filename'and rows[0][58].strip()=='SubType'
    map_ids={int(row[0])for row in rows[1:]if len(row)>58 and row[2].strip()==args.map and row[58].strip()=='1'}
    if args.map_id is not None:
        if args.map_id not in map_ids:raise ValueError('Selected id does not describe the requested scenario map')
        map_ids={args.map_id}
    assert len(map_ids)==1;map_id=map_ids.pop();receipt['mapId']=map_id;receipt['catalogSha256']=sha(catalog)
    console=game/'qconsole.log';old_console=rollback/'qconsole-before.log';console_moved=console_owned=False

    def replace(target,source):
        assert target.resolve().is_relative_to(game.resolve())and source.resolve().is_relative_to(ROOT)
        if any(p.exists()and p.lstat().st_file_attributes&0x400 for p in (target,*target.parents)):
            raise ValueError('reparse deployment target')
        index=len(changes);backup=rollback/str(index);before=sha(target)if target.exists()else None
        if before:shutil.copy2(target,backup)
        target.parent.mkdir(parents=True,exist_ok=True)
        temp=target.with_name(target.name+'.challenge-cold');assert not temp.exists();shutil.copy2(source,temp)
        assert sha(temp)==sha(source);os.replace(temp,target)
        changes.append(dict(target=str(target),before=before,backup=str(backup)if before else None))

    def free_port(kind):
        with socket.socket(socket.AF_INET,kind)as sock:sock.bind(('127.0.0.1',0));return sock.getsockname()[1]

    spec=importlib.util.spec_from_file_location('cold_rcon',ROOT/'tools/GoldSrc-Command.py');rcon=importlib.util.module_from_spec(spec);spec.loader.exec_module(rcon)
    cs_port,http_port=free_port(socket.SOCK_DGRAM),free_port(socket.SOCK_STREAM);token=secrets.token_hex(24)
    private=ROOT/'sandbox/cs-dione-probe/challenge-hud-cold-private.json'
    private.write_text(json.dumps(dict(csPort=cs_port,csRconToken=token))+'\n','utf8')
    config=cstrike/'addons/goldcraft/config/gameplay.json'
    settings_source=args.settings_file.resolve(strict=True)if args.settings_file else config
    if not settings_source.is_relative_to(ROOT):raise ValueError('Test settings must stay in the workspace')
    settings=json.loads(settings_source.read_text('utf-8-sig'))
    settings['features']['time_cycle']['enabled']=not args.live_time_controls
    (out/'gameplay.json').write_text(json.dumps(settings,ensure_ascii=False,indent=2)+'\n','utf8')
    (out/'server.cfg').write_text(f'hostname "GoldCraft isolated HUD cold"\nrcon_password "{token}"\nsv_lan 0\nlog off\nsv_logecho 0\nmp_timelimit 0\nmp_autokick 0\nsypb_quota 0\ncsol_weapons 1\ncsol_scenario_difficulty 1\nmc_http_download 1\nmc_http_bind "127.0.0.1"\nmc_http_port {http_port}\nmc_http_public_url "http://127.0.0.1:{http_port}/goldcraft/"\n','utf8')
    try:
        for row in json.loads(args.resource_plan.read_text('utf-8-sig'))['files']:
            source=Path(row['source']).resolve(strict=True);name=row['destination']
            if not source.is_relative_to(ROOT)or name.startswith('/')or '..'in Path(name).parts or ':'in name:raise ValueError('unsafe prepared resource')
            if sha(source)!=row['sha256']or source.stat().st_size!=row['bytes']:raise ValueError('prepared audio changed')
            target=cstrike/name
            if not target.exists()or sha(target)!=row['sha256']:replace(target,source)
        replace(cstrike/'dlls/mp.dll',args.mp);replace(game/'swds.dll',args.engine)
        replace(config,out/'gameplay.json');replace(cstrike/'server.cfg',out/'server.cfg')
        receipt['preflight']=csol_hud_resources.verify(cstrike);assert receipt['preflight']['success']
        if console.exists():os.replace(console,old_console);console_moved=True
        console_owned=True
        startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
        env={k:v for k,v in os.environ.items()if not k.startswith(('GOLDCRAFT_','METAHOOK_'))}
        env.update(GOLDCRAFT_HTTP_ENABLED='1',GOLDCRAFT_HTTP_BIND='127.0.0.1',GOLDCRAFT_HTTP_PORT=str(http_port),GOLDCRAFT_HTTP_PUBLIC_URL=f'http://127.0.0.1:{http_port}/goldcraft/')
        child=subprocess.Popen([str(game/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+ip','127.0.0.1','-port',str(cs_port),'-maxplayers','32','-csol_game_mode','2','-csol_scenario_map_id',str(map_id),'+map',args.map],cwd=game,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        receipt['pid']=child.pid;deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            if child.poll()is not None:raise RuntimeError('isolated server exited before ready')
            try:
                status=rcon.command('status',config_path=private)
                if args.map in status and '0 active'in status:break
            except (TimeoutError,ConnectionResetError,ConnectionRefusedError):pass
            except ValueError as error:
                if str(error)!='Unexpected GoldSrc RCON reply type':raise
            time.sleep(.25)
        else:raise TimeoutError('isolated server did not finish cold startup')
        for key,command in [('status','status'),('mode','csol_game_mode'),('time','csol_custom_time_cycle'),('deathmatch','deathmatch'),('coop','coop'),('mapId','csol_scenario_map_id'),('difficulty','amx_scenario_difficulty')]:
            receipt['responses'][key]=rcon.command(command,config_path=private)
        if args.pdb:
            sys.path.insert(0,str(ROOT/'tests/csol-dione'))
            spec=importlib.util.spec_from_file_location('cold_runtime',ROOT/'tests/csol-dione/inspect_runtime.py')
            inspect=importlib.util.module_from_spec(spec);spec.loader.exec_module(inspect)
            process=inspect.Process(child.pid)
            try:
                module=next(row for row in process.modules()if Path(row['path']).name.casefold()=='mp.dll')
                pe=inspect.reader.PE(module['path']);symbols=inspect.reader.Symbols(args.pdb,pe)
                if process.read(module['base']+pe.codeview['rva'],24).hex()!=pe.codeview['identity']:raise ValueError('actual cold module/PDB identity differs')
                u32=lambda address:int.from_bytes(process.read(address,4),'little')
                rules=u32(module['base']+symbols.global_rva('g_pGameRules'))
                globals_address=u32(module['base']+symbols.global_rva('gpGlobals'))
                vtable=u32(rules)if rules else 0
                native=dict(moduleSha256=pe.sha256,rules=rules,vtableRva=vtable-module['base'],globals=globals_address)
                import struct
                matches=re.findall(r'S_PUB32[^\n]*`([^`]+)`\s*\n[^\n]*addr = (\d+):(\d+)',symbols.dump('-publics'))
                native['vtableNames']=[name for name,section,offset in matches if name.startswith('??_7')and pe.sections[int(section)-1]['rva']+int(offset)==native['vtableRva']]
                layout=symbols.layout('globalvars_t')
                native['globalFields']={name:struct.unpack('<f',process.read(globals_address+layout['members'][name],4))[0]for name in ('deathmatch','coop')}
                def shared_object(address):return u32(address)
                def field(address,type_name,name):return address+symbols.layout(type_name)['members'][name]
                rules_layout=symbols.layout("`anonymous namespace'::CCSOLScenarioRules")
                runtime=shared_object(rules+rules_layout['members']['m_runtime'])
                native['runtime']=runtime
                runtime_layout=symbols.layout('CCSOLScenarioRuntime')
                session=shared_object(runtime+runtime_layout['members']['session'])
                native['session']=session
                native['runtimeFields']={name:process.read(runtime+offset,8).hex()for name,offset in runtime_layout['members'].items()if any(word in name.lower()for word in ('clos','gener','oper'))}
                session_layout=symbols.layout('CCSOLScenarioSession')
                native['sessionFields']={name:process.read(session+offset,8).hex()for name,offset in session_layout['members'].items()if any(word in name.lower()for word in ('state','error','map','ready'))}
                if args.challenge_environment:
                    environment=shared_object(runtime+runtime_layout['members']['environment'])
                    if not environment:raise ValueError('Actual scenario runtime has no environment owner')
                    env_layout=symbols.layout('CCSOLChallengeEnvironment')['members']
                    config=environment+env_layout['config_']
                    config_layout=symbols.layout('csol::scenario::custom::EnvironmentConfig')['members']
                    fog=config+config_layout['fog']
                    fog_layout=symbols.layout('csol::scenario::custom::FogProfile')['members']
                    engine_module=next(row for row in process.modules()if Path(row['path']).name.casefold()=='swds.dll')
                    lights=u32(environment+env_layout['lights_'])
                    actual=dict(address=environment,lights=lights,
                        prepared=bool(process.read(environment+env_layout['prepared_'],1)[0]),
                        revoked=bool(process.read(environment+env_layout['revoked_'],1)[0]),
                        message=u32(environment+env_layout['message_']),
                        dusk=process.read(config+config_layout['dusk'],1)[0],night=process.read(config+config_layout['night'],1)[0],
                        step=struct.unpack('<f',process.read(config+config_layout['stepSeconds'],4))[0],
                        densityBits=process.read(fog+fog_layout['density'],4).hex(),
                        color=list(process.read(fog+fog_layout['color'],3)),
                        start=struct.unpack('<f',process.read(fog+fog_layout['start'],4))[0],
                        end=struct.unpack('<f',process.read(fog+fog_layout['end'],4))[0],
                        sky=bool(process.read(fog+fog_layout['sky'],1)[0]))
                    receipt['environment']=actual
                    receipt['checks'].update(environmentPrepared=actual['prepared']and not actual['revoked'],
                        realEngineService=engine_module['base']<=lights<engine_module['base']+engine_module['size'],
                        fixedEnvironmentMessage=64<=actual['message']<=255,
                        configuredLightProfile=(actual['dusk'],actual['night'],actual['step'])==(105,98,2.5),
                        originalFogProfile=actual['densityBits']=='3480373a'and actual['color']==[1,1,1]and
                            actual['start']==100 and actual['end']==2000 and actual['sky'])
                receipt['nativeRules']=native
                if args.live_time_controls:
                    controls_type='csol::scenario::custom::TimeCycleControls'
                    duration_type='csol::scenario::custom::PhaseConfig'
                    controls=field(rules,"`anonymous namespace'::CCSOLScenarioRules",'m_timeControls')
                    duration=field(controls,controls_type,'duration')
                    enabled=field(controls,controls_type,'enabled')
                    noon=field(duration,duration_type,'noonSeconds')
                    dusk=field(duration,duration_type,'duskSeconds')
                    pointer=module['base']+symbols.global_rva('g_pGameRules')
                    original_config=sha(config)
                    initial_generation=process.read(runtime+runtime_layout['members']['m_generation'],8)
                    def snapshot():
                        if child.poll()is not None:raise RuntimeError('Owned time-control test server exited')
                        if u32(pointer)!=rules:raise ValueError('Time-control edit replaced the native rules object')
                        if process.read(runtime+runtime_layout['members']['m_generation'],8)!=initial_generation:
                            raise ValueError('Time-control edit replaced the map runtime generation')
                        return dict(enabled=bool(process.read(enabled,1)[0]),
                            noon=struct.unpack('<f',process.read(noon,4))[0],
                            dusk=struct.unpack('<f',process.read(dusk,4))[0],
                            invalidReported=bool(process.read(rules+rules_layout['members']['m_timeInvalidReported'],1)[0]),
                            prepared=bool(process.read(rules+rules_layout['members']['m_timePrepared'],1)[0]),
                            round=int.from_bytes(process.read(rules+rules_layout['members']['m_timeRound'],8),'little'))
                    initial=snapshot()
                    if initial['enabled']or not initial['prepared']:
                        raise ValueError('Initially disabled map did not cold-prepare the live time owner')
                    receipt['timeControlSamples']=[dict(step='initial-disabled',**initial)]
                    def edit(label,commands,expected):
                        for text in commands:
                            answer=rcon.command(text,config_path=private)
                            receipt.setdefault('timeControlCommands',[]).append(dict(command=text,output=answer))
                        deadline=time.monotonic()+5
                        while time.monotonic()<deadline:
                            actual=snapshot()
                            if all(actual[key]==value for key,value in expected.items()):break
                            time.sleep(.02)
                        else:raise TimeoutError('Current-map CVAR edit was not applied: '+label)
                        if sha(config)!=original_config:raise ValueError('Temporary CVAR edit rewrote persistent gameplay JSON')
                        receipt['timeControlSamples'].append(dict(step=label,**actual))
                    edit('enabled-current-map',['csol_custom_time_cycle 1'],dict(enabled=True))
                    edit('retimed-current-map',['csol_custom_midday_seconds 17','csol_custom_dusk_seconds 19'],
                        dict(enabled=True,noon=17,dusk=19))
                    edit('invalid-noon-refused',['csol_custom_midday_seconds -1'],
                        dict(enabled=True,noon=17,dusk=19,invalidReported=True))
                    edit('disabled-current-map',['csol_custom_midday_seconds 17','csol_custom_time_cycle 0'],
                        dict(enabled=False,invalidReported=False))
                    edit('reenabled-current-map',['csol_custom_time_cycle 1'],dict(enabled=True,noon=17,dusk=19))
                    current_status=rcon.command('status',config_path=private)
                    receipt['checks'].update(liveTimeControls=True,sameMapNoPlayers=args.map in current_status and '0 active'in current_status,
                        liveConfigNotPersisted=sha(config)==original_config,
                        noSyntheticRound=all(item['round']==0 for item in receipt['timeControlSamples']))
            finally:process.close()
        receipt['checks'].update(mode2='"2"'in receipt['responses']['mode'],
            timeEnabled=('"0"'if args.live_time_controls else'"1"')in receipt['responses']['time'],
            slots32='32 max'in receipt['responses']['status'],noPlayers='0 active'in receipt['responses']['status'],difficultyReady='1..6'in receipt['responses']['difficulty'])
        advertised={}
        if args.http_resource:
            for kind in ('model','sound','generic','event'):
                listing=rcon.command('reslist '+kind,config_path=private)
                receipt['responses']['reslist-'+kind]=listing
                for name in runtime_resource_paths(listing,kind):advertised[name.casefold()]=name
            missing=[name for name in sorted(advertised.values())if not any(
                (folder/name).is_file()and(folder/name).stat().st_size>0
                for folder in (cstrike,game/'cstrike_downloads',game/'valve'))]
            receipt['resourceAudit']=dict(advertised=len(advertised),missing=missing)
            if missing:raise ValueError('Advertised resources are missing or empty: '+', '.join(missing))
            receipt['checks']['allAdvertisedFilesPresent']=True
        receipt['http']=[]
        names=list(dict.fromkeys(['resource/goldcraft/challenge/night_blood_v1.tga','sound/vox/man/sprint_end_default.wav','sound/vox/woman/sprint_end_default.wav',*args.http_resource]))
        for name in names:
            if name.startswith('/')or'..'in Path(name).parts or any(c in name for c in '\\:'):
                raise ValueError('Unsafe HTTP resource path')
            if args.http_resource:
                if name.casefold() not in advertised:raise ValueError('Map did not advertise required resource: '+name)
                advertised_name=advertised[name.casefold()]
            else:advertised_name=name
            receipt['lastHttpRequest']=dict(requested=name,advertised=advertised_name)
            data=urllib.request.urlopen(f'http://127.0.0.1:{http_port}/goldcraft/'+urllib.parse.quote(advertised_name,safe='/'),timeout=10).read()
            expected=cstrike/name
            assert hashlib.sha256(data).hexdigest()==sha(expected)
            receipt['http'].append(dict(path=name,advertised=advertised_name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
        receipt['checks']['actualHttpHashes']=len(receipt['http'])==len(names)
        receipt['success']=all(receipt['checks'].values())
    except BaseException as error:
        receipt['error']=type(error).__name__+': '+str(error).replace(token,'[redacted]')
    finally:
        if child and child.poll()is None:
            try:rcon.command('quit',config_path=private)
            except Exception:pass
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.terminate();child.wait(timeout=5)
        receipt['restored']=True
        for row in reversed(changes):
            target=Path(row['target'])
            if row['backup']:
                temp=target.with_name(target.name+'.challenge-restore');shutil.copy2(row['backup'],temp);os.replace(temp,target)
                receipt['restored']&=sha(target)==row['before']
            elif target.exists():target.unlink()
        receipt['success']&=receipt['restored'];receipt['changes']=changes
        if console_owned and console.exists():
            diagnostic=console.read_text('utf8',errors='replace').replace(token,'[redacted]')
            (out/'console.log').write_text(diagnostic,'utf8');console.unlink()
        if console_moved:os.replace(old_console,console)
        (out/'receipt.json').write_text(json.dumps(receipt,ensure_ascii=True,indent=2)+'\n','utf8')
    print(json.dumps({k:receipt.get(k)for k in ('success','restored','checks','error')}))
    if not receipt['success']:raise SystemExit(1)


if __name__=='__main__':main()
