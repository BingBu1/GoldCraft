"""Execute the production encrypted readers against independent common fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    matrix = json.loads(args.matrix.read_text(encoding="utf-8-sig"))
    assert matrix["passed"]
    records, failures = [], []
    for source in matrix["records"]:
        assert digest(source["source"]) == source["sourceSHA256"]
        assert digest(source["output"]) == source["decodedSHA256"]
        kind = source["kind"] if source["ordinaryParserExpected"] else "reject-" + source["kind"]
        command = [str(args.exe.resolve()), "--resource", kind, source["source"],
                   source["output"], str(source["dataRows"])]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        match = re.search(r"resources checks=(\d+) failures=(\d+)", result.stdout)
        unchanged = digest(source["source"]) == source["sourceSHA256"] and digest(source["output"]) == source["decodedSHA256"]
        passed = result.returncode == 0 and match is not None and int(match[2]) == 0 and unchanged
        records.append({"map": source["map"], "kind": kind, "command": command,
            "returnCode": result.returncode, "stdout": result.stdout, "stderr": result.stderr,
            "checks": int(match[1]) if match else None, "passed": passed,
            "expectedRows": source["dataRows"], "unchanged": unchanged,
            "rawSHA256": source["sourceSHA256"], "decodedSHA256": source["decodedSHA256"]})
        if not passed:
            failures.append({"map": source["map"], "kind": kind})
    output = {"passed": not failures, "failures": failures, "records": records,
        "inputMatrix": {"path": str(args.matrix.resolve()), "sha256": digest(args.matrix)},
        "productionExe": {"path": str(args.exe.resolve()), "sha256": digest(args.exe)},
        "testSource": {"path": str(Path(__file__).resolve()), "sha256": digest(__file__)},
        "commonWrites": False, "archivesRead": False, "nativeDLLLoaded": False,
        "scope": "Exact raw decode and encrypted resource integration, not game execution"}
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"files": len(records), "checks": sum(row["checks"] or 0 for row in records),
                      "passed": output["passed"], "failures": failures}))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
