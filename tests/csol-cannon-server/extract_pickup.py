"""Extract the native pickup chain; optional override changes one method only."""
from pathlib import Path
import argparse
import hashlib
import json

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--clip-source", type=Path)
p.add_argument("--ammo-source", type=Path)
a = p.parse_args()
root = a.source.resolve()
targets = [
    ("native/csol/server/wpn_cannon.cpp", "bool CSOL_ClipAmmo("),
    ("native/csol/server/wpn_cannon.cpp", "int CSOL_GiveClipAmmo("),
    ("external/ReGameDLL_CS/regamedll/dlls/player.cpp", "int EXT_FUNC CBasePlayer::__API_HOOK(GiveAmmo)"),
    ("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp", "BOOL CBasePlayerWeapon::AddPrimaryAmmo("),
    ("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp", "int CBasePlayerWeapon::ExtractAmmo("),
    ("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp", "int CBasePlayerWeapon::ExtractClipAmmo("),
    ("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp", "int CBasePlayerWeapon::AddDuplicate("),
    ("external/ReGameDLL_CS/regamedll/dlls/player.cpp", "BOOL EXT_FUNC CBasePlayer::__API_HOOK(AddPlayerItem)"),
    ("external/ReGameDLL_CS/regamedll/dlls/gamerules.cpp", "BOOL CGameRules::CanHaveAmmo("),
    ("external/ReGameDLL_CS/regamedll/dlls/gamerules.cpp", "BOOL CGameRules::CanHavePlayerItem("),
    ("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp", "void CBasePlayerItem::DefaultTouch("),
]
chunks, inputs = [], []
for relative, signature in targets:
    base = a.clip_source.resolve() if a.clip_source and signature == "int CBasePlayerWeapon::ExtractClipAmmo(" else root
    if a.ammo_source and signature == "int EXT_FUNC CBasePlayer::__API_HOOK(GiveAmmo)":
        base = a.ammo_source.resolve()
    path = base / relative
    data = path.read_bytes()
    text = data.decode("utf-8")
    start = text.index(signature)
    brace = text.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    body = text[start:end]
    chunks.append('#line %d "%s"\n%s' % (text[:start].count("\n") + 1, path.as_posix(), body))
    inputs.append(dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                       signature=signature, method_sha256=hashlib.sha256(body.encode()).hexdigest()))
header = root / "native/csol/cannon.hpp"
chunks.insert(0, '#include "%s"' % header.as_posix())
a.output.mkdir(parents=True, exist_ok=True)
(a.output / "pickup.inc").write_text("\n\n".join(chunks), encoding="utf-8")
(a.output / "pickup-inputs.json").write_text(json.dumps(inputs, indent=2), encoding="utf-8")
print(f"Extracted {len(targets)} pickup methods; ExtractClipAmmo override={a.clip_source}; GiveAmmo override={a.ammo_source}")
