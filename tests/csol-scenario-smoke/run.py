#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute the prepared 1-job smoke plan only after the shared window is released."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess


def identity(path):
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", required=True, type=Path)
    parser.add_argument("--execute", action="store_true", help="Run only after parent releases the build window")
    args = parser.parse_args()
    out = args.plan.resolve(strict=True)
    plan_path = out / "plan.json"
    plan = json.loads(plan_path.read_text(encoding="utf-8-sig"))
    workspace = Path(plan["workspace"]).resolve(strict=True)
    if not out.is_relative_to(workspace / "build") or plan["maxJobs"] != 1:
        parser.error("The plan must be an isolated workspace build with maxJobs=1")
    if not args.execute:
        print(json.dumps({"executed": False, "commands": len(plan["commands"]), "maxJobs": 1,
                          "reason": "Await explicit parent window release, then pass --execute."}))
        return
    if (out / "receipt.json").exists() or (out / "running.lock").exists():
        parser.error("This plan has already run or is running; preserve its evidence")
    for node in (out, *out.parents):
        if node.is_symlink() or (hasattr(node, "is_junction") and node.is_junction()):
            parser.error("Linked output path is not allowed")
    known = {str(Path(item["path"]).resolve()).casefold(): item for item in plan["files"]}

    def verify():
        drift = []
        for item in plan["files"]:
            path = Path(item["path"])
            actual = identity(path) if path.is_file() else None
            if actual is None or actual["bytes"] != item["bytes"] or actual["sha256"] != item["sha256"]:
                drift.append(item["path"])
        return drift

    before = verify()
    if before:
        (out / "input-drift-before.json").write_text(json.dumps(before, indent=2), encoding="utf-8")
        raise RuntimeError("Inputs drifted before execution; no command was run")
    (out / "running.lock").write_text(str(os.getpid()), encoding="ascii")
    environment = os.environ.copy()
    environment.update(plan["environment"])
    environment["PATH"] = str(Path(next(row["path"] for row in plan["files"] if row["path"].lower().endswith("clang-cl.exe"))).parent) + os.pathsep + environment.get("PATH", "")
    result = {"at": datetime.datetime.now(datetime.timezone.utc).isoformat(), "plan": identity(plan_path),
              "maxJobs": 1, "inputsUnchangedBefore": True, "steps": [], "actualIncludes": [], "actualLibraries": [],
              "deployed": False, "liveGameplay": False, "limits": plan["limits"]}
    error = None
    try:
        for step in plan["commands"]:
            completed = subprocess.run(step["command"], cwd=out, env=environment, capture_output=True,
                                       text=True, encoding="utf-8", errors="replace")
            log = completed.stdout + completed.stderr
            logfile = out / (step["name"] + ".log")
            logfile.write_text(log, encoding="utf-8")
            entry = {"name": step["name"], "kind": step["kind"], "exit": completed.returncode,
                     "expectedExit": step["expectedExit"], "log": identity(logfile)}
            result["steps"].append(entry)
            if step["kind"] == "compile":
                paths = re.findall(r"Note: including file:\s*(.+)", log)
                for value in paths:
                    path = Path(value.strip()).resolve(strict=True)
                    key = str(path).casefold()
                    if key not in known:
                        raise RuntimeError("Unprepared actual include: " + str(path))
                    result["actualIncludes"].append(key)
            elif step["kind"] == "link":
                # LLVM COFF /verbose emits archive input paths as Reading lines.
                paths = re.findall(r"^(?:lld-link:\s*)?Reading\s+(.+?\.lib)\s*$", log, re.I | re.M)
                if not paths and completed.returncode == 0:
                    raise RuntimeError("Link succeeded without a captured archive input list")
                for value in paths:
                    path = Path(value.strip().strip('"')).resolve(strict=True)
                    key = str(path).casefold()
                    if key not in known:
                        raise RuntimeError("Unprepared actual link library: " + str(path))
                    result["actualLibraries"].append(key)
            if completed.returncode != step["expectedExit"]:
                raise RuntimeError(step["name"] + " returned an unexpected exit status")
            if step["kind"] == "test":
                summary = re.search(r"scenario-smoke-(?:event-order|routing)\s+(\d+) checks\s+(\d+) failures", log)
                if not summary:
                    raise RuntimeError("Missing smoke fixture result: " + step["name"])
                entry["checks"], entry["failures"] = map(int, summary.groups())
                if step["expectedExit"] == 0 and entry["failures"] != 0:
                    raise RuntimeError("Positive fixture reports failures")
                if step["expectedExit"] != 0:
                    if entry["failures"] < len(step["expectedFailureLines"]):
                        raise RuntimeError("Negative control did not reproduce enough routing failures")
                    for expected in step["expectedFailureLines"]:
                        if expected not in log:
                            raise RuntimeError("Negative control missed the intended regression: " + expected)
            (out / "progress.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
            print(step["name"], completed.returncode, flush=True)
    except Exception as exc:
        error = str(exc)
    finally:
        result["actualIncludes"] = [known[key] for key in sorted(set(result["actualIncludes"]))]
        result["actualLibraries"] = [known[key] for key in sorted(set(result["actualLibraries"]))]
        result["inputDriftAfter"] = verify()
        result["inputsUnchangedAfter"] = not result["inputDriftAfter"]
        result["error"] = error
        result["artifacts"] = [identity(path) for path in out.iterdir()
                               if path.is_file() and path.suffix.lower() in (".obj", ".exe", ".log")]
        (out / "receipt.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        (out / "running.lock").unlink()
    if error or result["inputDriftAfter"]:
        raise RuntimeError(error or "Inputs changed during execution")


if __name__ == "__main__":
    main()
