"""Run the matched production landing block and fall rule in an engine fixture."""
from pathlib import Path
import sys

root, output = map(Path, sys.argv[1:])
player=(root/'external/ReGameDLL_CS/regamedll/dlls/player.cpp').read_text(encoding='utf-8-sig')
start=player.index('\t// Do not accumulate a second fall')
end=player.index('\t// select the proper animation',start)
landing=player[start:end]
assert landing.count('GoldCraft_StatusFall(edict(), m_flFallVelocity, gravity)')==1
assert landing.count('g_pGameRules->FlPlayerFallDamage(this) * fall.multiplier')==1
rule=(root/'external/ReGameDLL_CS/regamedll/dlls/multiplay_gamerules.cpp').read_text(encoding='utf-8-sig')
start=rule.index('float EXT_FUNC CHalfLifeMultiplay::__API_HOOK(FlPlayerFallDamage)')
end=rule.index('\n}',start)+2
rule=rule[start:end].replace('float EXT_FUNC CHalfLifeMultiplay::__API_HOOK(FlPlayerFallDamage)','float CHalfLifeMultiplay::NativeFallDamage')
bridge=(root/'native/server/bridge.cpp').read_text(encoding='utf-8-sig')
start=bridge.index('GoldCraft_FallAdjustment GoldCraft_StatusFall(')
end=bridge.index('\n}',start)+2
header=(root/'external/ReGameDLL_CS/regamedll/dlls/player.h').read_text(encoding='utf-8-sig')
constants='\n'.join(line for line in header.splitlines() if line.startswith('const float ') and any(
    key in line for key in ['MAX_PLAYER_FATAL_FALL_SPEED','MAX_PLAYER_SAFE_FALL_SPEED','DAMAGE_FOR_FALL_SPEED','PLAYER_FALL_PUNCH_THRESHHOLD']))
output.write_text(constants+'\n'+bridge[start:end]+'\n'+rule+'\nvoid CBasePlayer::Landing(){\n'+landing+'\n}\n',encoding='utf-8')
