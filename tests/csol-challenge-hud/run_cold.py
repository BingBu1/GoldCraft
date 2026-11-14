#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Cold-check authored HUD resources on the unused isolated scenario server."""
import argparse
import csv
import hashlib
import io
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from win32_processes import processes
import csol_hud_resources


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('mp','engine','output','resource-plan'):parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--pdb',type=Path)
    parser.add_argument('--challenge-environment',action='store_true',
                        help='Read the actual environment service/profile through its matching PDB')
    args=parser.parse_args();out=args.output.resolve();game=ROOT/'sandbox/cs-dione-probe/Half-Life';cstrike=game/'cstrike'
    if args.challenge_environment and not args.pdb:raise ValueError('Environment inspection requires the exact candidate PDB')
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
    map_ids={int(row[0])for row in rows[1:]if len(row)>58 and row[2].strip()=='zs_deadend'and row[58].strip()=='1'}
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
    config=cstrike/'addons/goldcraft/config/gameplay.json';settings=json.loads(config.read_text('utf-8-sig'))
    settings['features']['time_cycle']['enabled']=True
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
        child=subprocess.Popen([str(game/'hlds.exe'),'-game','cstrike','-insecure','-nomaster','-console','-condebug','+ip','127.0.0.1','-port',str(cs_port),'-maxplayers','32','-csol_game_mode','2','-csol_scenario_map_id',str(map_id),'+map','zs_deadend'],cwd=game,env=env,startupinfo=startup,creationflags=subprocess.CREATE_NEW_CONSOLE)
        receipt['pid']=child.pid;deadline=time.monotonic()+45
        while time.monotonic()<deadline:
            if child.poll()is not None:raise RuntimeError('isolated server exited before ready')
            try:
                status=rcon.command('status',config_path=private)
                if 'zs_deadend'in status and '0 active'in status:break
            except (TimeoutError,ConnectionResetError,ConnectionRefusedError):pass
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
                import re,struct
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
                        originalLightProfile=(actual['dusk'],actual['night'],actual['step'])==(105,99,2.5),
                        originalFogProfile=actual['densityBits']=='3480373a'and actual['color']==[1,1,1]and
                            actual['start']==100 and actual['end']==2000 and actual['sky'])
                receipt['nativeRules']=native
            finally:process.close()
        receipt['checks'].update(mode2='"2"'in receipt['responses']['mode'],timeEnabled='"1"'in receipt['responses']['time'],slots32='32 max'in receipt['responses']['status'],noPlayers='0 active'in receipt['responses']['status'],difficultyReady='1..6'in receipt['responses']['difficulty'])
        receipt['http']=[]
        for name in ('resource/goldcraft/challenge/night_blood_v1.tga','sound/vox/man/sprint_end_default.wav','sound/vox/woman/sprint_end_default.wav'):
            data=urllib.request.urlopen(f'http://127.0.0.1:{http_port}/goldcraft/'+name,timeout=10).read()
            expected=cstrike/name
            assert hashlib.sha256(data).hexdigest()==sha(expected)
            receipt['http'].append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
        receipt['checks']['threeActualHttpHashes']=len(receipt['http'])==3
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
