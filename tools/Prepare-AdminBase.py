"""Rebuild the editable AMXX 1.9 Admin Base source from its pinned package.

Only source, dictionaries and the original license are prepared. No accounts,
credentials, bytecode or server configuration are written by this tool.
"""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SDK = ROOT / ".tools/amxx-1.9.0.5303/addons/amxmodx"
WORK = ROOT / "amxx/administration"
PATCH = ROOT / "patches/amxx-admin-modern.patch"
MANIFEST = ROOT / "patches/amxx-admin-modern.json"
FILES = {"admin.sma": "scripting/admin.sma", "lang/admin.txt": "data/lang/admin.txt",
         "lang/common.txt": "data/lang/common.txt", "LICENSE.txt": "LICENSE.txt"}


def safe(path):
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Admin source path escapes workspace")
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError("Reparse point in admin source path")
        if parent == ROOT:
            break
    return path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def normalized(data):
    return data.replace(b"\r\n", b"\n")


def replay(manifest):
    if manifest["amxx"] != "1.9.0.5303" or sha(PATCH.read_bytes()) != manifest["patchSha256"]:
        raise ValueError("Admin patch identity mismatch")
    if {entry["path"] for entry in manifest["files"]} != set(FILES):
        raise ValueError("Unexpected admin source manifest")
    output = {}
    scratch = safe(ROOT / "build/admin-patch")
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as temporary:
        temp = Path(temporary)
        subprocess.run(["git", "init", "-q", str(temp)], check=True, capture_output=True)
        for entry in manifest["files"]:
            data = normalized(safe(SDK / FILES[entry["path"]]).read_bytes())
            if sha(data) != entry["seedSha256LF"]:
                raise ValueError("Prepare the matching AMXX package before applying the patch")
            path = temp / entry["path"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        subprocess.run(["git", "-c", "core.autocrlf=false", "-C", str(temp), "apply", str(PATCH)],
                       check=True, capture_output=True)
        for entry in manifest["files"]:
            data = (temp / entry["path"]).read_bytes()
            if sha(data) != entry["resultSha256LF"]:
                raise ValueError("Replayed admin source differs from the expected result")
            output[entry["path"]] = data
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--export-patch", action="store_true")
    mode.add_argument("--check-patch", action="store_true")
    args = parser.parse_args()
    if args.export_patch:
        entries, diff = [], []
        for name, seed in FILES.items():
            before = normalized(safe(SDK / seed).read_bytes())
            after = normalized(safe(WORK / name).read_bytes())
            entries.append({"path": name, "seedSha256LF": sha(before), "resultSha256LF": sha(after)})
            if before != after:
                diff.extend(difflib.unified_diff(before.decode("utf-8").splitlines(keepends=True),
                                               after.decode("utf-8").splitlines(keepends=True),
                                               "a/" + name, "b/" + name))
        patch = "".join(diff).encode("utf-8")
        manifest = {"format": 1, "amxx": "1.9.0.5303", "patchSha256": sha(patch), "files": entries}
        old_patch = PATCH.read_bytes() if PATCH.exists() else None
        try:
            safe(PATCH).write_bytes(patch)
            replay(manifest)
        except BaseException:
            if old_patch is None:
                PATCH.unlink(missing_ok=True)
            else:
                PATCH.write_bytes(old_patch)
            raise
        safe(MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    else:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        output = replay(manifest)
        if not args.check_patch:
            for entry in manifest["files"]:
                path = safe(WORK / entry["path"])
                if path.exists() and sha(normalized(path.read_bytes())) not in (entry["seedSha256LF"], entry["resultSha256LF"]):
                    raise ValueError(f"Preserving modified admin source: {entry['path']}")
            for name, data in output.items():
                path = safe(WORK / name)
                if not path.exists() or normalized(path.read_bytes()) != data:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
    print(json.dumps({"replayed": len(manifest["files"]), "accountsWritten": False}))


if __name__ == "__main__":
    main()
