"""Prepare one fixed wall test candidate. No compiler, linker or native run."""
from pathlib import Path
import hashlib, json, os, re

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/csol-scenario-walls-tests-20261026'
CAND=OUT/'candidate'
WALL=ROOT/'build/csol-scenario-walls-source-20261026'
PREV=ROOT/'build/csol-scenario-navigation-idle-events-20261026/candidate'
SDK=ROOT/'build/csol-scenario-full-server-20261025/source/external/ReGameDLL_CS/regamedll/dlls'
def identity(p):
    b=Path(p).read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p,b):
    p.parent.mkdir(parents=True,exist_ok=True)
    # Drafts may change before the sole combined handoff is frozen. Immutable
    # predecessor hardlinks use a separate function and are never overwritten.
    if not p.exists() or p.read_bytes()!=b:p.write_bytes(b)
def link_or_copy(source,target):
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists():assert target.read_bytes()==source.read_bytes()
    else:os.link(source,target)

assert not (OUT/'handoff.json').exists(),'Final candidate already frozen'
assert identity(WALL/'source-checkpoint.json')['sha256']=='18878f6c95964d70e63dbfa86bc1ce8cfe651bd5dc008d3383d64d6b9fa3c8ce'
rows={row['source']:row for row in read(WALL/'source-checkpoint.json')['files']}
rows.update({row['source']:row for row in read(WALL/'delay-lifetime-fix/handoff.json')['files']})
copied=[];linked=[];extractions=[]
for p in (PREV/'external/ReGameDLL_CS').rglob('*'):
    if not p.is_file() or p.suffix=='.cpp':continue
    target=CAND/p.relative_to(PREV);link_or_copy(p,target);linked.append({'origin':identity(p),'candidate':identity(target)})
for p in (PREV/'native/csol').rglob('*'):
    if not p.is_file() or p.suffix not in ('.h','.hpp'):continue
    # Do not import unrelated server protocols or the newer live Session.
    if 'server' in p.relative_to(PREV/'native/csol').parts:continue
    target=CAND/p.relative_to(PREV);link_or_copy(p,target);linked.append({'origin':identity(p),'candidate':identity(target)})
for relative in ('native/csol/scenario/difficulty.cpp','external/ReGameDLL_CS/regamedll/regamedll/hookchains_impl.cpp'):
    source=PREV/relative;target=CAND/relative;write(target,source.read_bytes());copied.append({'origin':identity(source),'candidate':identity(target)})
for name in ('scenario_breakable.h','scenario_breakable.cpp','scenario_breakable_guard.h','scenario_breakable_guard.cpp','scenario_wall_rewards.h','scenario_wall_rewards.cpp'):
    relative=Path('native/csol/server')/name;row=rows[str(ROOT/relative)];source=Path(row['saved']);actual=identity(source)
    assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256'])
    target=CAND/relative;write(target,source.read_bytes());copied.append({'origin':actual,'candidate':identity(target)})
for name in ('wall_damage.hpp','wall_damage.cpp'):
    relative=Path('native/csol/scenario')/name;row=rows[str(ROOT/relative)];source=Path(row['saved']);actual=identity(source)
    assert (actual['bytes'],actual['sha256'])==(row['bytes'],row['sha256'])
    target=CAND/relative;write(target,source.read_bytes());copied.append({'origin':actual,'candidate':identity(target)})
for p in (ROOT/'tests/csol-scenario-walls').iterdir():
    if p.suffix not in ('.cpp','.hpp'):continue
    target=CAND/p.relative_to(ROOT);write(target,p.read_bytes());copied.append({'origin':identity(p),'candidate':identity(target)})
for relative in ('tests/csol-scenario-difficulty/support.hpp','tests/csol-scenario-entities/engine_stubs.cpp'):
    p=PREV/relative;target=CAND/relative;write(target,p.read_bytes());copied.append({'origin':identity(p),'candidate':identity(target)})
# Existing player vtable fail-fast support has four providers that must be
# removed because this test links actual CBaseDelay and player Classify.
p=PREV/'tests/csol-scenario-player-scan/player_stubs.cpp'
raw=p.read_bytes();lines=raw.splitlines(keepends=True)
excluded=[line for line in lines if re.match(rb'(void CBaseDelay::KeyValue|int CBaseDelay::Save|int CBaseDelay::Restore|int CBasePlayer::Classify)\(',line)]
assert len(excluded)==4
target=CAND/'tests/csol-scenario-walls/player-vtable-boundary.cpp'
write(target,b''.join(line for line in lines if line not in excluded))
copied.append({'origin':identity(p),'candidate':identity(target),'excludedLines':[x.decode().strip() for x in excluded]})

