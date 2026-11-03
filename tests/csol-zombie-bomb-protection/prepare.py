# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse,hashlib,importlib.util,json,os,sys
parser=argparse.ArgumentParser(description='Freeze actual SDK zombie-bomb protection behavior from a verified compile-only checkpoint.')
parser.add_argument('--sdk',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];base=args.sdk.resolve();out=args.output.resolve()
assert base.is_relative_to(root/'build') and out.is_relative_to(root/'build')
assert not out.exists();out.mkdir()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def row(p):
 p=p.resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def save(p,j):p.write_text(json.dumps(j,indent=2)+'\n',encoding='utf8')
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('extract',root/'tests/common/cpp_function.py');extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
plan=read(base/'prepared.json');overlay=read(base/'overlay.json');ledger=read(Path(plan['base'])/'effective-source-manifest.json')['files']
files={r['relative']:r['frozen'] for r in ledger};files.update({r['relative']:r['frozen']for r in plan['delta']})
added=[]
for rel in ['tests/csol-zombie-bomb-protection/fixture.hpp','tests/csol-zombie-bomb-protection/edges.inc','tests/csol-zombie-bomb-protection/tests.inc','tests/csol-scenario-entities/engine_stubs.cpp','tests/csol-scenario-player-scan/player_stubs.cpp']:
 p=out/'source'/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((root/rel).read_bytes());files[rel]=row(p);added.append(row(p))
for rel,r in files.items():
 if Path(rel).suffix.lower() in ('.h','.hpp','.inc'):
  overlay['roots'].append(dict(type='file',name=str(out/'source'/rel),**{'external-contents':r['path']}))
save(out/'overlay.json',overlay)
pieces=[]
def take(rel,sig):
 r=files[rel];p=Path(r['path']);b=p.read_bytes();assert row(p)['sha256']==r['sha256']
 start,end,body=extract.function(b,sig.encode());pieces.append(dict(source=r,signature=sig,begin=start,end=end,sha256=hashlib.sha256(body).hexdigest()));return body+b'\n'
runtime='native/csol/server/scenario_player_runtime.cpp';weapons='native/csol/server/scenario_weapon_state.cpp'
text=('#include "'+(out/'source/tests/csol-zombie-bomb-protection/fixture.hpp').as_posix()+'"\n').encode()
text+=b'namespace {\n'+take(weapons,'bool Live(')+take(weapons,'CBasePlayerWeapon* Active(')+b'}\n'
for sig in ['CCSOLScenarioWeaponState::AttributeRecord::AttributeRecord(AttributeCounters&','CCSOLScenarioWeaponState::AttributeRecord::AttributeRecord(const AttributeRecord&','CCSOLScenarioWeaponState::AttributeRecord::~AttributeRecord(','CCSOLScenarioWeaponState::CCSOLScenarioWeaponState(','const CCSOLScenarioWeaponState::Weapon* CCSOLScenarioWeaponState::Find(','CCSOLScenarioWeaponState::Weapon* CCSOLScenarioWeaponState::Edit(','bool CCSOLScenarioWeaponState::SetParts(','std::array<std::uint32_t, 2> CCSOLScenarioWeaponState::Parts(','bool CCSOLScenarioWeaponState::ReplacePartEffects(','void CCSOLScenarioWeaponState::AppendParts(','bool CCSOLScenarioWeaponState::CollectPlayerParts(']:text+=take(weapons,sig)
for sig in ['std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Resolve(','std::shared_ptr<const CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Find(','void CCSOLScenarioPlayerRuntime::PlayerSpawnStarted(','bool CCSOLScenarioPlayerRuntime::CheckMonsterAddon13(','bool CCSOLScenarioPlayerRuntime::UpdatePlayerBuffs(','bool CCSOLScenarioPlayerRuntime::ResetMonsterSkillState(']:text+=take(runtime,sig)
text+=b'namespace csol::scenario {\n'+take('native/csol/scenario/equipment.cpp','float EquipmentAddon::Parameter(')+b'}\n'
for file in [files['native/csol/server/scenario_player_bomb_protection.cpp']['path'],str(out/'source/tests/csol-zombie-bomb-protection/edges.inc'),str(out/'source/tests/csol-zombie-bomb-protection/tests.inc')]:text+=('#include "'+Path(file).as_posix()+'"\n').encode()
main=out/'main.cpp';main.write_bytes(text);added+=[row(main),row(out/'overlay.json')];save(out/'extraction.json',pieces)
flags=plan['commands'][0]['argv'];flags=[x for x in flags if not x.lower().endswith('.cpp') and not x.startswith('/Fo')]
flags=[('/clang:'+str(out/'overlay.json')) if x.startswith('/clang:') and x.endswith('overlay.json') else x for x in flags]
commands=[]
for name,p in [('fixture',main),('engine',out/'source/tests/csol-scenario-entities/engine_stubs.cpp'),('player',out/'source/tests/csol-scenario-player-scan/player_stubs.cpp')]:
 commands.append(dict(name=name,argv=flags+[str(p),'/Fo'+str(out/(name+'.obj'))]))
commands.append(dict(name='link',argv=[flags[0],'--target=i686-pc-windows-msvc','/nologo','/MT','-fuse-ld=lld',*[str(out/(n+'.obj'))for n in ['fixture','engine','player']],'/Fe'+str(out/'tests.exe'),'/link','/MACHINE:X86','/OPT:REF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1']))
commands.append(dict(name='test',argv=[str(out/'tests.exe')]))
known={str(Path(r['path']).resolve()).lower():r for r in plan['inputs']+added}
save(out/'prepared.json',dict(commands=commands,environment=plan['environment'],inputs=list(known.values()),files=files,base=str(base),extraction=row(out/'extraction.json'),jobs=1,boundary='Exact runtime/part method bodies with real SDK entities and state, complete bomb TU; controlled session resource admission, registry and unrelated virtuals. No game or DLL lifecycle execution.'))
print(json.dumps(dict(out=str(out),inputs=len(known),pieces=len(pieces),commands=len(commands))))
