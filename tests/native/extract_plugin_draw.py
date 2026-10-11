"""Extract production scene dispatch and its lazy Pass for the actual GL test.

Only complete source regions are selected. The test adapts connection, clock,
ownership and timing inputs; Pass, Mesh, dispatch and shaders stay production.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding="utf-8-sig")


def region(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


parts = [region("class ProfiledPass {", "struct FramerateBenchmark {")]
for name in ("Section", "Texture", "EntityBatch", "EffectBatch"):
    # These production records occupy one line; reject a multiline change so
    # extraction cannot silently select a truncated declaration.
    declaration = source[source.index(f"struct {name} {{"):].splitlines()[0]
    if not declaration.endswith("};"):
        raise ValueError(f"Update the extractor for multiline {name}")
    parts.append(declaration)
args.output.parent.mkdir(parents=True, exist_ok=True)
types = args.output.with_name(args.output.stem + "_types.inc")
types.write_text("\n".join(parts), encoding="utf-8")
args.output.write_text(region("void Draw(bool transparent,std::uint32_t flags) {", "int Redraw("), encoding="utf-8")
print(json.dumps({"sourceSha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
                  "extractedSha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  "recordsSha256": hashlib.sha256(types.read_bytes()).hexdigest(),
                  "boundary": "Production Draw, records, ProfiledPass, Pass/Mesh and shaders on hidden GL; engine inputs adapted."}))
