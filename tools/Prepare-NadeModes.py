"""Verify original Nade Modes attachments, apply the source patch, and build.

Download the three pinned author attachments with a browser when the forum
requires its browser check. This tool never substitutes an unverified mirror
or overwrites customized editable sources. Use --compile-only after editing.
"""
from pathlib import Path, PurePosixPath
import argparse
import difflib
import hashlib
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "amxx/nade_modes"
PATCH = ROOT / "patches/nademodes-reapi.patch"
MANIFEST = ROOT / "patches/nademodes-reapi.json"
ATTACHMENTS = [
    {"name": "nademodes.sma", "sha256": "17beafcfb59c73139913f212e87249d55341c7a7075db72c759035463f101cc5",
     "url": "https://forums.alliedmods.net/attachment.php?attachmentid=100910&d=1332418604"},
    {"name": "nademodes.txt", "sha256": "59b6d03f028eae335bf6e45a6fbbfd384a3dbb0817ae3190c6473ab6bae7f03b",
     "url": "https://forums.alliedmods.net/attachment.php?attachmentid=100328&d=1331160708"},
    {"name": "nademodes.inc", "sha256": "134b18778bc2dca9aa69b6022b29512b3aaaee0156596e35577cfd3273da8657",
     "url": "https://forums.alliedmods.net/attachment.php?attachmentid=98762&d=1327933943"},
]


def safe(path):
    path = path.absolute()
    if not path.is_relative_to(ROOT) or not path.resolve().is_relative_to(ROOT):
        raise ValueError("Nade Modes path escapes the workspace")
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError("Nade Modes paths must not contain reparse points")
        if part == ROOT:
            break
    return path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalized(data):
    # Normalize only source formatting, after checking the original byte hash.
    return ("\n".join(line.rstrip() for line in data.decode("utf-8-sig").splitlines()) + "\n").encode("utf-8")


def upstream_files(upstream):
    source = {}
    for entry in ATTACHMENTS:
        path = safe(upstream / entry["name"])
        if not path.is_file():
            raise FileNotFoundError(f"Use a browser to save {entry['url']} as {path.relative_to(ROOT)}")
        source[entry["name"]] = path.read_bytes()
        if digest(source[entry["name"]]) != entry["sha256"]:
            raise ValueError(f"Original attachment hash mismatch: {entry['name']}")
    return source


def export_patch(upstream):
    source, records, diffs = upstream_files(upstream), [], []
    for relative, original in (("nademodes.sma", "nademodes.sma"), ("include/nademodes.inc", "nademodes.inc")):
        seed = normalized(source[original])
        result = normalized(safe(WORK / relative).read_bytes())
        diffs.append("diff --git a/" + relative + " b/" + relative + "\n")
        diffs.extend(difflib.unified_diff(seed.decode().splitlines(keepends=True),
                                        result.decode().splitlines(keepends=True),
                                        fromfile="a/" + relative, tofile="b/" + relative))
        records.append({"path": relative, "upstreamName": original,
                        "seedSha256Normalized": digest(seed), "resultSha256Normalized": digest(result)})
    data = "".join(diffs).encode("utf-8")
    spec = {"format": 1, "upstream": {"project": "Nade Modes", "version": "11.2", "authors": "Nomexous & OT",
            "topic": "https://forums.alliedmods.net/showthread.php?t=75322", "license": "GPL-3.0-or-later",
            "attachments": ATTACHMENTS}, "normalization": "UTF-8 without BOM, LF, stripped line-end whitespace, final newline",
            "patchSha256": digest(data), "files": records}
    safe(PATCH).parent.mkdir(parents=True, exist_ok=True)
    PATCH.write_bytes(data)
    safe(MANIFEST).write_text(json.dumps(spec, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"patchBytes": len(data), "files": len(records), "sha256": digest(data)}))


