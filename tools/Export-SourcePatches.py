"""Export only the source adaptations and new files listed in sources.lock.json."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent

def export(root, name, entry):
    source = root / entry["path"]
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != entry["commit"]:
        raise RuntimeError(f"Refusing a patch against an unpinned {name} revision")
    patch = subprocess.check_output(["git", "-c", "core.safecrlf=false", "diff", "--no-ext-diff", "--no-renames", "HEAD", "--"], cwd=source)
    for file in entry.get("newFiles", []):
        path = source / file
        if not path.resolve().is_relative_to(source.resolve()) or not path.is_file() or path.is_symlink():
            raise RuntimeError(f"Invalid new source path: {name}/{file}")
        exists = subprocess.run(["git", "cat-file", "-e", "HEAD:" + file], cwd=source, capture_output=True)
        if exists.returncode == 0:
            raise RuntimeError(f"Declared new source already exists upstream: {name}/{file}")
        result = subprocess.run(["git", "diff", "--no-index", "--", "/dev/null", file], cwd=source, capture_output=True)
        if result.returncode != 1:
            raise RuntimeError(f"Could not export the new source: {name}/{file}")
        patch += result.stdout
    if b"GIT binary patch" in patch or b"Binary files " in patch:
        raise RuntimeError(f"Refusing binary content in the {name} source adaptation")
    target = root / entry["patch"]
    if not target.resolve().is_relative_to((root / "patches").resolve()):
        raise RuntimeError("Patch destination outside the workspace patch directory")
    target.write_bytes(patch)
    print(f"{name}: {target.relative_to(root)}, {len(patch)} bytes")


def main():
    lock = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8"))
    entries = {name: entry for name, entry in lock["sources"].items() if "patch" in entry}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", nargs="+", choices=entries, help="Export only these components")
    args = parser.parse_args()
    for name in args.only or entries:
        export(ROOT, name, entries[name])


if __name__ == "__main__":
    main()
