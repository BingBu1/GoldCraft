"""Check production overlay wiring and the resulting paint order.

Parses each CSOL VGUI2Extension overlay's real GetAltitude() and the layer it
queues in Paint(), then compares VGUI2Extension's actual call order (descending
altitude, VGUI2ExtensionInternal.cpp BaseUI_Paint) with the compositor order.
Usage: python -I wiring.py
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLIENT = ROOT / "native/csol/client"
header = (CLIENT / "overlay_layers.hpp").read_text(encoding="utf-8")
body = re.search(r"enum class OverlayLayer[^{]*\{([^}]*)\}", header)[1]
layers = [name.strip() for name in body.replace("\n", " ").split(",") if name.strip()]
assert layers[-1] == "Count", layers
layers = layers[:-1]

rows = []
for path in sorted(CLIENT.glob("*.cpp")):
    text = path.read_text(encoding="utf-8")
    if "IVGUI2Extension_BaseUICallbacks" not in text or path.name == "overlay_layers.cpp":
        continue
    paints = re.findall(r"void Paint\(int&[^)]*\)\s*override\s*\{([^}]*)\}", text)
    altitude = re.findall(r"GetAltitude\(\)\s*const\s*override\s*\{\s*return\s*(\d+);\s*\}", text)
    queued = [re.search(r"OverlayLayer::(\w+)", paint) for paint in paints]
    rows.append(dict(file=path.name, altitude=int(altitude[0]) if altitude else None,
                     layer=queued[0][1] if paints and queued[0] else None, paintBodies=len(paints)))

failures = []
painting = [row for row in rows if row["paintBodies"] and row["layer"]]
unqueued = [row["file"] for row in rows if row["paintBodies"] and not row["layer"]]
for row in painting:
    if row["layer"] not in layers:
        failures.append(f"{row['file']}: unknown layer {row['layer']}")
seen = {}
for row in painting:
    if row["layer"] in seen:
        failures.append(f"layer {row['layer']} used by {seen[row['layer']]} and {row['file']}")
    seen[row["layer"]] = row["file"]
missing = [layer for layer in layers if layer not in seen]
if missing:
    failures.append(f"layers without a producer: {missing}")
if any(row["altitude"] is None or row["altitude"] <= 1 for row in painting):
    failures.append("every queued overlay must stay above the compositor altitude 1")

before = [row["file"] for row in sorted(painting, key=lambda row: -row["altitude"])]
after = [seen[layer] for layer in layers]
ties = {}
for row in painting:
    ties.setdefault(row["altitude"], []).append(row["file"])
result = dict(overlays=painting, unqueuedPaintOverlays=unqueued,
              oldPaintOrderBottomToTop=before, tiedAltitudes={k: v for k, v in ties.items() if len(v) > 1},
              newPaintOrderBottomToTop=after, failures=failures)
print(json.dumps(result, indent=2))
raise SystemExit(1 if failures else 0)
