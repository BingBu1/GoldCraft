"""Use the matched ReGameDLL PM functions, not a reimplementation of swimming.

Only the extraction recipe is published. Generated third-party bodies stay in
the build tree; the engine trace/sound sinks in the test are deterministic.
"""
import hashlib
import json
import sys
from pathlib import Path

source = Path(sys.argv[1])
output = Path(sys.argv[2])
text = source.read_text(encoding="utf-8-sig")
markers = (
    "qboolean PM_CheckWater()",
    "void PM_WaterMove()",
    "void EXT_FUNC __API_HOOK(PM_Jump)()",
    "void PM_CategorizePositionOriginal()",
)
parts = []
for marker in markers:
    start = text.index(marker + "\n{")
    end = text.index("\n}", start) + 2
    parts.append(text[start:end].replace("PM_CategorizePositionOriginal", "PM_CategorizePosition"))
output.write_text("\n\n".join(parts) + "\n", encoding="utf-8")
output.with_suffix(".json").write_text(json.dumps({
    "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "functions": list(markers),
    "extractedSha256": hashlib.sha256(output.read_bytes()).hexdigest(),
}, indent=2) + "\n", encoding="utf-8")
