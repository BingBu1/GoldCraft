# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze real SDK presentation consumers and their engine-boundary tests; no compile."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,sys
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--revision',type=Path,required=True)
parser.add_argument('--server-sdk',type=Path,required=True)
parser.add_argument('--client-baseline',type=Path,required=True)
parser.add_argument('--extra',action='append',default=[],help='Explicit frozen shared dependencies absent from the older baseline')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2];out=args.output.resolve()
assert out.is_relative_to(root/'build') and not out.exists();out.mkdir()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def row(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def save(p,j):Path(p).write_text(json.dumps(j,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
revisions=read(args.revision)
for r in revisions:assert row(root/r['path'])['sha256']==r['sha256'],r
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('extract',root/'tests/common/cpp_function.py');extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)

def prepare(kind):
 dest=out/kind;dest.mkdir()
 if kind=='server':
  sdk=read(args.server_sdk/'prepared.json');base=Path(sdk['base']);ledger=read(base/'effective-source-manifest.json')['files']
  files={r['relative']:r['frozen'] for r in ledger};files.update({r['relative']:r['frozen']for r in sdk['delta']})
  overlay=read(args.server_sdk/'overlay.json');known=sdk['inputs'];environment=sdk['environment'];template=sdk['commands'][0]['argv']
  test_files=['fixture.hpp','edges.inc','presentation_server.inc']
  extra=['tests/csol-scenario-entities/engine_stubs.cpp','tests/csol-scenario-player-scan/player_stubs.cpp']
 else:
  base=args.client_baseline;ledger=read(base/'effective-source-manifest.json')['files'];files={r['relative']:r['frozen']for r in ledger}
  overlay=read(base/'source-overlay.json');known=read(base/'inputs.json')['files']+[r['frozen']for r in ledger]
  environment=read(base/'environment.json');template=next(c['command']for c in read(base/'build-commands.json')if any('include\\VGUI' in x for x in c['command']))
  test_files=['presentation_client.cpp','presentation_overlay.cpp']
  extra=['tests/csol-scenario-monster-skills/client.cpp','tests/csol-wall-status/surface_stub.hpp']
 changes={};frozen=[]
 selected=[r['path'] for r in revisions if (kind=='server' and '/server/' in r['path']) or (kind=='client' and '/server/' not in r['path'])]
 for rel in selected+args.extra+['tests/csol-zombie-bomb-protection/'+x for x in test_files]+extra:
  source=root/rel;target=dest/'source'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(source.read_bytes());assert target.stat().st_nlink==1
  old=files.get(rel);r=row(target);files[rel]=r;frozen.append(dict(relative=rel,original=row(source),frozen=r))
  changes[str(source).lower()]=str(target)
  if old:changes[str(Path(old['path'])).lower()]=str(target)
 for item in overlay['roots']:
  if item['name'].lower() in changes:item['external-contents']=changes[item['name'].lower()]
  elif item['external-contents'].lower() in changes:item['external-contents']=changes[item['external-contents'].lower()]
 for path,target in changes.items():
  if not any(item['name'].lower()==path for item in overlay['roots']):overlay['roots'].append(dict(type='file',name=path,**{'external-contents':target}))
 for rel,r in files.items():
  if Path(rel).suffix.lower() in ('.h','.hpp','.inc'):
   overlay['roots'].append(dict(type='file',name=str(dest/'source'/rel),**{'external-contents':r['path']}))
 save(dest/'overlay.json',overlay);save(dest/'source-delta.json',frozen)
 flags=[x for x in template if not x.lower().endswith('.cpp') and not x.startswith(('/Fo','/Fd')) and x not in ['/JMC','/Zi','--']]
 flags=[('/clang:'+str(dest/'overlay.json'))if x.startswith('/clang:') and x.endswith('overlay.json')else x for x in flags]
 added=[row(dest/'overlay.json')]+[r['frozen']for r in frozen];commands=[]
 def compile(name,path):commands.append(dict(name=name,argv=flags+[str(path),'/Fo'+str(dest/(name+'.obj'))]))
 def link(name,objects):
  commands.append(dict(name=name+'-link',argv=[flags[0],'--target=i686-pc-windows-msvc','/nologo','/MT','-fuse-ld=lld',*[str(dest/(n+'.obj'))for n in objects],'/Fe'+str(dest/(name+'.exe')),'/link','/MACHINE:X86','/OPT:REF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1']))
  commands.append(dict(name=name+'-test',argv=[str(dest/(name+'.exe'))]))
 if kind=='server':
  pieces=[]
  def take(rel,sig):
   r=files[rel];p=Path(r['path']);assert row(p)['sha256']==r['sha256'];start,end,b=extract.function(p.read_bytes(),sig.encode())
   pieces.append(dict(source=r,signature=sig,begin=start,end=end,sha256=hashlib.sha256(b).hexdigest()));return b+b'\n'
  runtime='native/csol/server/scenario_player_runtime.cpp';weapons='native/csol/server/scenario_weapon_state.cpp'
  body=('#include "'+(dest/'source/tests/csol-zombie-bomb-protection/fixture.hpp').as_posix()+'"\n').encode()
  body+=b'namespace {\n'+take(weapons,'bool Live(')+take(weapons,'CBasePlayerWeapon* Active(')+b'}\n'
  for sig in ['CCSOLScenarioWeaponState::AttributeRecord::AttributeRecord(AttributeCounters&','CCSOLScenarioWeaponState::AttributeRecord::AttributeRecord(const AttributeRecord&','CCSOLScenarioWeaponState::AttributeRecord::~AttributeRecord(','CCSOLScenarioWeaponState::CCSOLScenarioWeaponState(','const CCSOLScenarioWeaponState::Weapon* CCSOLScenarioWeaponState::Find(','CCSOLScenarioWeaponState::Weapon* CCSOLScenarioWeaponState::Edit(','bool CCSOLScenarioWeaponState::SetParts(','std::array<std::uint32_t, 2> CCSOLScenarioWeaponState::Parts(','bool CCSOLScenarioWeaponState::ReplacePartEffects(','void CCSOLScenarioWeaponState::AppendParts(','bool CCSOLScenarioWeaponState::CollectPlayerParts(']:body+=take(weapons,sig)
  for sig in ['std::shared_ptr<CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Resolve(','std::shared_ptr<const CSOLScenarioPlayerRuntimeState> CCSOLScenarioPlayerRuntime::Find(','void CCSOLScenarioPlayerRuntime::PlayerSpawnStarted(','bool CCSOLScenarioPlayerRuntime::CheckMonsterAddon13(']:body+=take(runtime,sig)
  body+=b'namespace csol::scenario {\n'+take('native/csol/scenario/equipment.cpp','float EquipmentAddon::Parameter(')+b'}\n'
  for file in [files['native/csol/server/scenario_player_bomb_protection.cpp']['path'],str(dest/'source/tests/csol-zombie-bomb-protection/edges.inc'),str(dest/'source/tests/csol-zombie-bomb-protection/presentation_server.inc')]:body+=('#include "'+Path(file).as_posix()+'"\n').encode()
  main=dest/'main.cpp';main.write_bytes(body);added.append(row(main));save(dest/'extraction.json',pieces)
  compile('fixture',main);compile('engine',Path(files[extra[0]]['path']));compile('player',Path(files[extra[1]]['path']))
  compile('messages',Path(files['native/csol/server/scenario_messages.cpp']['path']));compile('sdk_bomb',Path(files['native/csol/server/scenario_player_bomb_protection.cpp']['path']))
  compile('damage_numbers',Path(files['native/csol/server/damage_numbers.cpp']['path']))
  link('server',['fixture','engine','player','messages','damage_numbers'])
 else:
  for name,rel in [('attachments','native/csol/client/scenario_effects.cpp'),('overlay','native/csol/client/character_skill_display.cpp'),('images','native/csol/image_codec.cpp'),('attachment_fixture','tests/csol-zombie-bomb-protection/presentation_client.cpp'),('overlay_fixture','tests/csol-zombie-bomb-protection/presentation_overlay.cpp')]:
   if rel not in files:raise RuntimeError(rel)
   compile(name,Path(files[rel]['path']))
  link('attachment',['attachments','attachment_fixture']);link('overlay',['overlay','images','overlay_fixture'])
  commands.append(dict(name='cancellation-test',argv=[str(dest/'attachment.exe'),'--cancel']))
  commands.append(dict(name='reentry-test',argv=[str(dest/'attachment.exe'),'--reentry']))
 # Include only immutable known dependencies; dropped live files cannot silently leak into compilation.
 inputs={}
 for r in known+list(files.values())+added:
  p=Path(r['path']);cur=row(p)
  if cur['sha256']==r['sha256']:inputs[str(p.resolve()).lower()]=cur
 save(dest/'prepared.json',dict(commands=commands,environment=environment,inputs=list(inputs.values()),files=files,base=str(base),jobs=1,deployed=False))
 print(kind,'inputs',len(inputs),'commands',len(commands))
prepare('server');prepare('client')
