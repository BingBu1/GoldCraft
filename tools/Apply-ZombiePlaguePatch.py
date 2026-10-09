"""Apply the pinned ZP ReAPI migration without overwriting local source edits."""
import argparse
import difflib
import hashlib
import importlib.util
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


def seed_sources():
    """Use the pristine package plus the published Chinese seed transformation.

    Never derive a cumulative patch from an already patched working copy.
    Only the 71 active upstream plugins and their headers belong in this patch;
    original GoldCraft integration/tests are published as standalone sources.
    """
    def module(filename):
        spec = importlib.util.spec_from_file_location(filename, ROOT / "tools" / (filename + ".py"))
        value = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(value)
        return value

    prepare = module("Prepare-ZombiePlague")
    localized, _, _ = module("Localize-ZombiePlague").prepare()
    scripting = prepare.SOURCE / "addons/amxmodx/scripting"
    result = {}
    for path in sorted(scripting.rglob("*.sma")):
        relative = prepare.source_category(path.stem) + "/" + path.name
        if relative in result:
            raise ValueError("Ambiguous ZP upstream source: " + relative)
        # Preparation builds the active ammopack set, not every alternative.
        if not (ROOT / "amxx/zombie_plague" / relative).is_file():
            continue
        result[relative] = safe(localized.get(path.stem, path)).read_bytes().replace(b"\r\n", b"\n")
    for path in sorted((scripting / "include").glob("*.inc")):
        result["include/" + path.name] = safe(path).read_bytes().replace(b"\r\n", b"\n")
    # The earlier migration introduced one original shared ReAPI header.
    for entry in json.loads(MANIFEST.read_text(encoding="utf-8"))["files"]:
        if entry["path"] not in result and entry["seedSha256LF"] in (None, normalized_hash(b"")):
            result[entry["path"]] = b""
    return result


def replay_patch(seeds=None):
    seeds = seeds if seeds is not None else seed_sources()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if hashlib.sha256(PATCH.read_bytes()).hexdigest() != manifest["patchSha256"]:
        raise ValueError("ZP patch hash mismatch")
    scratch = safe(ROOT / "build/zp-patch")
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="replay-", dir=scratch) as directory:
        temp = safe(Path(directory))
        subprocess.run(["git", "init", "-q", str(temp)], check=True, capture_output=True)
        for entry in manifest["files"]:
            data = seeds[entry["path"]]
            if (normalized_hash(data) if data else None) != entry["seedSha256LF"]:
                raise ValueError("ZP seed changed: " + entry["path"])
            if not data:
                continue  # A new file in the cumulative patch.
            destination = safe(temp / entry["path"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
        command = ["git", "-c", "core.autocrlf=false", "-C", str(temp), "apply"]
        for options in (["--check"], []):
            applied = subprocess.run(command + options + [str(PATCH)], capture_output=True, text=True)
            if applied.returncode:
                raise ValueError("ZP patch replay failed: " + applied.stderr.strip())
        for entry in manifest["files"]:
            if normalized_hash((temp / entry["path"]).read_bytes()) != entry["resultSha256LF"]:
                raise ValueError("ZP patch replay mismatch: " + entry["path"])
    return {"replayed": len(manifest["files"]), "workingFilesWritten": False}


def export_patch(working):
    seeds, entries, diff = seed_sources(), [], []
    previous = json.loads(MANIFEST.read_text(encoding="utf-8"))
    # Existing seed hashes anchor the package and localization revisions. An
    # upstream version change needs a separate, explicit migration.
    for entry in previous["files"]:
        seed = seeds[entry["path"]]
        if (normalized_hash(seed) if seed else None) != entry["seedSha256LF"]:
            raise ValueError("Preserving patch: upstream seed changed: " + entry["path"])
    for relative, seed in sorted(seeds.items()):
        path = safe(working / relative)
        result = path.read_bytes().replace(b"\r\n", b"\n")
        if seed == result:
            continue
        diff.append("diff --git a/" + relative + " b/" + relative + "\n")
        if not seed:
            diff.append("new file mode 100644\n")
        for line in difflib.unified_diff(seed.decode("utf-8").splitlines(keepends=True),
                                        result.decode("utf-8").splitlines(keepends=True),
                                        fromfile="a/" + relative if seed else "/dev/null", tofile="b/" + relative):
            diff.append(line if line.endswith("\n") else line + "\n\\ No newline at end of file\n")
        entries.append({"path": relative, "seedSha256LF": normalized_hash(seed) if seed else None,
                        "resultSha256LF": normalized_hash(result)})
    patch = "".join(diff).encode("utf-8")
    manifest = dict(previous, patchSha256=hashlib.sha256(patch).hexdigest(), files=entries)
    old_patch, old_manifest = PATCH.read_bytes(), MANIFEST.read_bytes()
    try:
        safe(PATCH).write_bytes(patch)
        safe(MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return replay_patch(seeds)
    except BaseException:
        PATCH.write_bytes(old_patch)
        MANIFEST.write_bytes(old_manifest)
        raise


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
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--export-patch", action="store_true", help="Export and replay intentional working-source edits")
    mode.add_argument("--replay", action="store_true", help="Verify reconstruction from pristine package/localized seeds")
    args = parser.parse_args()
    if args.export_patch:
        result = export_patch(ROOT / "amxx/zombie_plague")
    elif args.replay:
        result = replay_patch()
    else:
        result = apply_migration(ROOT / "amxx/zombie_plague", args.check)
    print(json.dumps(result))
