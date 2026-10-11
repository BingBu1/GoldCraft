"""Check recorded real Epic packet order against its verified client slot semantics.
This is a transport/order oracle, not a graphical client or network handshake.
"""
from pathlib import Path
import argparse,json
p=argparse.ArgumentParser();p.add_argument('report',type=Path);p.add_argument('--fixed',action='store_true');a=p.parse_args()
d=json.loads(a.report.read_text('utf-8'));assert d['passed'] and d['exitCode']==0
slots={};disabled={};unbound=[];feedback=0;bindings=0;phase_rows={};phase_state={}
for i,row in enumerate(d['packets']):
    phase_rows.setdefault(row['phase'],[]).append(row)
    if row['packet']=='ClientboundRespawnPacket' and not row['keepAllData']:slots.clear();disabled.clear()
    payload=row.get('payload','')
    if payload.endswith(':client_bound_init_skills'):
        slots.update(row.get('slots',{}))
    elif payload.endswith(':client_bound_change_skill') and row['entityId']==d.get('playerId',1):
        slots[row['slot']]=row['skill'];disabled[row['slot']]=False;bindings+=1
    elif payload.endswith(':client_bound_set_skill_container_values') and row.get('target')=='ENABLE' and row['entityId']==d.get('playerId',1):
        disabled[row['slot']]=row['boolVal']
    elif payload.endswith(':client_bound_skill_feedback'):
        feedback+=1
        if not slots.get(row['slot']) or slots[row['slot']]=='null':unbound.append({'index':i,'phase':row['phase'],'slot':row['slot']})
    phase_state[row['phase']]={'slots':dict(slots),'disabled':dict(disabled)}
checks={
    'runCompleted':d['passed'],
    'serverRetainedInnateAfterDeath':d['respawnSkill']=='efn:yamato' and d['after20TicksSkill']=='efn:yamato',
    'emptyInventoryAfterDeath':d['respawnItem']=='0 minecraft:air',
}
if 'initializeResetsDisabled' in d:
    checks.update(initializeResetsDisabled=d['initializeResetsDisabled'],dimensionRetainsSkill=d['dimensionSkill']=='efn:yamato',dimensionRetainsDisabled=d['dimensionDisabled'],noKeepSkillsClears=d['noKeepSkillsInnate']=='null')
    if a.fixed:
        for phase,value in [('disabled-join',True),('forced-init',False)]:
            checks[phase+'Disabled']=phase_state[phase]['disabled'].get('WEAPON_INNATE')==value
        checks['nullJoinClearsBinding']=phase_state['null-join']['slots'].get('WEAPON_INNATE')=='null'
        checks['dimensionStartsSynchronized']=phase_state['disable-before-dimension']['disabled'].get('WEAPON_INNATE')==True
        checks['dimensionClientBinding']=phase_state['dimension']['slots'].get('WEAPON_INNATE')==d['dimensionSkill']
        checks['dimensionClientDisabled']=phase_state['dimension']['disabled'].get('WEAPON_INNATE')==d['dimensionDisabled']
        checks['noKeepSkillsClientClears']=phase_state['no-keep-skills']['slots'].get('WEAPON_INNATE')=='null'
checks['noUnboundFeedback' if a.fixed else 'reproducesUnboundFeedback']=not unbound if a.fixed else bool(unbound)
result={'passed':all(checks.values()),'checks':checks,'feedback':feedback,'bindings':bindings,'unboundCount':len(unbound),'unbound':unbound}
out=a.report.with_name(a.report.stem+'-sequence.json');out.write_text(json.dumps(result,indent=2)+'\n','utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='unbound'},indent=2))
if not result['passed']:raise SystemExit(1)
