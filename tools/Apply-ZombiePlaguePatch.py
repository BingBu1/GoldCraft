"""Apply the pinned ZP ReAPI migration without overwriting local source edits."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
PATCH = ROOT / "patches/zombieplague-reapi.patch"
MANIFEST = ROOT / "patches/zombieplague-reapi.json"


def normalized_hash(data):
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def safe(path):
    path = path.absolute()
    if not path.is_relative_to(ROOT) or not path.resolve().is_relative_to(ROOT):
        raise ValueError("ZP patch destination escapes the workspace")
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError(f"Refusing a reparse path: {parent}")
        if parent == ROOT:
            break
    return path


def apply_migration(working, check_only=False):
    working = safe(working)
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest["format"] != 1 or hashlib.sha256(PATCH.read_bytes()).hexdigest() != manifest["patchSha256"]:
        raise ValueError("ZP ReAPI patch manifest mismatch")
    entries = manifest["files"]
    before = {}
    for entry in entries:
        relative = PurePosixPath(entry["path"])
        if relative.is_absolute() or ".." in relative.parts or ":" in str(relative) or "\\" in str(relative):
            raise ValueError("Invalid ZP patch source path")
        path = safe(working / relative)
        before[path] = path.read_bytes() if path.exists() else None
    exact = all(data is not None and normalized_hash(data) == entry["resultSha256LF"]
                for entry, data in zip(entries, before.values()))
    if exact:
        return {"state": "already-applied", "files": len(entries), "localEdits": False}

    scratch = safe(ROOT / "build/zp-patch")
    scratch.mkdir(parents=True, exist_ok=True)
    # Git's check runs on a private copy. A conflicting/customized source tree
    # stays byte-for-byte unchanged, including partially applied migrations.
    with tempfile.TemporaryDirectory(prefix="apply-", dir=scratch) as directory:
        temp = safe(Path(directory))
        subprocess.run(["git", "init", "-q", str(temp)], check=True, capture_output=True)
        for entry, data in zip(entries, before.values()):
            if data is not None:
                target = temp / entry["path"]
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data.replace(b"\r\n", b"\n"))

        def run(*options):
            return subprocess.run(["git", "-c", "core.autocrlf=false", "-C", str(temp), "apply", "--ignore-space-change",
                                   *options, str(PATCH)], capture_output=True, text=True)

        forward = run("--check")
        if forward.returncode:
            if run("--reverse", "--check").returncode == 0:
                return {"state": "already-applied", "files": len(entries), "localEdits": True}
            raise ValueError("ZP migration conflicts with the editable sources; no patch files were changed.\n"
                             + forward.stderr.strip())
        if check_only:
            return {"state": "applicable", "files": len(entries), "localEdits": any(
                data is not None and normalized_hash(data) != entry["seedSha256LF"]
                for entry, data in zip(entries, before.values()))}
        applied = run()
        if applied.returncode:
            raise RuntimeError("ZP migration failed in its private copy: " + applied.stderr.strip())
        after = {path: (temp / entry["path"]).read_bytes().replace(b"\r\n", b"\n")
                 for entry, path in zip(entries, before)}
        # Commit the preflighted files together and restore originals if a write
        # fails. Preserve the original line-ending convention of edited files.
        try:
            for path, data in after.items():
                original = before[path]
                if original is not None and b"\r\n" in original:
                    data = data.replace(b"\n", b"\r\n")
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        except BaseException:
            for path, data in before.items():
                if data is None:
                    path.unlink(missing_ok=True)
                else:
                    path.write_bytes(data)
            raise
    return {"state": "applied", "files": len(entries), "localEdits": any(
        normalized_hash(path.read_bytes()) != entry["resultSha256LF"] for entry, path in zip(entries, before))}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    print(json.dumps(apply_migration(ROOT / "amxx/zombie_plague", args.check)))
