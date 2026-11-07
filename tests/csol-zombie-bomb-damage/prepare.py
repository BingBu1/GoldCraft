# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze narrow actual-SDK damage/impulse tests without compiling or running."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,sys
args=argparse.ArgumentParser()
args.add_argument('--sdk',type=Path,required=True,help='Prepared protection behavior baseline')
args.add_argument('--output',type=Path,required=True)
args.add_argument('--compiler',type=Path,required=True)
args=args.parse_args()
root=Path(__file__).resolve().parents[2];base=args.sdk.resolve();out=args.output.resolve()
assert out.is_relative_to(root/'build') and not out.exists()
out.mkdir(parents=True)
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def row(p):
 p=p.resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def save(p,j):p.write_text(json.dumps(j,indent=2)+'\n',encoding='utf8')
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('extract',root/'tests/common/cpp_function.py')
extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
plan=read(base/'prepared.json');overlay=read(base/'overlay.json');files=plan['files'].copy();delta=[]
rels=['native/csol/server/'+n for n in ['scenario_zombie_bomb_damage.cpp','scenario_zombie_bomb.h',
 'scenario_player_impulse.cpp','scenario_player_impulse.h','scenario_player_runtime.cpp','scenario_player_runtime.h',
 'scenario_session.h','scenario_runtime.h','scenario_map.h','game_rules_gate.h']]
rels+=['tests/csol-zombie-bomb-damage/'+n for n in ['fixture.hpp','edges.inc','golden.inc','tests.inc']]
rels+=['tests/csol-scenario-entities/engine_stubs.cpp','tests/csol-scenario-player-scan/player_stubs.cpp']
for rel in rels:
 p=out/'source'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((root/rel).read_bytes());assert p.stat().st_nlink==1
 frozen=row(p);delta.append(dict(relative=rel,original=row(root/rel),frozen=frozen));files[rel]=frozen
 suffix='/'+rel.lower()
 for item in overlay['roots']:
  if item['name'].replace('\\','/').lower().endswith(suffix):item['external-contents']=str(p)
 overlay['roots'].append(dict(type='file',name=str(root/rel),**{'external-contents':str(p)}))
for rel,r in files.items():
 if Path(rel).suffix.lower() in ('.h','.hpp','.inc'):
  overlay['roots'].append(dict(type='file',name=str(out/'source'/rel),**{'external-contents':r['path']}))
save(out/'overlay.json',overlay)
runtime=files['native/csol/server/scenario/core/scenario_player_runtime.cpp'];pieces=[]
main=('#include "'+(out/'source/tests/csol-zombie-bomb-damage/fixture.hpp').as_posix()+'"\n').encode()
for signature in ['std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Resolve(',
 'std::shared_ptr<const CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Find(',
 'void CCSOLScenarioPlayerRuntime::PlayerSpawnStarted(']:
 raw=Path(runtime['path']).read_bytes();start,end,body=extract.function(raw,signature.encode())
 pieces.append(dict(source=runtime,signature=signature,begin=start,end=end,sha256=hashlib.sha256(body).hexdigest()))
 main+=body+b'\n'
for n in ['edges.inc','tests.inc']:main+=('#include "'+(out/'source/tests/csol-zombie-bomb-damage'/n).as_posix()+'"\n').encode()
(out/'main.cpp').write_bytes(main);save(out/'extraction.json',pieces)
flags=plan['commands'][0]['argv'];flags=[x for x in flags if not x.lower().endswith('.cpp') and not x.startswith('/Fo')]
flags[0]=str(args.compiler.resolve())
flags.insert(1,'/I'+str(out/'source/native/csol/server'))
flags=[('/clang:'+str(out/'overlay.json')) if x.startswith('/clang:') and x.endswith('overlay.json') else x for x in flags]
names=['fixture','damage','impulse','engine','player']
sources=[out/'main.cpp',out/'source/native/csol/server/scenario/monsters/scenario_zombie_bomb_damage.cpp',
 out/'source/native/csol/server/scenario/core/scenario_player_impulse.cpp',out/'source/tests/csol-scenario-entities/engine_stubs.cpp',
 out/'source/tests/csol-scenario-player-scan/player_stubs.cpp']
commands=[dict(name=n,argv=flags+[str(p),'/Fo'+str(out/(n+'.obj'))])for n,p in zip(names,sources)]
commands.append(dict(name='link',argv=[flags[0],'--target=i686-pc-windows-msvc','/nologo','/MT','-fuse-ld=lld',
 *[str(out/(n+'.obj'))for n in names],'/Fe'+str(out/'tests.exe'),'/link','/MACHINE:X86','/OPT:REF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1']))
commands.append(dict(name='test',argv=[str(out/'tests.exe')]))
added=[r['frozen']for r in delta]+[row(out/n) for n in ['main.cpp','overlay.json','extraction.json']]+[row(Path(__file__))]
known={str(Path(r['path']).resolve()).lower():r for r in plan['inputs']+added}
save(out/'prepared.json',dict(commands=commands,environment=plan['environment'],inputs=list(known.values()),files=files,
 delta=delta,base=str(base),extraction=row(out/'extraction.json'),jobs=1,
 boundary='Whole production damage and impulse TUs, real SDK layouts/RTTI/edict/EHANDLE, exact registry Find/Resolve and SpawnStarted; explicit resource/session/authority/protection/TakeDamage/messages boundaries. No full game rule/DLL/live acceptance.'))
print(json.dumps(dict(out=str(out),inputs=len(known),pieces=len(pieces),commands=len(commands))))
