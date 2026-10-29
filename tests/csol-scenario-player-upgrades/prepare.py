"""Freeze one upgrade component batch. Never compiles, links or executes tests."""
from pathlib import Path
import hashlib, json, re

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/csol-scenario-player-upgrades-tests-20261026'
CAND=OUT/'candidate'
BASE=ROOT/'build/csol-scenario-player-upgrades-source-20261026'
WALL=ROOT/'build/csol-scenario-walls-tests-20261026'
NAV=ROOT/'build/csol-scenario-navigation-idle-events-20261026'
SDK=ROOT/'build/csol-scenario-full-server-20261025/source/external/ReGameDLL_CS/regamedll/dlls'
GROUPS=['resources','progress','consumers','messages','cancellation']

def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def ident(path):
    p=Path(path);b=p.read_bytes()
    return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def verify(row):
    actual=ident(row['path']);assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256']),row['path']
    return actual
def put(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)
def dump(path,value):put(path,(json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
assert not (OUT/'handoff.json').exists(),'Final handoff freezes this candidate'
assert ident(BASE/'source-checkpoint.json')['sha256']=='79fc787b6427b7433a11187e19a9f0d62f1e0568e340710ff4736c4341a4e5a8'
assert ident(WALL/'source-plan.json')['sha256']=='62ebfa3ec1cb92cbb984f42c0ceeecffb3bcc51bb4f57c74924bfedf5a8df660'
base=read(BASE/'source-checkpoint.json');wall=read(WALL/'source-plan.json');nav=read(NAV/'build-plan.json')
frozen={}
for row in base['files']:
    actual=ident(row['saved']);assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256'])
    frozen[str(Path(row['path']).relative_to(ROOT))]=actual
copies=[];extractions=[];sources=[]
def copy(source,relative,expected=None):
    actual=ident(source)
    if expected:assert (actual['bytes'],actual['sha256'])==(expected['bytes'],expected['sha256'])
    target=CAND/relative;put(target,Path(source).read_bytes())
    copies.append({'origin':actual,'candidate':ident(target)})
    if target.suffix=='.cpp':sources.append(str(target))
    return target
def production(relative):
    row=frozen[str(Path(relative))];return copy(row['path'],relative,row)

# Only the reviewed empty-stat correction may differ from79fc. No other live
# production path is read. Keep the old source and its identity in the manifest.
relative='native/csol/scenario/upgrade_tables.cpp';old=Path(frozen[str(Path(relative))]['path']).read_bytes()
before=b'            if (record.fields[stat + 3].empty() || !Number(record.fields[stat + 3], level.values[stat]))'
after=(b'            // Real levels 31..100 retain XP but leave the five attributes\n'
       b'            // empty. The original atof path reads these as zero.\n'
       b'            if (!Number(record.fields[stat + 3], level.values[stat]))')
assert old.count(before)==1
delta=ROOT/relative
assert delta.read_bytes()==old.replace(before,after),'Only parent-authorized empty-stat delta is permitted'
copy(delta,relative)
delta_record={'base':frozen[str(Path(relative))],'corrected':ident(CAND/relative),
    'review':'stat-empty precheck removed; existing Number empty-to-zero conversion unchanged',
    'originalData':'real physical rows31..100 contain blank attributes; XP and all class numeric columns have no empty cells'}
for relative in ('native/csol/scenario/upgrade_tables.hpp','native/csol/scenario/upgrade_progress.hpp',
    'native/csol/scenario/upgrade_progress.cpp','native/csol/scenario/resources.cpp',
    'native/csol/server/scenario_player_upgrades.h','native/csol/server/scenario_player_upgrades.cpp',
    'native/csol/server/scenario_messages.h','native/csol/server/scenario_messages.cpp',
    'native/csol/server/game_rules_gate.h'):
    production(relative)

# Reuse the already-manifested SDK header tree in place: no scan, copy, hardlink
# or junction of the SDK. Only a few project headers need local relative paths.
headers={str(Path(r['candidate']['path']).relative_to(WALL/'candidate')):r['candidate'] for r in wall['immutableHeaderLinks']}
for relative in ('native/csol/config_codec.hpp','native/csol/scenario/config.hpp','native/csol/scenario/difficulty.hpp',
    'native/csol/scenario/drop_items.hpp','native/csol/scenario/map_resources.hpp','native/csol/scenario/resources.hpp',
    'native/csol/scenario/monster_types.hpp','native/csol/scenario/trigger_rules.hpp',
    'native/csol/scenario/spawn_points.hpp','native/csol/scenario/trigger_volumes.hpp'):
    row=headers[str(Path(relative))];copy(row['path'],relative,row)
slot_source=NAV/'candidate/native/csol/server/weapon_slots.h'
slot=next(row for row in read(NAV/'inputs.json')['inputs'] if row['path']==str(slot_source))
copy(slot_source,'native/csol/server/weapon_slots.h',slot)
existing={r['candidate']['path']:r['candidate'] for r in wall['copies']}
existing.update({r['output']['path']:r['output'] for r in wall['extractions']})
for relative in ('native/csol/scenario/difficulty.cpp','native/csol/scenario/wall_damage.hpp','native/csol/scenario/wall_damage.cpp',
    'native/csol/server/scenario_breakable.h','native/csol/server/scenario_breakable_guard.h',
    'native/csol/server/scenario_wall_rewards.h','native/csol/server/scenario_wall_rewards.cpp',
    'tests/csol-scenario-difficulty/support.hpp','tests/csol-scenario-entities/engine_stubs.cpp',
    'external/ReGameDLL_CS/regamedll/dlls/wall-sdk-account.cpp',
    'external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.cpp'):
    source=WALL/'candidate'/relative;copy(source,relative,existing[str(source)])
for name in ('fixture.hpp','fixture.cpp','main.cpp','resources.cpp','progress.cpp','consumers.cpp','messages.cpp','cancellation.cpp','sdk-boundary.cpp'):
    copy(ROOT/'tests/csol-scenario-player-upgrades'/name,Path('tests/csol-scenario-player-upgrades')/name)

# Start from the previously manifested fail-fast player hierarchy, retaining
# native CBaseDelay stubs and removing exactly the two real SDK providers.
origin=next(r['origin'] for r in wall['copies'] if r['candidate']['path'].endswith('player-vtable-boundary.cpp'))
verify(origin);raw=Path(origin['path']).read_bytes();lines=raw.splitlines(keepends=True)
removed=[line for line in lines if re.match(rb'(int CBasePlayer::Classify|void CBasePlayer::ResetMaxSpeed)\(',line)]
assert len(removed)==2
target=CAND/'tests/csol-scenario-player-upgrades/player-vtable-boundary.cpp'
put(target,b''.join(line for line in lines if line not in removed));sources.append(str(target))
extractions.append({'output':ident(target),'origin':origin,'excludedLines':[r.decode().strip() for r in removed]})

def region(source,start,end):
    raw=source.read_bytes();a=raw.index(start);b=raw.index(end,a);return a,b,raw[a:b]
def emit(name,source,start,end,includes):
    a,b,part=region(source,start,end);target=CAND/'external/ReGameDLL_CS/regamedll/dlls'/name
    put(target,includes+part);sources.append(str(target))
    extractions.append({'output':ident(target),'origin':ident(source),'byteStart':a,'byteEndExclusive':b,
        'sliceSHA256':hashlib.sha256(part).hexdigest(),'prefix':includes.decode()})
emit('upgrade-sdk-speed.cpp',Path(frozen[str(Path('external/ReGameDLL_CS/regamedll/dlls/player.cpp'))]['path']),
    b'LINK_HOOK_CLASS_VOID_CHAIN2(CBasePlayer, ResetMaxSpeed)',b'LINK_HOOK_CLASS_CHAIN(bool, CBasePlayer, HintMessageEx',
    b'#include "precompiled.h"\n#include "../../../../native/csol/server/game_rules_gate.h"\n')
emit('upgrade-sdk-rules-base.cpp',SDK/'gamerules.cpp',b'CGameRules *g_pGameRules = nullptr;',b'BOOL CGameRules::CanHaveAmmo(',b'#include "precompiled.h"\n')
guard=WALL/'candidate/native/csol/server/scenario_breakable_guard.cpp';verify(existing[str(guard)])
emit('upgrade-sdk-wall-scope.cpp',guard,b'thread_local CSOLBreakableScope*',b'namespace\n{\nclass CSOLScenarioDelayedUse',
    b'#include "precompiled.h"\n#include "../../../../native/csol/server/scenario_breakable_guard.h"\n#include <utility>\n#include <vector>\n')

resource_evidence=read(ROOT/'analysis/csol-model-20261017/scenario/wall-upgrade-consumer/tables.json')
resources=[]
for row in resource_evidence['resources']:
    source=verify(row['source']);target=OUT/'resources'/Path(source['path']).name;put(target,Path(source['path']).read_bytes())
    resources.append({'origin':source,'saved':ident(target)})

# Reuse only successful immutable pure-configuration objects, never the failed
# NAV run's objects. Their recorded identities come from the existing plan.
reuse=[]
for name in ('22-config.obj','28-monster_types.obj','29-trigger_rules.obj'):
    row=next(r for r in nav['reusedObjects'] if Path(r['path']).name==name);reuse.append(verify(row))

sdk_headers=[r['candidate'] for r in wall['immutableHeaderLinks'] if '\\external\\ReGameDLL_CS\\' in r['candidate']['path']]
for row in sdk_headers:verify(row)
contracts={name:ident(NAV/name) for name in ('compiler-environment.json','include-contract.json','library-contract.json','tool-runtime-contract.json')}
oracle=[ident(ROOT/relative) for relative in (
    'analysis/csol-model-20261017/scenario/wall-upgrade-consumer/tables.json',
    'analysis/csol-model-20261017/scenario/upgrade-consumers-final/extraction.json',
    'analysis/csol-model-20261017/scenario/upgrade-consumers-final/2026-10-26_分析-升级状态与实际消费者-report.md')]
compiler=nav['jobs'][0]['compiler'];linker=nav['links'][0]['linker']
template_path=NAV/'response/01-csol-scenario-navigation-loader-fixture.compile.rsp'
template=template_path.read_text(encoding='utf-8-sig').splitlines()
flags=[line for line in template if not line.startswith(('/Fo','/Fd')) and not (line.startswith('"') and line.endswith('.cpp"'))]
flags=[line.replace(str(NAV/'candidate/external/ReGameDLL_CS'),str(WALL/'candidate/external/ReGameDLL_CS')) for line in flags]
# weapons.h has a project-relative include. Let the normal include search
# resolve that one header in this candidate without altering its frozen SDK.
flags.append(f'/I"{CAND / "external/ReGameDLL_CS/regamedll/dlls"}"')
jobs=[]
for i,source in enumerate(sources):
    name=f'{i:02d}-{Path(source).parent.name}-{Path(source).stem}'
    obj=OUT/'objects'/f'{name}.obj';rsp=OUT/'response'/f'{name}.compile.rsp'
    put(rsp,('\n'.join(flags+[f'/Fo"{obj}"',f'/Fd"{obj.with_suffix(".pdb")}"',f'"{source}"'])+'\n').encode('utf-8'))
    jobs.append({'source':source,'object':str(obj),'response':str(rsp),'compiler':compiler})
old_link=Path(nav['links'][0]['response']);old_lines=old_link.read_text(encoding='utf-8-sig').splitlines()
link_flags=[line for line in old_lines if not line.startswith(('/out:','/pdb:')) and not (line.startswith('"') and line.endswith('.obj"'))]
objects=[job['object'] for job in jobs]+[r['path'] for r in reuse]
exe=OUT/'bin/csol_scenario_player_upgrades_tests.exe';rsp=OUT/'response/player-upgrades.link.rsp'
put(rsp,('\n'.join(link_flags+[f'/out:"{exe}"',f'/pdb:"{exe.with_suffix(".pdb")}"']+[f'"{o}"' for o in objects])+'\n').encode('utf-8'))
plan={'productionBase':ident(BASE/'source-checkpoint.json'),'correctedDelta':delta_record,
    'predecessor':ident(WALL/'source-plan.json'),'groups':GROUPS,'sources':sources,'sourceCount':len(sources),
    'copies':copies,'extractions':extractions,'sdkHeaderReferences':sdk_headers,'resourceCopies':resources,
    'reusedObjects':reuse,'jobs':jobs,'links':[{'response':str(rsp),'linker':linker,'exe':str(exe),'objects':objects}],
    'tests':[{'group':g,'command':[str(exe),g,str(OUT/'resources')]} for g in GROUPS],
    'inheritedContracts':contracts,'responseOrigins':[ident(template_path),ident(old_link)],'oracleEvidence':oracle,
    'originalMpSha256':'39f962f34816411e615f01e5279d914204f7fd07c5a5112b0b303acafe0308b2',
    'scope':'one new upgrade function batch only; no prior NAV/wall/ordinary test group rerun',
    'controlledEdges':['character/effect services','engine message/edict allocation','rules multiplayer query and movement routing/rejection'],
    'actualSDK':['CBasePlayer ResetMaxSpeed hook and complete body','AddAccount hook and complete body','Classify','CGameRules ctor/dtor','SDK player layout and EHANDLE'],
    'compiled':False,'executed':False,'buildClosureObserved':False,
    'remaining':['independent source/closure review','execution-input manifest merging inherited compiler/library contracts','explicit coordinator window','actual compile/link closure and five group results']}
dump(OUT/'source-plan.json',plan)
print(json.dumps({'sources':len(sources),'reusedObjects':len(reuse),'sdkHeaderReferences':len(sdk_headers),'groups':len(GROUPS),'compiled':False,'executed':False}))
