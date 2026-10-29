"""Extract exact capacity, delivery and purchase functions for boundary fixtures."""
from pathlib import Path
import hashlib
import json
import sys

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
sources = [
    ("native/csol/server/scenario_inventory_capacity.cpp", "int CSOL_PlayerAmmoCapacity("),
    ("native/csol/server/scenario_inventory_capacity.cpp", "int CSOL_PlayerArmorCapacity("),
    ("external/ReGameDLL_CS/regamedll/dlls/gamerules.cpp", "BOOL CGameRules::CanHaveAmmo("),
    ("external/ReGameDLL_CS/regamedll/dlls/weapons.cpp", "int CBasePlayerItem::iMaxAmmo1() const"),
    ("external/ReGameDLL_CS/regamedll/dlls/player.cpp", "int EXT_FUNC CBasePlayer::__API_HOOK(GiveAmmo)("),
    ("external/ReGameDLL_CS/regamedll/dlls/client.cpp", "bool EXT_FUNC __API_HOOK(BuyGunAmmo)("),
    ("external/ReGameDLL_CS/regamedll/dlls/items.cpp", "BOOL CItemKevlar::MyTouch("),
    ("external/ReGameDLL_CS/regamedll/dlls/items.cpp", "BOOL CItemAssaultSuit::MyTouch("),
]
chunks, manifest = [], []
for relative, signature in sources:
    path = root / relative
    data = path.read_bytes()
    text = data.decode("utf-8")
    start = text.index(signature)
    end = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    chunks.append(text[start:end])
    manifest.append(dict(path=relative, signature=signature, sha256=hashlib.sha256(data).hexdigest(),
                         bodySha256=hashlib.sha256(chunks[-1].encode()).hexdigest()))
(out / "actual_capacity.inc").write_text("\n\n".join(chunks) + "\n", encoding="utf-8")
(out / "source.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
native = root / "native/csol/server/scenario_native_weapons.cpp"
native_text = native.read_text(encoding="utf-8")
(out / "actual_native_weapons.inc").write_text(native_text[native_text.index("bool CSOL_NativeWeaponMonsterCallbacks("):], encoding="utf-8")
runtime = (root / "native/csol/server/scenario_player_runtime.cpp").read_text(encoding="utf-8")
for signature, output in [("bool CCSOLScenarioPlayerRuntime::ReadEntitySource(", "actual_read_source.inc"),
                          ("const auto notifyWeapon = [&](CBasePlayerWeapon* weapon, bool active)", "actual_notify_weapon.inc")]:
    start = runtime.index(signature)
    end = runtime.index("{", start) + 1
    depth = 1
    while depth:
        depth += (runtime[end] == "{") - (runtime[end] == "}")
        end += 1
    text = runtime[start:end]
    if signature.startswith("const"): text += ";"
    (out / output).write_text(text + "\n", encoding="utf-8")
print("Extracted eight actual capacity/delivery/purchase functions.")
