"""Extract the exact marked lifecycle implementation from a frozen bridge.

Preparation writes this script only. A separately authorized build window calls
it with --bridge <Frozen>/native/server/bridge.cpp and --output <Output>/
effect_lifecycle_bridge.inc, then compiles effect_lifecycle_tests.cpp with that
output directory on the include path. This script never invokes a compiler.
"""
import argparse
import hashlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bridge", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source_path = args.bridge.resolve(strict=True)
    if source_path.name != "bridge.cpp":
        raise SystemExit("Expected the frozen bridge.cpp input")
    data = source_path.read_bytes()
    source = data.decode("utf-8-sig")
    parts = []
    for name in ("identity", "use", "release", "relocation", "form"):
        begin = "// GOLDCRAFT_LIFECYCLE_FIXTURE_BEGIN " + name
        end = "// GOLDCRAFT_LIFECYCLE_FIXTURE_END " + name
        if source.count(begin) != 1 or source.count(end) != 1:
            raise SystemExit("Missing or duplicate extraction markers: " + name)
        start = source.index(begin) + len(begin)
        stop = source.index(end)
        if stop <= start:
            raise SystemExit("Reversed extraction markers: " + name)
        parts.append(source[start:stop].strip())
    output = args.output.absolute()
    if output.resolve() == source_path or output.name != "effect_lifecycle_bridge.inc":
        raise SystemExit("Output must be the isolated effect_lifecycle_bridge.inc")
    output.parent.mkdir(parents=True, exist_ok=True)
    header = "// Exact frozen bridge SHA256: " + hashlib.sha256(data).hexdigest() + "\n"
    output.write_text(header + "\n\n".join(parts) + "\n", encoding="utf-8", newline="\n")
    print("Extracted 5 exact lifecycle blocks; bridge SHA256 " + hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    main()
