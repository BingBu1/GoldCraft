"""Single-job actual SDK objects and full production client behavior fixture."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('action',choices=('prepare','execute'))
parser.add_argument('output',help='Fresh subdirectory name under build/csol-melee-20261102')
parser.add_argument('--compiler',type=Path,help='Path to the project Clang compiler; required for prepare')
parser.add_argument('--environment',type=Path,default=ROOT/'build/csol-frozen-death-20261101/fresh-combination-01/environment.json')
parser.add_argument('--reuse',help='Prior pass whose successful leading server TU checks can be reused')
parser.add_argument('--skip',type=int,default=0)
parser.add_argument('--purchase',action='store_true',help='Verify knife transaction, actual SDK inventory methods and public service forwarding')
args=parser.parse_args()
if args.action == 'prepare' and args.compiler is None:
    parser.error('--compiler is required for prepare')
COMPILER = args.compiler.resolve() if args.compiler else None
LINKER = COMPILER.with_name('lld-link.exe') if COMPILER else None
ENVFILE = args.environment.resolve()
BUILD = ROOT / 'build/csol-melee-20261102'
OUT = (BUILD / args.output).resolve()
if not OUT.is_relative_to(BUILD) or OUT==BUILD:raise ValueError('Output must be a child directory of the melee build area')
NATIVE = ROOT / 'native/csol'
SERVER = ROOT / 'external/ReGameDLL_CS/regamedll'
METAHOOK = ROOT / 'external/MetaHookSv-20261007'
SDK = METAHOOK / 'MetaHook/include'
TASK = ['melee_attacks.cpp', 'melee_attacks.hpp', 'melee_damage.cpp', 'melee_damage.hpp',
        'melee_identity.hpp', 'melee_prediction.hpp', 'melee_resources.hpp',
        'server/weapons/melee_trace.cpp', 'server/weapons/melee_trace.h', 'server/weapons/wpn_melee.cpp', 'server/weapons/wpn_melee.h',
        'client/weapons/melee.cpp', 'client/weapons/melee.hpp', 'client/weapons/weapons.cpp',
        'server/weapons/weapons.h', 'server/weapons/wpn_tknifeex2.cpp', 'server/scenario/monsters/scenario_monster_damage.cpp',
        'server/scenario/core/scenario_player_runtime.cpp', 'server/weapons/scenario_weapon_state.cpp',
        'server/scenario/core/game_rules_gate.cpp', 'server/scenario/core/game_rules_gate.h']

def identity(p):
    s = p.stat()
    return dict(path=str(p), bytes=s.st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())

def prepare():
    assert not OUT.exists()
    source = OUT / 'source'
    source.mkdir(parents=True)
    task_files = ['server/weapons/melee_purchase.cpp','server/weapons/melee_purchase.h','server/weapons/weapon_service.cpp',
        'server/weapons/wpn_melee.cpp','server/weapons/wpn_melee.h','melee_identity.hpp','melee_resources.hpp',
        'melee_attacks.hpp','melee_prediction.hpp'] if args.purchase else TASK
    files = [NATIVE / name for name in task_files]
    if args.purchase:
        files += [ROOT/'tests/csol-melee'/name for name in ('purchase.cpp','prepare_purchase.py')]
        files += [ROOT/'tests/csol-cannon-server'/name for name in ('purchase_backup.cpp','extract_purchase_backup.py')]
        files += [SERVER/'dlls'/name for name in ('player.cpp','player.h','weapons.cpp','ehandle.h')]
    else:
        files += [ROOT/'tests/csol-melee'/name for name in ('client.cpp','wire.hpp')]
        files += [SERVER/'dlls/client.cpp']
    inputs = {}
    overlay = {}
    for original in files:
        target = source / original.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        assert target.stat().st_nlink == 1 and target.stat().st_ino != original.stat().st_ino
        assert original.read_bytes() == target.read_bytes()
        for p in (original, target): inputs[str(p).lower()] = identity(p)
        overlay[str(original).lower()] = str(target)
    if args.purchase:
        spec=importlib.util.spec_from_file_location('purchase_prepare',source/'tests/csol-melee/prepare_purchase.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        generated=module.prepare(ROOT,source)
        subprocess.run([sys.executable,str(source/'tests/csol-cannon-server/extract_purchase_backup.py'),
            str(source),str(source/'tests/csol-cannon-server')],check=True,capture_output=True)
        generated += [source/'tests/csol-cannon-server'/name for name in ('purchase_backup.inc','inputs.json')]
        for path in generated:
            inputs[str(path).lower()]=identity(path)
            overlay[str(ROOT/path.relative_to(source)).lower()]=str(path)
    delta_source=SERVER.parent/'dist/delta.lst'
    delta_tool=ROOT/'tools/Prepare-CSOLWeaponDelta.py'
    spec=importlib.util.spec_from_file_location('weapon_delta',delta_tool)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    delta=module.expand(delta_source.read_bytes()).decode('latin1')
    body=re.search(r'(?m)^weapon_data_t\s+[^\r\n]+\s*\{([^}]*)\}',delta)[1]
    fields=re.findall(r'DEFINE_DELTA\([^)]*\)',body)
    field_file=source/'tests/csol-melee/delta_fields.inc'
    field_file.write_text(',\n'.join(fields)+'\n',encoding='utf-8')
    for path in (delta_source,delta_tool,field_file): inputs[str(path).lower()]=identity(path)
    overlay[str(ROOT/'tests/csol-melee/delta_fields.inc').lower()]=str(field_file)
    overlay_file=OUT/'source-overlay.json'
    overlay_file.write_text(json.dumps({'version':0,'case-sensitive':False,'use-external-names':False,
        'roots':[{'type':'file','name':name,'external-contents':path} for name,path in overlay.items()]},indent=2)+'\n')
    inputs[str(overlay_file).lower()]=identity(overlay_file)
    environment = json.loads(ENVFILE.read_text())
    server_includes = [ROOT/'native/include', NATIVE/'server'] + [SERVER/s for s in
        ['', 'hookers', 'common', 'dlls', 'engine', 'public', 'public/regamedll', 'pm_shared', 'regamedll', 'testsuite']]
    client_includes = [NATIVE/'client'] + [SDK/s for s in ['', 'Interface', 'HLSDK/cl_dll', 'HLSDK/common',
        'HLSDK/engine', 'HLSDK/pm_shared', 'HLSDK/public', 'SourceSDK', 'SourceSDK/tier0', 'SourceSDK/tier1']]
    client_includes += [METAHOOK/'MetaHook/thirdparty/capstone_fork/include',
        METAHOOK/'MetaHook/thirdparty/rapidjson/include', METAHOOK/'thirdparty/glew_fork/include',
        METAHOOK/'Plugins/Renderer/include/Interface']
    server_includes += [METAHOOK/'MetaHook/thirdparty/rapidjson/include']
    # Record before compilation and verify after. Shared SDK headers are read
    # in place, not copied or claimed as a frozen historical SDK tree.
    roots = [NATIVE, ROOT/'native/include', ROOT/'native/server', SERVER, SDK, *client_includes[-4:]]
    system_roots = [Path(p) for p in environment.get('INCLUDE','').split(';') if p]
    system_roots += [COMPILER.parent.parent/'lib/clang/22/include']
    roots += system_roots
    for root in roots:
        if not root.exists(): continue
        for p in root.rglob('*'):
            if p.is_file() and (root in system_roots or p.suffix.lower() in ('.h','.hpp','.inl','.inc')):
                key=str(p).lower()
                if key not in inputs: inputs[key]=identity(p)
    for p in (COMPILER, LINKER, ENVFILE): inputs[str(p).lower()] = identity(p)
    flags = ['/nologo','/TP','--target=i686-pc-windows-msvc','/EHsc','/O2','/clang:-O3','/clang:-flto=thin',
        '/arch:AVX2','/fp:precise','/DNDEBUG','/std:c++20','/MT','/utf-8','/W3','/showIncludes','/D_CRT_SECURE_NO_WARNINGS']
    flags += ['/clang:-ivfsoverlay','/clang:'+str(overlay_file)]
    server_definitions = ['REGAMEDLL_ADD','REGAMEDLL_API','REGAMEDLL_FIXES','REGAMEDLL_SSE','REGAMEDLL_SELF',
        'REGAMEDLL_CHECKS','UNICODE_FIXES','BUILD_LATEST','BUILD_LATEST_FIXES','CLIENT_WEAPONS','USE_QSTRING','_WINDLL','_MBCS']
    client_definitions = ['WIN32','_WINDOWS','_USRDLL','_MBCS','NO_MALLOC_OVERRIDE','NOMINMAX','GLEW_STATIC','GLEW_NO_GLU']
    commands = []
    server_tasks = ['server/weapons/melee_purchase.cpp','server/weapons/weapon_service.cpp','server/weapons/wpn_melee.cpp'] if args.purchase else ['server/weapons/melee_trace.cpp','server/weapons/wpn_melee.cpp','server/weapons/wpn_tknifeex2.cpp',
                 'server/scenario/monsters/scenario_monster_damage.cpp','server/scenario/core/scenario_player_runtime.cpp',
                 'server/weapons/scenario_weapon_state.cpp','server/scenario/core/game_rules_gate.cpp']
    for name in server_tasks:
        commands.append([str(COMPILER),*flags,'/Zc:strictStrings-',*['/D'+x for x in server_definitions],
            *['-I'+str(p) for p in server_includes],'/c',str(source/'native/csol'/name),'/Fo'+str(OUT/(Path(name).stem+'.obj'))])
    if args.purchase:
        for folder,stem in [('csol-melee','purchase'),('csol-cannon-server','purchase_backup')]:
            commands.append([str(COMPILER),*flags,'/c',str(ROOT/'tests'/folder/(stem+'.cpp')),'/Fo'+str(OUT/(stem+'.obj'))])
            commands.append([str(LINKER),'/nologo','/machine:X86','/subsystem:console','/threads:1',
                '/out:'+str(OUT/(stem+'.exe')),str(OUT/(stem+'.obj')),'kernel32.lib'])
            commands.append([str(OUT/(stem+'.exe'))])
    else:
        prepare_existing_client(commands, flags, server_definitions, server_includes,
            client_definitions, client_includes, source)
    commands=[[str(ROOT/Path(arg).relative_to(source)) if arg.startswith(str(source)+os.sep) else arg for arg in command] for command in commands]
    reused = []
    if args.purchase and args.reuse:raise ValueError('Purchase checks do not reuse preceding server objects')
    if args.reuse:
        previous=(BUILD/args.reuse).resolve()
        if not previous.is_relative_to(BUILD):raise ValueError('Prior pass must stay in melee build area')
        count=args.skip
        if count<1 or count>8:raise ValueError('Only leading server compilation steps can be reused')
        old=json.loads((previous/'result.json').read_text())
        old_plan=json.loads((previous/'proposal.json').read_text())
        assert not old['includeUnknown'] and not old['includeChanged']
        for number,command in enumerate(old_plan['commands'][:count],1):
            assert old['steps'][number-1]['exitCode']==0
            old_source=Path(command[command.index('/c')+1])
            new_source=source/old_source.relative_to(previous/'source')
            assert old_source.read_bytes()==new_source.read_bytes()
            for line in (previous/f'{number:02}.log').read_text().splitlines():
                match=re.match(r'Note: including file:\s*(.+)',line)
                if not match: continue
                path=Path(match[1].strip()).resolve()
                record=next(x for x in old['actualIncludes'] if x['path'].lower()==str(path).lower())
                assert identity(path)==record
                if path.is_relative_to(previous/'source'):
                    replacement=source/path.relative_to(previous/'source')
                else: replacement=Path(overlay.get(str(path).lower(),str(path)))
                assert replacement.read_bytes()==path.read_bytes()
                reused.append(record)
            obj=Path(next(arg[3:] for arg in command if arg.startswith('/Fo')))
            record=next(x for x in old['products'] if x['path']==str(obj))
            assert identity(obj)==record;reused.append(record)
        reused += [identity(previous/'proposal.json'),identity(previous/'result.json')]
        commands=commands[count:]
    proposal = dict(inputs=list(inputs.values()),commands=commands,jobs=1,reused=reused,
        overlay=overlay,environment=str(ENVFILE),runner=identity(Path(__file__)),
        boundary=('Complete actual SDK transaction, service and melee TUs; controlled unchanged SDK inventory methods and public service melee forwarding; inherited cannon backup regression. No full DLL or live game.' if args.purchase else
            'Actual SDK melee and public integration TU compilation, full actual client controlled test plus real delta-field round-trip; not a full DLL or live game.'))
    (OUT/'proposal.json').write_text(json.dumps(proposal,indent=2)+'\n')
    print(json.dumps({'path':str(OUT),'inputs':len(inputs),'steps':len(commands)}))

def prepare_existing_client(commands, flags, server_definitions, server_includes, client_definitions, client_includes, source):
    commands.append([str(COMPILER),*flags,'/Zc:strictStrings-',*['/D'+x for x in server_definitions],
        *['-I'+str(p) for p in server_includes],'/c',str(source/'external/ReGameDLL_CS/regamedll/dlls/client.cpp'),'/Fo'+str(OUT/'server_client.obj')])
    commands.append([str(COMPILER),*flags,'/c',str(source/'native/csol/weapons/melee_attacks.cpp'),'/Fo'+str(OUT/'melee_attacks.obj')])
    commands.append([str(COMPILER),*flags,*['/D'+x for x in client_definitions],
        *['-I'+str(p) for p in client_includes],'/c',str(source/'tests/csol-melee/client.cpp'),'/Fo'+str(OUT/'client.obj')])
    commands.append([str(LINKER),'/nologo','/machine:X86','/subsystem:console','/threads:1',
        '/out:'+str(OUT/'client.exe'),str(OUT/'client.obj'),str(OUT/'melee_attacks.obj'),'kernel32.lib'])
    commands.append([str(OUT/'client.exe')])
    commands.append([str(COMPILER),*flags,*['/D'+x for x in client_definitions],
        *['-I'+str(p) for p in client_includes],'/c',str(source/'native/csol/client/weapons/weapons.cpp'),'/Fo'+str(OUT/'client_weapons.obj')])

def execute():
    p = json.loads((OUT/'proposal.json').read_text())
    assert not (OUT/'result.json').exists()
    # Only actually read shared headers are required to remain stable; unrelated
    # parallel source work does not invalidate this component compilation.
    before={x['path'].lower():x for x in p['inputs']}
    for x in p.get('reused',[]): assert identity(Path(x['path']))==x
    for x in p['inputs']:
        if '/source/' in x['path'].replace('\\','/'): assert identity(Path(x['path']))==x
    environment=Path(p.get('environment',str(ENVFILE)))
    assert identity(environment)==before[str(environment).lower()]
    env=os.environ.copy();env.update(json.loads(environment.read_text()));env['VSLANG']='1033'
    result={'steps':[],'boundary':p['boundary']};includes={}
    for number,command in enumerate(p['commands'],1):
        start=time.monotonic()
        r=subprocess.run(command,cwd=OUT,env=env,capture_output=True,timeout=180)
        output=r.stdout+r.stderr;(OUT/f'{number:02}.log').write_bytes(output)
        text=output.decode('utf-8','replace')
        for line in text.splitlines():
            m=re.match(r'Note: including file:\s*(.+)',line)
            if m:
                logical=Path(m[1].strip()).resolve()
                path=Path(p.get('overlay',{}).get(str(logical).lower(),str(logical)))
                includes[str(path).lower()]=identity(path)
        result['steps'].append(dict(number=number,exitCode=r.returncode,seconds=time.monotonic()-start,log=identity(OUT/f'{number:02}.log')))
        print(json.dumps(result['steps'][-1]),flush=True)
        if r.returncode:
            print(text[-6000:]);break
    unknown=[x for key,x in includes.items() if key not in before]
    changed=[x for key,x in includes.items() if key in before and
             (x['bytes'],x['sha256']) != (before[key]['bytes'],before[key]['sha256'])]
    result['includeUnknown']=unknown;result['includeChanged']=changed
    result['actualIncludes']=list(includes.values())
    result['reuseUnchanged']=all(identity(Path(x['path']))==x for x in p.get('reused',[]))
    result['passed']=len(result['steps'])==len(p['commands']) and all(x['exitCode']==0 for x in result['steps']) and not unknown and not changed and result['reuseUnchanged']
    result['products']=[identity(x) for suffix in ('*.obj','*.exe') for x in OUT.glob(suffix)]
    result['frozenSourcesUnchanged']=all(identity(Path(x['path']))==x for x in p['inputs'] if '/source/' in x['path'].replace('\\','/'))
    result['passed'] &= result['frozenSourcesUnchanged']
    (OUT/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    for number,command in enumerate(p['commands'],1):
        if command[0]==str(OUT/'client.exe') and number<=len(result['steps']):
            print((OUT/f'{number:02}.log').read_text())
    return 0 if result['passed'] else 1

if args.action=='prepare': prepare()
elif args.action=='execute': raise SystemExit(execute())
