"""Extract the production purchase entry points without replacing their bodies."""
from pathlib import Path
import hashlib
import json
import sys

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
targets = [
    ("player.cpp", "bool CBasePlayer::CanPlayerBuy("),
    ("player.cpp", "void CBasePlayer::HandleSignals("),
    ("client.cpp", "bool EXT_FUNC __API_HOOK(BuyGunAmmo)("),
]
chunks, identities = [], []
def extract(path, signature):
    data = path.read_bytes()
    source = data.decode("utf-8")
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    body = source[start:end]
    identities.append({"path": str(path), "sha256": hashlib.sha256(data).hexdigest(),
                       "signature": signature, "bodySha256": hashlib.sha256(body.encode()).hexdigest()})
    return body

gate = root / "native/csol/server/game_rules_gate.cpp"
phase = extract(gate, "enum class Phase") + ";\n"
phase += extract(gate, "bool CanBuyThroughoutScenario() const")
(output / "actual_phase.inc").write_text(phase + "\n", encoding="utf-8")
chunks.append(extract(gate, "bool CSOL_ScenarioCanBuy()"))
for filename, signature in targets:
    path = root / "external/ReGameDLL_CS/regamedll/dlls" / filename
    chunks.append(extract(path, signature))
(output / "actual_buy.inc").write_text("\n\n".join(chunks) + "\n", encoding="utf-8")
(output / "source.json").write_text(json.dumps(identities, indent=2) + "\n", encoding="utf-8")
print("Extracted rule phase gate, scope query, CanPlayerBuy, HandleSignals and BuyGunAmmo unchanged.")
