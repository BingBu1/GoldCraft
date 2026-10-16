"""Compile actual bridge consumers and WaterMove; generated upstream bodies stay in build."""
from pathlib import Path
import argparse
import hashlib
import json

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
def function(text,signature):
    start=text.index(signature); opening=text.index('{',start); depth=1; cursor=opening+1
    while depth:
        if text[cursor]=='{': depth+=1
        elif text[cursor]=='}': depth-=1
        cursor+=1
    return text[start:cursor]+'\n'
bridge=a.root/'native/server/bridge.cpp'
player=a.root/'external/ReGameDLL_CS/regamedll/dlls/player.cpp'
b=bridge.read_text(encoding='utf-8-sig');s=player.read_text(encoding='utf-8-sig')
parts=[function(b,'bool GoldCraft_StatusBreathing('),function(b,'bool GoldCraft_StatusImmunity('),function(s,'void CBasePlayer::WaterMove()')]
# The native damage and fall paths must actually invoke these consumers.
damage=function(s,'BOOL EXT_FUNC CBasePlayer::__API_HOOK(TakeDamage)')
assert damage.index('GoldCraft_StatusImmunity(edict(), bitsDamageType)')<damage.index('float flRatio')
assert 'GoldCraft_MinecraftFallAuthority(edict()) || GoldCraft_StatusImmunity(edict(), DMG_FALL)' in s
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text('\n'.join(parts),encoding='utf-8')
print(json.dumps(dict(sourceHashes={str(x.relative_to(a.root)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [bridge,player]},
                     boundary='Actual production consumers and WaterMove; engine sounds/random and damage sinks adapted. No live server.')))