def prepare(upstream, check_only=False):
    spec = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if spec["format"] != 1 or digest(PATCH.read_bytes()) != spec["patchSha256"]:
        raise ValueError("Nade Modes patch/manifest mismatch")
    if spec["upstream"]["attachments"] != ATTACHMENTS:
        raise ValueError("Unexpected upstream attachments in the patch manifest")
    source = upstream_files(upstream)
    before = {}
    license_path = safe(WORK / "LICENSE.txt")
    license_data = (ROOT / "notices/GPLv3.txt").read_bytes()
    if license_path.exists() and license_path.read_bytes() != license_data:
        raise ValueError("Preserving customized license file")
    for entry in spec["files"]:
        rel = PurePosixPath(entry["path"])
        if rel.is_absolute() or ".." in rel.parts or ":" in str(rel) or "\\" in str(rel):
            raise ValueError("Invalid patch path")
        path = safe(WORK / rel)
        data = path.read_bytes() if path.exists() else None
        before[path] = data
        if data is not None and digest(normalized(data)) not in (entry["seedSha256Normalized"], entry["resultSha256Normalized"]):
            raise ValueError(f"Preserving local edits in {path.relative_to(ROOT)}. Use --compile-only to build them.")
    scratch = safe(ROOT / "build/nademodes-patch")
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="verify-", dir=scratch) as directory:
        temp = safe(Path(directory))
        subprocess.run(["git", "init", "-q", str(temp)], check=True, capture_output=True)
        for entry in spec["files"]:
            destination = temp / entry["path"]
            seed = normalized(source[entry["upstreamName"]])
            if digest(seed) != entry["seedSha256Normalized"]:
                raise ValueError("Normalized upstream source mismatch")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(seed)
        command = ["git", "-c", "core.autocrlf=false", "-C", str(temp), "apply"]
        subprocess.run(command + ["--check", str(PATCH)], check=True, capture_output=True)
        subprocess.run(command + [str(PATCH)], check=True, capture_output=True)
        after = {safe(WORK / entry["path"]): (temp / entry["path"]).read_bytes() for entry in spec["files"]}
        if any(digest(normalized(after[WORK / entry["path"]])) != entry["resultSha256Normalized"] for entry in spec["files"]):
            raise ValueError("Patch replay did not reproduce the published source")
        if check_only:
            return {"replayed": len(after), "upstreamAttachments": len(source), "written": False}
        language = safe(WORK / "lang/nademodes.txt")
        before[language] = language.read_bytes() if language.exists() else None
        if before[language] is not None and before[language] != source["nademodes.txt"]:
            raise ValueError("Preserving customized upstream language file")
        after[language] = source["nademodes.txt"]
        before[license_path] = license_path.read_bytes() if license_path.exists() else None
        after[license_path] = license_data
        try:
            for path, data in after.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        except BaseException:
            for path, data in before.items():
                if data is None: path.unlink(missing_ok=True)
                else: path.write_bytes(data)
            raise
    return {"replayed": len(spec["files"]), "upstreamAttachments": len(source), "written": True}


def build():
    compiler = safe(ROOT / ".tools/amxx-1.9.0.5303/addons/amxmodx/scripting/amxxpc.exe")
    output = safe(ROOT / "build/amxx/plugins/nademodes.amxx")
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(compiler), str(WORK / "nademodes.sma"), "-i" + str(WORK / "include"),
               "-i" + str(ROOT / ".tools/reapi-5.29.0.358/addons/amxmodx/scripting/include"),
               "-i" + str(compiler.parent / "include"), "-o" + str(output)]
    subprocess.run(command, cwd=ROOT, check=True)
    sources = [WORK / "nademodes.sma", WORK / "include/goldcraft_nademodes.inc", WORK / "include/nademodes.inc"]
    runtime = [(output, "plugins/nademodes.amxx"),
               (WORK / "lang/nademodes.txt", "data/lang/nademodes.txt"),
               (WORK / "lang/nademodes_goldcraft.txt", "data/lang/nademodes_goldcraft.txt"),
               (WORK / "configs/nade_modes.cfg", "configs/nade_modes.cfg")]
    manifest = {"format": 1, "upstream": "Nade Modes 11.2", "amxx": "1.9.0.5303", "reapiSdk": "5.29.0.358",
                "sources": [{"path": path.relative_to(ROOT).as_posix(), "sha256": digest(path.read_bytes())} for path in sources],
                "runtime": [{"source": path.relative_to(ROOT).as_posix(), "destination": destination,
                             "sha256": digest(path.read_bytes())} for path, destination in runtime],
                "patchSha256": digest(PATCH.read_bytes())}
    destination = safe(ROOT / "dist/nademodes/manifest.json")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Built Nade Modes and its verified deployment manifest; no server files changed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, default=ROOT / "references/NadeModes")
    parser.add_argument("--check-patch", action="store_true")
    parser.add_argument("--compile-only", action="store_true")
    parser.add_argument("--export-patch", action="store_true", help="Regenerate the source-only patch after intentional edits to the working port")
    args = parser.parse_args()
    if sum((args.check_patch, args.compile_only, args.export_patch)) > 1:
        parser.error("Choose only one patch/check/build mode")
    if args.export_patch:
        export_patch(args.upstream)
        raise SystemExit(0)
    if not args.compile_only:
        print(json.dumps(prepare(args.upstream, args.check_patch)))
    if not args.check_patch:
        build()