def region(source,start,end):
    raw=source.read_bytes();a=raw.index(start);b=raw.index(end,a)
    return a,b,raw[a:b]
def function(source,signature):
    raw=source.read_bytes();a=raw.index(signature);begin=raw.index(b'{',a);depth=0
    for i in range(begin,len(raw)):
        depth+=(raw[i]==123)-(raw[i]==125)
        if depth==0:return a,i+1,raw[a:i+1]+b'\n'
    raise ValueError('unbalanced function')
def emit(name,selections,guard=False):
    chunks=[b'#include "precompiled.h"\n']
    if guard:chunks.append(b'#include "../../../../native/csol/server/scenario_breakable_guard.h"\n')
    local=[]
    for source,selector,args in selections:
        a,b,chunk=selector(source,*args);chunks.append(chunk);chunks.append(b'\n')
        local.append({'origin':identity(source),'byteStart':a,'byteEndExclusive':b,'sliceSHA256':hashlib.sha256(source.read_bytes()[a:b]).hexdigest()})
    target=CAND/'external/ReGameDLL_CS/regamedll/dlls'/name
    write(target,b''.join(chunks));extractions.append({'output':identity(target),'regions':local});return target

break_source=Path(rows[str(ROOT/'external/ReGameDLL_CS/regamedll/dlls/func_break.cpp')]['saved'])
subs_source=Path(rows[str(ROOT/'external/ReGameDLL_CS/regamedll/dlls/subs.cpp')]['saved'])
emit('wall-sdk-breakable.cpp',[(break_source,region,(b'// Just add more items',b'TYPEDESCRIPTION CPushable::m_SaveData[]'))],True)
emit('wall-sdk-targets.cpp',[(subs_source,region,(b'// This updates global tables',b'// QuakeEd only writes')),
    (subs_source,function,(b'void CBaseDelay::DelayThink()',))],True)
emit('wall-sdk-relay-render.cpp',[(SDK/'triggers.cpp',region,(b'TYPEDESCRIPTION CTriggerRelay::m_SaveData[]',b'// Global Savedata for multi_manager')),
    (SDK/'triggers.cpp',region,(b'LINK_ENTITY_TO_CLASS(env_render',b'LINK_ENTITY_TO_CLASS(trigger, CBaseTrigger'))])
emit('wall-sdk-services.cpp',[(SDK/'util.cpp',function,(s,)) for s in (
    b'int UTIL_EntitiesInBox(',b'bool UTIL_IsMasterTriggered(',b'void UTIL_Remove(',b'BOOL UTIL_IsValidEntity(',b'void UTIL_MakeVectors(',b'void MAKE_STRING_CLASS(')] +
    [(SDK/'cbase.cpp',function,(b'bool CBaseEntity::Intersects(const Vector &mins, const Vector &maxs)',)),
     (SDK/'cbase.cpp',function,(b'void REMOVE_ENTITY(edict_t *pEntity)',))])
emit('wall-sdk-account.cpp',[(SDK/'player.cpp',region,(b'LINK_HOOK_CLASS_VOID_CHAIN(CBasePlayer, AddAccount',b'void CBasePlayer::ResetMenu()')),
    (SDK/'player.cpp',region,(b'LINK_HOOK_CLASS_CHAIN2(int, CBasePlayer, Classify)',b'LINK_HOOK_CLASS_VOID_CHAIN(CBasePlayer, AddPoints'))])

units=[str(p) for p in sorted(CAND.rglob('*.cpp'))]
plan={'groups':['numeric','metadata','entities','damage','targets','delays','cancellation','rewards'],
    'sources':units,'sourceCount':len(units),'extractions':extractions,'copies':copied,'immutableHeaderLinks':linked,
    'productionBase':identity(WALL/'source-checkpoint.json'),'productionFix':identity(WALL/'delay-lifetime-fix/handoff.json'),
    'sdkOrigins':str(SDK),'sdkHeaderTree':str(PREV/'external/ReGameDLL_CS'),
    'compiled':False,'executed':False,'buildPlanComplete':False,
    'remaining':'independent source review; compiler response/include/library manifest and explicit coordinator execution window; actual compiler/linker closure not yet observed'}
write(OUT/'source-plan.json',(json.dumps(plan,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
print(json.dumps({'sources':len(units),'extractedUnits':len(extractions),'immutableHeaderLinks':len(linked),'compiled':False,'executed':False}))
