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
    for name in ("MetaHook", "Renderer", "ReGameDLL_CS", "ReHLDS", "SyPB"):
        entry = lock["sources"][name]
        source, patch = ROOT / entry["path"], ROOT / entry["patch"]
        actual = run(source, "rev-parse", "HEAD").stdout.decode().strip()
        if actual != entry["commit"]:
            raise ValueError(f"Source revision mismatch: {name}")
        work = scratch / name
        work.mkdir()
        run(work, "init", "-q")
        names = []
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("--- a/"):
                relative = line[6:].split("\t", 1)[0]
                target = work / relative
                if not target.resolve().is_relative_to(work):
                    raise ValueError("Patch path outside scratch repository")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(run(source, "show", entry["commit"] + ":" + relative).stdout)
            elif line.startswith("+++ b/"):
                names.append(line[6:].split("\t", 1)[0])
        run(work, "apply", "--check", str(patch))
        run(work, "apply", str(patch))
        for relative in names:
            if (work / relative).read_bytes().replace(b"\r\n", b"\n") != (source / relative).read_bytes().replace(b"\r\n", b"\n"):
                raise ValueError(f"Patch does not reproduce current source: {name}/{relative}")
        result = {"component": name, "commit": entry["commit"], "files": len(names),
                  "patchSha256": hashlib.sha256(patch.read_bytes()).hexdigest(), "passed": True}
        reports.append(result)
        print(json.dumps(result))
    output = ROOT / "analysis/publication/source-patches.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(reports, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
