"""Compare monster_impact::Decide with the original CSOL client oracle.

The oracle (analysis/.../impact-oracle.json) executed client 02568CF0 under
Unicorn with controlled engine APIs. Mode 15 is the original scenario branch;
mode 0 is its ordinary-game branch. Test stdin encodes these as 3/0; the
production bridge no longer uses an entity marker. Usage: run.py <decide.exe>
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ORACLE = ROOT / "analysis/csol-model-20261017/scenario/monster-impact-presentation/impact-oracle.json"
MODULE = "f4145136edbd229c219ea793bb3a6dcacea0b406ccc1e610138abbb8063bcbfd"
MARKER = 3


def main():
    oracle = json.loads(ORACLE.read_text(encoding="utf-8"))
    if oracle["module"] != MODULE or oracle["start"] != "02568CF0":
        raise SystemExit("unexpected oracle module")
    lines, expected, labels = [], [], []
    for row in oracle["rows"]:
        args = dict(solid=3, modelclass=0, flags=0, hitgroup=1, name="monster", gamemode=15)
        args.update(row.get("arguments", {}))
        bullet = row.get("bullet", 12)
        marker = MARKER if args["gamemode"] == 15 else 0
        lines.append(f'{marker} {ord(args["name"][0])} {int(bool(args["flags"] & 0x20000))} '
                     f'{args["modelclass"]} {args["solid"]} {args["hitgroup"]} {bullet}')
        effects = row["effects"]
        if len(effects) > 1:
            raise SystemExit(f"unexpected multiple effects: {row}")
        if not effects:
            expected.append("none 0")
        elif effects[0]["kind"] == "blood":
            e = effects[0]
            if e["color"] != -1 or e["models"] != [-1, -1] or e["count"] != -1 or e["position"] != [12.0, 34.0, 56.0]:
                raise SystemExit(f"unexpected blood arguments: {row}")
            expected.append(f'blood {int(e["size"])}')
        else:
            arguments = effects[0]["arguments"]
            if arguments[1:] != [0, 500, 0]:
                raise SystemExit(f"unexpected spark arguments: {row}")
            expected.append(f"spark {arguments[0]}")
        labels.append(row.get("case", f"bullet {bullet}"))
    result = subprocess.run([sys.argv[1]], input="\n".join(lines) + "\n", capture_output=True, text=True)
    sys.stderr.write(result.stderr)
    if result.returncode:
        raise SystemExit(f"decide failed: {result.returncode}")
    actual = result.stdout.split("\n")[:-1]
    failures = [(label, want, got) for label, want, got in zip(labels, expected, actual) if want != got]
    if len(actual) != len(expected) or failures:
        for failure in failures:
            print("MISMATCH", *failure)
        raise SystemExit(f"{len(failures)} mismatches, {len(actual)}/{len(expected)} rows")
    print(json.dumps(dict(oracleRows=len(expected), passed=len(expected), module=MODULE)))


if __name__ == "__main__":
    main()
