"""Compare all encrypted-reader fields with independent Python CSV expectations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from consumer_compare import compare, identity


def verify(row):
    assert hashlib.sha256(Path(row["path"]).read_bytes()).hexdigest() == row["sha256"]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    captures = args.output.parent / "field-captures"
    captures.mkdir(exist_ok=True)
    records, failures, stats = [], [], {"checks": 0}
    for row in manifest["records"]:
        for key in ("expected", "source", "decoded"):
            verify(row[key])
        expected = json.loads(Path(row["expected"]["path"]).read_text(encoding="utf-8"))
        command = [str(args.exe.resolve()), "--inspect", row["kind"], row["source"]["path"]]
        result = subprocess.run(command, capture_output=True, timeout=30)
        capture = captures / (row["map"] + "-" + row["kind"] + ".json")
        capture.write_bytes(result.stdout)
        local, count = [], {"checks": 0}
        if result.returncode:
            local.append({"returnCode": result.returncode, "stderr": result.stderr.decode("utf-8", errors="replace")})
        else:
            compare(expected, json.loads(result.stdout), "$", local, count)
        verify(row["source"])
        stats["checks"] += count["checks"]
        entry = {"map": row["map"], "kind": row["kind"], "command": command,
            "expected": row["expected"], "actual": identity(capture),
            "checks": count["checks"], "passed": not local, "failures": local,
            "returnCode": result.returncode, "stderr": result.stderr.decode("utf-8", errors="replace")}
        records.append(entry)
        if local:
            failures.append({"map": row["map"], "kind": row["kind"], "failures": local})
    output = {"passed": not failures, **stats, "failures": failures, "records": records,
        "manifest": identity(args.manifest), "exe": identity(args.exe),
        "comparisonSource": identity(Path(__file__)),
        "scope": "All production encrypted-reader bytes offsets fields typed rows and groups; separate independent CSV oracle; no entities/gameplay"}
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": output["passed"], "files": len(records), **stats, "failedFiles": len(failures)}))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
