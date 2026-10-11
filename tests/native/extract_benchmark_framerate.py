"""Extract the production benchmark policy for the native engine-adapter test.

This only selects complete source regions; the executable tests their behavior.
No duplicate benchmark policy or expected-source-string assertions are used.
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
state = source[source.index("struct FramerateBenchmark {"):
               source.index("std::ofstream motion_capture;")]
functions = source[source.index("bool MatchingHostProtocol(){"):
                   source.index("void PublishBrushSnapshot(){")]
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(state + "\n" + functions, encoding="utf-8")
print(json.dumps({"sourceSha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
                  "extractedSha256": hashlib.sha256(args.output.read_bytes()).hexdigest(),
                  "boundary": "Production functions; public SDK engine, connection and cvar adapters."}))
