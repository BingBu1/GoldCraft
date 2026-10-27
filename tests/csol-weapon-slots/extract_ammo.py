"""Extract exact function bodies; compilation exercises production branches."""
from pathlib import Path
import sys,hashlib,json
root=Path(__file__).resolve().parents[2]
source=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else root
targets=[('client.cpp','bool EXT_FUNC __API_HOOK(BuyGunAmmo)'),('player.cpp','int EXT_FUNC CBasePlayer::__API_HOOK(GiveAmmo)'),('weapons.cpp','BOOL CBasePlayerWeapon::AddPrimaryAmmo')]
targets += [('weapons.cpp','int CBasePlayerWeapon::ExtractAmmo'),('weapons.cpp','int CBasePlayerWeapon::ExtractClipAmmo'),('weapons.cpp','int CBasePlayerWeapon::AddDuplicate')]
chunks=[];sources=[]
for name,signature in targets:
    path=source/'external/ReGameDLL_CS/regamedll/dlls'/name
    data=path.read_bytes();text=data.decode('utf-8');start=text.index(signature);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    chunks.append(text[start:end]);sources.append(dict(file=str(path),sha256=hashlib.sha256(data).hexdigest(),signature=signature))
path=source/'native/csol/server/wpn_cannon.cpp'
data=path.read_bytes();text=data.decode('utf-8')
for signature in ['bool CSOL_ClipAmmo(', 'int CSOL_GiveClipAmmo(']:
    start=text.index(signature);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    chunks.insert(0,text[start:end]);sources.append(dict(file=str(path),sha256=hashlib.sha256(data).hexdigest(),signature=signature))
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
(out/'actual_ammo.inc').write_text('\n'.join(chunks),encoding='utf-8')
(out/'ammo-source.json').write_text(json.dumps(sources,indent=2),encoding='utf-8')
