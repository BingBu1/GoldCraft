"""Compile exact candidate methods against an instrumented engine fixture.

This does not parse or emulate original CSO binaries, and is not a live server.
"""
from pathlib import Path
import argparse
import hashlib
import json

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
a = p.parse_args()
source = a.source.resolve()
a.output.mkdir(parents=True, exist_ok=True)
targets = {
    "native/csol/server/wpn_cannon.cpp": [
        "void CCannon::PrimaryAttack()", "BOOL CCannon::Deploy()", "void CCannon::WeaponIdle()"],
    "external/ReGameDLL_CS/regamedll/dlls/weapons.cpp": [
        "void EXT_FUNC __API_HOOK(ClearMultiDamage)()",
        "void EXT_FUNC __API_HOOK(ApplyMultiDamage)",
        "void EXT_FUNC __API_HOOK(AddMultiDamage)",
        "BOOL CanAttack(", "bool CBasePlayerWeapon::HasSecondaryAttack()",
        "void EXT_FUNC CBasePlayerWeapon::__API_HOOK(ItemPostFrame)()",
        "BOOL EXT_FUNC CBasePlayerWeapon::__API_HOOK(DefaultDeploy)"],
    "external/ReGameDLL_CS/regamedll/dlls/cbase.cpp": ["void CBaseEntity::TraceAttack("],
    "external/ReGameDLL_CS/regamedll/dlls/func_break.cpp": ["BOOL CBreakable::TakeDamage("],
    "external/ReGameDLL_CS/regamedll/dlls/player.cpp": [
        "const char *CBasePlayer::GetKillerWeaponName(", "BOOL CBasePlayer::IsArmored("],
}
chunks, inputs = [], []
for relative, signatures in targets.items():
    path = source / relative
    data = path.read_bytes()
    text = data.decode("utf-8")
    for signature in signatures:
        start = text.index(signature)
        brace = text.index("{", start)
        depth, end = 1, brace + 1
        # These selected methods contain no braces inside string literals.
        while depth:
            depth += (text[end] == "{") - (text[end] == "}")
            end += 1
        body = text[start:end]
        chunks.append('#line %d "%s"\n%s' % (text[:start].count("\n") + 1, path.as_posix(), body))
        inputs.append(dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(),
                           bytes=len(data), signature=signature, method_sha256=hashlib.sha256(body.encode()).hexdigest()))
header = source / "native/csol/cannon.hpp"
chunks.insert(0, '#include "%s"' % header.as_posix())
(a.output / "actual.inc").write_text("\n\n".join(chunks), encoding="utf-8")
inputs.append(dict(path=str(header), sha256=hashlib.sha256(header.read_bytes()).hexdigest(), bytes=header.stat().st_size))
(a.output / "inputs.json").write_text(json.dumps(inputs, indent=2), encoding="utf-8")
print(f"Extracted {len(chunks)-1} unchanged production methods from {source}")
