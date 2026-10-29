"""Compare defined death-rule scalar inputs against copied HW integer instructions.

This reuses the reviewed bounded getter harness, never loads a native game DLL,
and explicitly substitutes host UCRT atoi only for non-overflowing ASCII input.
The complete MP table loader and runtime monster death are outside this test.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import subprocess


def identity(path):
    path = Path(path).resolve()
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--hw", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--getter", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location("death_original_csv_getter", args.getter)
    getter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(getter)
    original = getter.Original(args.hw, args.metadata)
    cases = [("0", 0), ("1", 1), ("-1", -1), ("+7.9", 7), ("  -42junk", -42),
        ("123xyz", 123), ("1e3", 1), (".5", 0), ("xyz", 0), ("#comment", 0),
        ("0x10", 0), ("+", 0), ("-", 0), ("+-1", 0), ("--1", 0),
        ("  ", 0), ("\t\r\n\v\f", 0), ("32768", 32768), ("65535", 65535),
        ("65536", 65536), ("70000", 70000), ("-32769", -32769),
        ("2147483647", 2147483647), ("-2147483648", -2147483648),
        ("00000000000000000005.5", 5), ("-0000012x", -12), ("+ 1", 0),
        ("5.5", 5), ("10.6", 10), ("0.1", 0), ("-5.5", -5), ("-0.1", 0)]
    # Empty input is deterministic at the getter but deliberately rejected by
    # the safe loader instead of consuming an uninitialized original local.
    texts = [text for text, unused in cases] + [""]
    oracle_input = args.output.with_name("oracle-input.txt")
    oracle_input.write_text("".join(text.encode("ascii").hex() + "\n" for text in texts), encoding="ascii")
    completed = subprocess.run([str(args.exe), "--oracle", str(oracle_input)],
        text=True, capture_output=True, check=False, timeout=30)
    args.output.with_name("oracle-stdout.txt").write_text(completed.stdout, encoding="utf-8")
    args.output.with_name("oracle-stderr.txt").write_text(completed.stderr, encoding="utf-8")
    assert completed.returncode == 0, completed
    production = [[int(value) for value in line.split()] for line in completed.stdout.splitlines()]
    assert len(production) == len(texts)
    records, failures = [], []
    for index, (text, expected) in enumerate(cases):
        result = original.get("int32", text)
        want = [index, 1, expected, int(expected != 0), int(expected != 0)]
        row = {"original": result, "production": production[index], "expectedProduction": want,
            "expectedInteger": expected}
        records.append(row)
        if result["value"] != expected or result["returnBool"] != 1 or production[index] != want:
            failures.append(row)
    for out_of_bounds in (False, True):
        result = original.get("int32", "" if not out_of_bounds else "unused", out_of_bounds=out_of_bounds)
        row = {"original": result, "expectedInteger": getter.SENTINEL, "expectedReturn": 0,
            "scope": "untouched getter output; safe table loader rejects empty/missing fields"}
        records.append(row)
        if result["value"] != getter.SENTINEL or result["returnBool"] != 0:
            failures.append(row)
    if production[-1] != [len(cases), 0]:
        failures.append({"emptyProduction": production[-1]})
    result = {"level": "copied HW int32 wrapper/getter/cell instructions plus actual production table parser",
        "nativeDLLLoaded": False, "liveGameTest": False, "fullMpLoaderExecuted": False,
        "overflowOriginalExecuted": False, "comparisons": len(cases), "getterAbsentChecks": 2,
        "productionEmptyRejected": production[-1] == [len(cases), 0], "failures": failures, "records": records,
        "substitution": "IAT 0x3800fd68 atoi supplied by host ucrtbase.dll; bounded defined ASCII inputs only",
        "inputs": [identity(p) for p in (args.hw, args.metadata, args.getter, args.exe, Path(__file__), oracle_input)],
        "windows": [{"address": hex(address), "bytes": count,
            "sha256": hashlib.sha256(original.original(address, count)).hexdigest()}
            for address, count in ((0x3732D270, 15), (0x37C15910, 87), (0x37C15B70, 69))]}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"comparisons": len(cases), "getterAbsentChecks": 2, "failures": len(failures)}))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
