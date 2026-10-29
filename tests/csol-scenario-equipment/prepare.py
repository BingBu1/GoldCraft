"""Extend the unexecuted upgrade batch using one frozen equipment checkpoint."""
from pathlib import Path
import ast,hashlib,json,re
ROOT=Path(__file__).resolve().parents[2]
OLD=ROOT/'build/csol-scenario-player-upgrades-tests-20261026'
BASE=ROOT/'build/csol-scenario-equipment-source-20261026'
OUT=ROOT/'build/csol-scenario-upgrade-equipment-tests-20261026'
CAND=OUT/'candidate'
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def ident(p):
    p=Path(p);b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def verify(row):
    actual=ident(row['path']);assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256']),row['path'];return actual
def put(p,b):
    p.parent.mkdir(parents=True,exist_ok=True)
    if not p.exists() or p.read_bytes()!=b:p.write_bytes(b)
def dump(p,v):put(p,(json.dumps(v,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
assert not (OUT/'handoff.json').exists(),'combined handoff already sealed'
assert ident(OLD/'handoff.json')['sha256']=='da142ddfbf1b2897397587a0d5a28a3138f18a739e28f982a37fee0f2f645144'
assert ident(BASE/'source-checkpoint.json')['sha256']=='9ba32cf3aa8f489c5dced54dd5ab5c413a42ca9e17837b6cd3e3c5929fef914b'
old=read(OLD/'source-plan.json');base=read(BASE/'source-checkpoint.json');checkpoint=[];copies=[];forwarders=[]
for row in base['files']:
    actual=ident(row['saved']);assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256']);checkpoint.append(actual)
frozen={str(Path(r['path']).relative_to(ROOT)):r for r in base['files']}
sources=[s for s in old['sources'] if s!=str(OLD/'candidate/tests/csol-scenario-player-upgrades/main.cpp')]
for name in ('equipment.hpp','equipment.cpp','equipment_resources.cpp'):
    relative=Path('native/csol/scenario')/name;r=frozen[str(relative)];target=CAND/relative
    put(target,Path(r['saved']).read_bytes());copies.append({'origin':ident(r['saved']),'candidate':ident(target)})
    if target.suffix=='.cpp':sources.append(str(target))
for name in ('scenario_equipment.h','scenario_equipment.cpp'):
    relative=Path('native/csol/server')/name;r=frozen[str(relative)];target=CAND/relative
    put(target,Path(r['saved']).read_bytes());copies.append({'origin':ident(r['saved']),'candidate':ident(target)})
    if target.suffix=='.cpp':sources.append(str(target))
for name in ('fixture.hpp','fixture.cpp','main.cpp','resources.cpp','selection.cpp','effects.cpp','services.cpp'):
    source=ROOT/'tests/csol-scenario-equipment'/name;target=CAND/'tests/csol-scenario-equipment'/name
    put(target,source.read_bytes());copies.append({'origin':ident(source),'candidate':ident(target)})
    if target.suffix=='.cpp':sources.append(str(target))
# Four small forwarding headers preserve exact already-frozen definitions and
# relative includes without copying old fixtures or mutating the old candidate.
for relative in ('tests/csol-scenario-player-upgrades/fixture.hpp','native/csol/scenario/upgrade_tables.hpp','native/csol/server/scenario_player_upgrades.h','native/csol/config_codec.hpp'):
    source=OLD/'candidate'/relative;target=CAND/relative
    put(target,('#pragma once\n#include "'+source.as_posix()+'"\n').encode('utf-8'))
    forwarders.append({'origin':ident(source),'candidate':ident(target)})
assert ident(OLD/'candidate/native/csol/scenario/upgrade_tables.cpp')['sha256']==frozen[str(Path('native/csol/scenario/upgrade_tables.cpp'))]['sha256']

buff=read(ROOT/'analysis/csol-model-20261017/scenario/equipment-abilities/buff-resource.json')
zbs=read(ROOT/'analysis/csol-model-20261017/scenario/upgrade-consumers-final/extraction.json')['resource']
resources=[]
for row in (buff,zbs):
    verify(row);target=OUT/'resources'/Path(row['path']).name;put(target,Path(row['path']).read_bytes());resources.append({'origin':ident(row['path']),'saved':ident(target)})
for row in old['resourceCopies']:
    verify(row['saved']);target=OUT/'resources'/Path(row['saved']['path']).name;put(target,Path(row['saved']['path']).read_bytes());resources.append({'origin':row['saved'],'saved':ident(target)})

# Follow actual rapidjson include directives from the selected two public
# headers. This is a bounded dependency closure, not another full SDK scan/copy.
rapid=ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'
pending=[rapid/'rapidjson/document.h',rapid/'rapidjson/error/en.h'];seen=set();rapid_headers=[]
while pending:
    source=pending.pop().resolve()
    if source in seen:continue
    seen.add(source);rapid_headers.append(ident(source))
    for quote,name in re.findall(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]',source.read_text(encoding='utf-8-sig',errors='replace'),re.M):
        p=(source.parent/name if quote=='"' else rapid/name).resolve()
        if p.is_relative_to(rapid.resolve()) and p.is_file():pending.append(p)
flags=Path(old['jobs'][0]['response']).read_text(encoding='utf-8-sig').splitlines()
flags=[l for l in flags if not l.startswith(('/Fo','/Fd')) and not(l.startswith('"') and l.endswith('.cpp"'))]
flags.append(f'/I"{rapid}"');jobs=[]
for i,source in enumerate(sources):
    name=f'{i:02d}-{Path(source).parent.name}-{Path(source).stem}';obj=OUT/'objects'/f'{name}.obj';rsp=OUT/'response'/f'{name}.compile.rsp'
    put(rsp,('\n'.join(flags+[f'/Fo"{obj}"',f'/Fd"{obj.with_suffix(".pdb")}"',f'"{source}"'])+'\n').encode('utf-8'))
    jobs.append({'source':source,'object':str(obj),'response':str(rsp),'compiler':old['jobs'][0]['compiler']})
link_flags=Path(old['links'][0]['response']).read_text(encoding='utf-8-sig').splitlines()
link_flags=[l for l in link_flags if not l.startswith(('/out:','/pdb:')) and not(l.startswith('"') and l.endswith('.obj"'))]
objects=[j['object'] for j in jobs]+[r['path'] for r in old['reusedObjects']]
exe=OUT/'bin/csol_scenario_upgrade_equipment_tests.exe';rsp=OUT/'response/upgrade-equipment.link.rsp'
put(rsp,('\n'.join(link_flags+[f'/out:"{exe}"',f'/pdb:"{exe.with_suffix(".pdb")}"']+[f'"{o}"' for o in objects])+'\n').encode('utf-8'))
groups=old['groups']+['equipment-resources','equipment-selection','equipment-effects','equipment-services']
plan={'baseUpgradeHandoff':ident(OLD/'handoff.json'),'baseUpgradePlan':ident(OLD/'source-plan.json'),
    'equipmentCheckpoint':ident(BASE/'source-checkpoint.json'),'equipmentSavedFiles':checkpoint,
    'groups':groups,'sources':sources,'copies':copies,'forwarders':forwarders,'resourceCopies':resources,'rapidjsonHeaderClosure':rapid_headers,
    'jobs':jobs,'reusedObjects':old['reusedObjects'],'links':[{'response':str(rsp),'linker':old['links'][0]['linker'],'exe':str(exe),'objects':objects}],
    'tests':[{'group':g,'command':[str(exe),g,str(OUT/'resources')]} for g in groups],
    'sourceCount':len(sources),'compiled':False,'linked':False,'executed':False,'buildClosureObserved':False,
    'oldFiveGroups':'unchanged saved bodies; only the dispatcher main is replaced for one combined binary',
    'sessionBoundary':'fixed Session h/cpp and build integration reviewed statically, deliberately not compiled or linked in this component batch',
    'remaining':['independent preparation review','complete input contract','coordinator execution window','actual compile/link/native tests','full Session/GameRules/inventory/ClassMod/event0x26 and live acceptance']}
dump(OUT/'source-plan.json',plan)
for p in (ROOT/'tests/csol-scenario-equipment').glob('*.py'):ast.parse(p.read_text(encoding='utf-8-sig'))
print(json.dumps({'newTUs':len(sources),'reusedObjects':len(objects)-len(sources),'groups':len(groups),'rapidjsonHeaders':len(rapid_headers),'compiled':False,'executed':False}))
