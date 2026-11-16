#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute real server frame/snapshot and unmodified native client/server PM."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("seal", "adapter-dir", "native", "dependencies", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    sample = args.native.resolve(strict=True)
    dependencies = args.dependencies.resolve(strict=True)
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Use fresh workspace build output")
    if not sample.is_relative_to(ROOT / "analysis") or not dependencies.is_relative_to(ROOT / "sandbox"):
        raise ValueError("Use private analysis sample and isolated sandbox dependencies")
    if identity(sample)["sha256"] != "b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2":
        raise ValueError("Native client does not match the accepted IDA evidence")
    evidence = ROOT / "analysis/native-jump-prediction-20261115"
    sources = [sample, Path(__file__).resolve(), Path(__file__).with_name("native_pm.cpp"),
               evidence / "identity.json", evidence / "ida-move.json", evidence / "ida-init.json",
               evidence / "ida-jump.json"]
    before = [identity(p) for p in sources]
    out.mkdir()
    adapter = out / "adapter"
    adapter.mkdir()
    (adapter / "native_sample.hpp").write_text(
        "// Generated test adapter; no production source copy.\nnamespace native_sample {\n"
        + "inline constexpr char path[] = " + json.dumps(sample.as_posix()) + ";\n"
        + "inline constexpr char dependencies[] = " + json.dumps(dependencies.as_posix()) + ";\n}\n", "utf-8")
    command = [sys.executable, "-B", str(ROOT / "tests/csol-emergency-kick/run.py"),
               "--seal", str(args.seal.resolve(strict=True)), "--adapter-dir", str(args.adapter_dir.resolve(strict=True)),
               "--output", str(out / "sdk"), "--fixture", str(Path(__file__).with_name("native_pm.cpp")),
               "--fixture-include", str(adapter), "--groups", "walk", "run", "crouch", "jump", "wall", "freeze",
               "--sealed-production"]
    receipt = dict(success=False, sourceCopies=0, inputsBefore=before, command=command,
                   graphicalClient=False, networkTransport=False, realMapCollision=False, deployed=False,
                   originalCrouchFidelity=False,
                   boundary="Actual native client/server PM agreement on controlled planes; original CSO crouch field and movevars remain unresolved")
    try:
        process = subprocess.run(command, cwd=ROOT, timeout=900)
        receipt["exit"] = process.returncode
        sdk = json.loads((out / "sdk/receipt.json").read_text("utf-8"))
        receipt["sdkReceipt"] = identity(out / "sdk/receipt.json")
        receipt["success"] = process.returncode == 0 and sdk["success"] and sdk["inputsUnchanged"]
        receipt["groups"] = [{k: row[k] for k in ("name", "exit", "behaviorChecks", "sdkChecks", "failures") if k in row}
                             for row in sdk["steps"] if row["kind"] == "behavior"]
        if not receipt["success"]:
            raise RuntimeError("Complete native-PM SDK verification failed")
    finally:
        receipt["inputsUnchanged"] = before == [identity(p) for p in sources]
        receipt["success"] &= receipt["inputsUnchanged"]
        (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", "utf-8")


if __name__ == "__main__":
    main()
