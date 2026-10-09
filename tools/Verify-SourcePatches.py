"""Apply each published patch to its pristine pinned files, then compare the result.

Scratch repositories contain only the files changed by that patch. External
worktrees and runtime installations are read only for this verification.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent


def run(cwd, *args):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


def main():
    lock = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8"))
    scratch = ROOT / "build/source-patch-validation" / str(time.time_ns())
    scratch.mkdir(parents=True)
    reports = []
    for name, entry in lock["sources"].items():
        if "patch" not in entry:
            continue
        source, patch = ROOT / entry["path"], ROOT / entry["patch"]
        actual = run(source, "rev-parse", "HEAD").stdout.decode().strip()
        if actual != entry["commit"]:
            raise ValueError(f"Source revision mismatch: {name}")
        work = scratch / name
        work.mkdir()
        run(work, "init", "-q")
        before, after = set(), set()
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("--- a/"):
                relative = line[6:].split("\t", 1)[0]
                before.add(relative)
                target = work / relative
                if not target.resolve().is_relative_to(work):
                    raise ValueError("Patch path outside scratch repository")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(run(source, "show", entry["commit"] + ":" + relative).stdout)
            elif line.startswith("+++ b/"):
                relative = line[6:].split("\t", 1)[0]
                if not (work / relative).resolve().is_relative_to(work):
                    raise ValueError("Patch path outside scratch repository")
                after.add(relative)
        run(work, "apply", "--check", str(patch))
        run(work, "apply", str(patch))
        for relative in after:
            if (work / relative).read_bytes().replace(b"\r\n", b"\n") != (source / relative).read_bytes().replace(b"\r\n", b"\n"):
                raise ValueError(f"Patch does not reproduce current source: {name}/{relative}")
        for relative in before - after:
            if (work / relative).exists() or (source / relative).exists():
                raise ValueError(f"Deleted source still exists: {name}/{relative}")
        if not set(entry.get("newFiles", [])).issubset(after - before):
            raise ValueError(f"Patch omits a declared new source: {name}")
        result = {"component": name, "commit": entry["commit"], "files": len(before | after),
                  "added": len(after - before), "deleted": len(before - after),
                  "patchSha256": hashlib.sha256(patch.read_bytes()).hexdigest(), "passed": True}
        reports.append(result)
        print(json.dumps(result))
    output = ROOT / "analysis/publication/source-patches.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(reports, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
