"""Compile the actual packet consumer and verify its real AddToFullPack call site."""
from pathlib import Path
import argparse

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--root',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()

def function(text,signature):
    start=text.index(signature);opening=text.index('{',start);depth=1;cursor=opening+1
    while depth:
        if text[cursor]=='{':depth+=1
        elif text[cursor]=='}':depth-=1
        cursor+=1
    return text[start:cursor]+'\n'

bridge=(a.root/'native/server/bridge.cpp').read_text(encoding='utf-8-sig')
client=(a.root/'external/ReGameDLL_CS/regamedll/dlls/client.cpp').read_text(encoding='utf-8-sig')
fullpack=function(client,'BOOL EXT_FUNC AddToFullPack(')
assert fullpack.index('state->iuser4')<fullpack.index('if (player)\n\t\tGoldCraft_StatusVisibility(state, ent);')<fullpack.rindex('return TRUE;')
assert 'else if(message.type==Type::status_visibility)HandleStatusVisibility(message.payload);' in bridge
assert bridge.count('ClearStatusVisibility();')==2
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(function(bridge,'void GoldCraft_StatusVisibility('),encoding='utf-8')
