"""Deploy the built, active ZP plugins to the dedicated sandbox without changing its configuration."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import time
import zipfile

ROOT = Path(__file__).resolve().parent.parent
RUNTIME = ROOT / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx"


def safe(path):
    path = path.absolute()
    if not path.is_relative_to(ROOT) or not path.resolve().is_relative_to(ROOT):
        raise ValueError("Deployment path escapes workspace")
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError("Reparse point in deployment path")
        if part == ROOT:
            break
    return path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reload-map", action="store_true", help="Load the new Pawn bytecode with changelevel cs_assault")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "dist/zombieplague/manifest.json").read_text(encoding="utf-8"))
    plugins = manifest["plugins"]
    names = {entry["name"] for entry in plugins}
    enabled = set()
    for path in safe(RUNTIME / "configs").glob("plugins*.ini"):
        enabled.update(re.findall(r"^\s*([a-z0-9_]+)\.amxx(?:\s|$)", path.read_text(errors="replace"), re.M))
    if len(names) != len(plugins) or not names or not names <= enabled:
        raise ValueError("Build manifest must contain only the currently enabled ZP plugins")
    if any(name.startswith("goldcraft_") or not re.fullmatch(r"[a-z0-9_]+", name) for name in names):
        raise ValueError("Test/integration plugins must not be included in the ZP deployment")
    updates, before = {}, {}
    for entry in plugins:
        source = safe(ROOT / entry["source"])
        artifact = safe(ROOT / entry["artifact"])
        if not source.is_relative_to(ROOT / "amxx/zombie_plague") or artifact != ROOT / "build/amxx/plugins" / (entry["name"] + ".amxx"):
            raise ValueError("Unexpected source/artifact in ZP manifest")
        data = artifact.read_bytes()
        if digest(source.read_bytes()) != entry["sourceSha256"] or digest(data) != entry["sha256"]:
            raise ValueError("Rebuild the changed ZP sources before deployment")
        target = safe(RUNTIME / "plugins" / artifact.name)
        before[target] = target.read_bytes()
        updates[target] = data
    resources = {path: digest(path.read_bytes()) for folder in ("configs", "data/lang")
                 for path in safe(RUNTIME / folder).rglob("*") if path.is_file()}
    backups = safe(ROOT / "sandbox/cs-server/zp-deployment-backups")
    backups.mkdir(parents=True, exist_ok=True)
    latest, older = safe(backups / "previous-1.zip"), safe(backups / "previous-2.zip")
    if latest.exists():
        older.write_bytes(latest.read_bytes())
    with zipfile.ZipFile(latest, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path, data in before.items():
            archive.writestr(path.name, data)
    try:
        for path, data in updates.items():
            pending = safe(path.with_suffix(".amxx.pending"))
            pending.write_bytes(data)
            pending.replace(path)
        if any(path.read_bytes() != data for path, data in updates.items()):
            raise RuntimeError("ZP deployment hash mismatch")
    except BaseException:
        for path, data in before.items():
            path.write_bytes(data)
        raise
    report = {"plugins": [{"name": entry["name"], "sha256": entry["sha256"]} for entry in plugins],
              "configurationAndTranslationsUnchanged": all(digest(path.read_bytes()) == value for path, value in resources.items()),
              "mapReloadRequested": args.reload_map, "time": time.time()}
    if not report["configurationAndTranslationsUnchanged"]:
        raise RuntimeError("Server source resources changed during deployment")
    if args.reload_map:
        spec = importlib.util.spec_from_file_location("zp_rcon", ROOT / "tools/GoldSrc-Command.py")
        rcon = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rcon)
        report["reloadResponse"] = rcon.command("changelevel cs_assault")
    output = safe(ROOT / "sandbox/cs-server/zp-reapi-deployment.json")
    output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Deployed {len(plugins)} ZP plugins; configuration/translations preserved; map reload={args.reload_map}.")


if __name__ == "__main__":
    main()
