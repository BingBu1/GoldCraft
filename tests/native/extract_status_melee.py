"""Use the production consumer and exact knife damage expressions in the adapter."""
from pathlib import Path
import sys
root,output=map(Path,sys.argv[1:])
bridge=(root/'native/server/bridge.cpp').read_text(encoding='utf-8')
knife=(root/'external/ReGameDLL_CS/regamedll/dlls/wpn_shared/wpn_knife.cpp').read_text(encoding='utf-8')
start=bridge.index('float GoldCraft_StatusMeleeDamage(')
end=bridge.index('float GoldCraft_StatusMaxSpeed(',start)
swing='GoldCraft_StatusMeleeDamage(m_pPlayer->edict(), KnifeSwingDamage(m_flNextPrimaryAttack + 0.4f < UTIL_WeaponTimeBase()))'
stab='GoldCraft_StatusMeleeDamage(m_pPlayer->edict(), KnifeStabDamage())'
assert knife.count(swing)==1 and knife.count(stab)==1
assert knife.index(stab)<knife.index('flDamage *= KnifeBackStabMultiplier();')<knife.index('pEntity->TraceAttack(m_pPlayer->pev, flDamage')
assert 'else if(message.type==Type::status_melee)HandleStatusMelee(message.payload);' in bridge
swing=swing.replace('m_pPlayer->edict()','player').replace('KnifeSwingDamage(m_flNextPrimaryAttack + 0.4f < UTIL_WeaponTimeBase())','base')
stab=stab.replace('m_pPlayer->edict()','player').replace('KnifeStabDamage()','base')
output.write_text(bridge[start:end]+f'float knife_swing(edict_t* player,float base){{return {swing};}}\n'
    +f'float knife_stab(edict_t* player,float base,float backstab){{float damage={stab};return damage*backstab;}}\n',encoding='utf-8')
