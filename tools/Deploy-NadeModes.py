"""Install the verified Nade Modes bytecode/resources in the dedicated sandbox.

Preserve existing configuration and all other plugins. Pawn reloads on a map
change, not sv_restart. No editable source or test plugins go into the server.
"""
from pathlib import Path
import argparse
import codecs
import hashlib
import importlib.util
import json
import re
import time
import zipfile

ROOT = Path(__file__).resolve().parent.parent
RUNTIME = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx"
EXPECTED = {
    "plugins/nademodes.amxx": "build/amxx/plugins/nademodes.amxx",
    "data/lang/nademodes.txt": "amxx/nade_modes/lang/nademodes.txt",
    "data/lang/nademodes_goldcraft.txt": "amxx/nade_modes/lang/nademodes_goldcraft.txt",
    "configs/nade_modes.cfg": "amxx/nade_modes/configs/nade_modes.cfg",
}
SOURCES = {"amxx/nade_modes/nademodes.sma", "amxx/nade_modes/include/nademodes.inc",
           "amxx/nade_modes/include/goldcraft_nademodes.inc"}


def safe(path):
    path = path.absolute()
    if not path.is_relative_to(ROOT) or not path.resolve().is_relative_to(ROOT):
        raise ValueError("Nade Modes deployment escapes the workspace")
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError("Nade Modes deployment contains a reparse point")
        if part == ROOT:
            break
    return path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def plan():
    manifest = json.loads(safe(ROOT / "dist/nademodes/manifest.json").read_text(encoding="utf-8"))
    if manifest["format"] != 1 or manifest["amxx"] != "1.9.0.5303":
        raise ValueError("Rebuild with the pinned AMXX compiler")
    if manifest["patchSha256"] != digest(safe(ROOT / "patches/nademodes-reapi.patch").read_bytes()):
        raise ValueError("Source patch changed after the build")
    if {entry["path"] for entry in manifest["sources"]} != SOURCES or len(manifest["sources"]) != len(SOURCES):
        raise ValueError("Unexpected source manifest")
    for entry in manifest["sources"]:
        if digest(safe(ROOT / entry["path"]).read_bytes()) != entry["sha256"]:
            raise ValueError("Rebuild changed sources before deployment")
    resources = manifest["runtime"]
    if {entry["destination"] for entry in resources} != set(EXPECTED) or len(resources) != len(EXPECTED):
        raise ValueError("Unexpected runtime manifest")
    updates, preserved = {}, []
    previous = ROOT / "sandbox/cs-server/nademodes-deployment.json"
    previous_hashes = {}
    if previous.exists():
        previous_hashes = json.loads(safe(previous).read_text(encoding="utf-8")).get("installed", {})
    for entry in resources:
        relative = entry["destination"]
        if entry["source"] != EXPECTED[relative]:
            raise ValueError("Only the known Nade Modes runtime resources may be installed")
        source, target = safe(ROOT / entry["source"]), safe(RUNTIME / relative)
        data = source.read_bytes()
        if digest(data) != entry["sha256"]:
            raise ValueError("Rebuild changed resources before deployment")
        if target.exists():
            current = target.read_bytes()
            if relative.startswith("configs/"):
                preserved.append(relative)
                continue
            if relative.startswith("data/") and current != data and digest(current) != previous_hashes.get(relative):
                raise ValueError(f"Preserving customized translation: {relative}")
            if current == data:
                continue
        updates[target] = data

    primary = safe(RUNTIME / "configs/plugins.ini")
    if not primary.is_file():
        raise FileNotFoundError("Initialize AMXX on the dedicated sandbox first")
    # The primary list loads before ZP's secondary lists. Nade Modes must
    # register its grenade Think stage before ZP consumes the explosion.
    pattern = rb"(?im)^[ \t]*nademodes\.amxx(?:[ \t][^\r\n]*)?(?:\r?\n|$)"
    for path in safe(RUNTIME / "configs").glob("plugins*.ini"):
        if path != primary and re.search(pattern, safe(path).read_bytes()):
            raise ValueError("Nade Modes is also active in a secondary plugin list; preserve that list for review")
    content = primary.read_bytes()
    bom = codecs.BOM_UTF8 if content.startswith(codecs.BOM_UTF8) else b""
    body = content[len(bom):]
    newline = b"\r\n" if b"\r\n" in body else b"\n"
    body = re.sub(pattern, b"", body)
    updated = bom + b"nademodes.amxx" + newline + body
    if updated != content:
        updates[primary] = updated
    return manifest, updates, preserved


def deploy(check_only=False, reload_map=False):
    manifest, updates, preserved = plan()
    relative = lambda path: path.relative_to(RUNTIME).as_posix()
    report = {"format": 1, "amxx": manifest["amxx"], "reapiSdk": manifest["reapiSdk"],
              "patchSha256": manifest["patchSha256"], "preserved": preserved,
              "changed": sorted(relative(path) for path in updates), "mapReloadRequested": reload_map,
              "time": time.time(), "checkOnly": check_only}
    if check_only:
        print(json.dumps(report, ensure_ascii=False))
        return report
    before = {path: path.read_bytes() if path.exists() else None for path in updates}
    if before:
        backups = safe(ROOT / "sandbox/cs-server/nademodes-deployment-backups")
        backups.mkdir(parents=True, exist_ok=True)
        latest, older = safe(backups / "previous-1.zip"), safe(backups / "previous-2.zip")
        if latest.exists():
            older.write_bytes(latest.read_bytes())
        with zipfile.ZipFile(latest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr("manifest.json", json.dumps({relative(p): None if data is None else digest(data)
                                                         for p, data in before.items()}))
            for path, data in before.items():
                if data is not None:
                    archive.writestr(relative(path), data)
    pending = []
    try:
        for path, data in updates.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            temporary = safe(path.with_name(path.name + ".pending"))
            with temporary.open("xb") as file:
                pending.append(temporary)
                file.write(data)
            temporary.replace(path)
        if any(path.read_bytes() != data for path, data in updates.items()):
            raise RuntimeError("Nade Modes deployment verification failed")
    except BaseException:
        for path, data in before.items():
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)
        raise
    finally:
        for path in pending:
            path.unlink(missing_ok=True)
    report["installed"] = {name: digest(safe(RUNTIME / name).read_bytes()) for name in EXPECTED}
    report["pluginListSha256"] = digest((RUNTIME / "configs/plugins.ini").read_bytes())
    output = safe(ROOT / "sandbox/cs-server/nademodes-deployment.json")
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if reload_map:
        spec = importlib.util.spec_from_file_location("nm_rcon", ROOT / "tools/GoldSrc-Command.py")
        rcon = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rcon)
        report["reloadResponse"] = rcon.command("changelevel cs_assault")
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Nade Modes: {len(updates)} files installed; existing configuration preserved; map reload={reload_map}.")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-only", action="store_true", help="Verify and list the concrete deployment without writing runtime files")
    parser.add_argument("--reload-map", action="store_true", help="Load Pawn with changelevel cs_assault; clears this MC map session")
    args = parser.parse_args()
    if args.check_only and args.reload_map:
        parser.error("--check-only cannot reload the map")
    deploy(args.check_only, args.reload_map)
