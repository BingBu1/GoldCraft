"""Select production HUD acceptance, freshness and redraw for the real GL test.

The fixture adapts engine callbacks and time; it does not duplicate HUD policy,
RLE decoding, texture upload or composition shader implementations.
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


parts = [region("bool HudFresh(){", "bool ServerMenuInput(){"),
         region("void ClearHud(){", "void UpdateViewModel(){"),
         region("void ReadHud(Reader& r){", "void SendControl("),
         region("int Redraw(float time,int intermission) {", "void DrawNormal()")]
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text("\n".join(parts), encoding="utf-8")
print(json.dumps({"sourceSha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
                  "extractedSha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  "boundary": "Production ReadHud/HudFresh/ClearHud/Redraw, decoder and GL backend; clock, ownership and engine draw callbacks adapted."}))
