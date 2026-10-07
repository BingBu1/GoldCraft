"""Prune copied download caches and old generated logs in stopped sandbox copies.

Never follow junctions, touch the protected game, or delete edited copied files.
Without --apply this only writes a reviewable plan and reports its size.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import time

from Modpack import ROOT, read_json, safe

BASE = ROOT / "sandbox"
POLICY = read_json(ROOT / "sandbox-policy.json")


def process_inventory():
    """Check native copies and Java from either loader; never expose command lines."""
    root = str(ROOT).replace("'", "''")
    base = str(BASE).replace("'", "''")
    command = (
        "[Console]::OutputEncoding=[Text.UTF8Encoding]::new(); "
        "$p=@(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { "
        f"$_.ExecutablePath -like '{base}\\*' -or "
        f"($_.Name -match '^java(w)?\\.exe$' -and ($_.ExecutablePath -like '{root}\\*' "
        f"-or $_.CommandLine -like '*{root}*')) "
        "} | Select-Object ProcessId,Name,CommandLine); ConvertTo-Json -InputObject $p -Compress"
    )
    result = subprocess.run(["powershell.exe", "-NoProfile", "-Command", command],
                            capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError("Cannot verify that sandbox game processes have stopped")
    # Build daemons do not consume sandbox runtime files. Do not exclude another
    # Minecraft loader: cleanup covers both Fabric and NeoForge copies.
    return [{key: item[key] for key in ("ProcessId", "Name")}
            for item in json.loads(result.stdout or "[]")
            if not re.search(r"org\.gradle\.(?:launcher|wrapper)\.", item.get("CommandLine") or "")]


def files_below(directory):
    if not directory.exists():
        return
    safe(directory, BASE)
    pending = [directory]
    while pending:
        current = pending.pop()
        with os.scandir(current) as entries:
            for entry in entries:
                info = entry.stat(follow_symlinks=False)
                if entry.is_symlink() or getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                    raise RuntimeError(f"Refusing to traverse reparse point: {entry.path}")
                path = Path(entry.path)
                if entry.is_dir(follow_symlinks=False):
                    pending.append(path)
                else:
                    yield path, info


def plan():
    for name in POLICY["excludedCopiedDirectories"]:
        if not re.fullmatch(r"[A-Za-z0-9_-]+", name):
            raise ValueError("Copy exclusions must be top-level directory names")
    for key in ("keepRunsPerRole", "keepDeploymentBackups"):
        if not isinstance(POLICY[key], int) or not 1 <= POLICY[key] <= 100:
            raise ValueError(f"{key} must be between 1 and 100")
    baseline = {entry["path"].replace("\\", "/").lower(): entry
                for entry in read_json(ROOT / "analysis/installation/original-baseline.json")["files"]}
    result = []
    for instance in ("cs-client-a", "cs-client-b", "cs-server"):
        game = BASE / instance / "Half-Life"
        for name in POLICY["excludedCopiedDirectories"]:
            for path, info in files_below(game / name):
                original = baseline.get(path.relative_to(game).as_posix().lower())
                if original is None or info.st_size != original["bytes"]:
                    continue  # Preserve local additions and edits.
                result.append({"path": path.relative_to(BASE).as_posix(), "bytes": info.st_size,
                    "modifiedNs": info.st_mtime_ns, "sha256": original["sha256"].lower(),
                    "reason": "unchanged copied download/unrelated-game cache"})
        qconsole = game / "qconsole.log"
        if qconsole.is_file():
            info = qconsole.stat()
            result.append({"path": qconsole.relative_to(BASE).as_posix(), "bytes": info.st_size,
                "modifiedNs": info.st_mtime_ns, "reason": "generated native console transcript"})
        groups = defaultdict(list)
        for path, info in files_below(BASE / instance / "logs"):
            match = re.fullmatch(r"(CsClient|CsServer|MinecraftClient|MinecraftServer|MinecraftGameTest)-\d{8}-\d{6}\.(stdout|stderr)\.log", path.name)
            if match:
                groups[match.groups()].append((path, info))
        for entries in groups.values():
            entries.sort(key=lambda item: item[1].st_mtime_ns, reverse=True)
            for path, info in entries[POLICY["keepRunsPerRole"]:]:
                result.append({"path": path.relative_to(BASE).as_posix(), "bytes": info.st_size,
                    "modifiedNs": info.st_mtime_ns, "reason": "old generated run log"})
    backups = BASE / "deployment-backups"
    if backups.exists():
        directories = [safe(p, BASE) for p in backups.iterdir() if p.is_dir()]
        directories.sort(key=lambda p: p.stat().st_mtime_ns, reverse=True)
        for directory in directories[POLICY["keepDeploymentBackups"]:]:
            for path, info in files_below(directory):
                result.append({"path": path.relative_to(BASE).as_posix(), "bytes": info.st_size,
                    "modifiedNs": info.st_mtime_ns, "reason": "older deployment backup; newest two retained"})
    return result


def apply(records):
    if process_inventory():
        raise RuntimeError("Stop sandbox CS/ReHLDS and Minecraft processes before compacting")
    deleted, skipped = [], []
    last_report = time.monotonic()
    for record in records:
        path = safe(BASE / record["path"], BASE)
        if not path.is_file():
            continue
        info = path.stat()
        if info.st_size != record["bytes"] or info.st_mtime_ns != record["modifiedNs"]:
            skipped.append({"path": record["path"], "reason": "changed since plan"})
            continue
        if "sha256" in record:
            with path.open("rb") as stream:
                actual = hashlib.file_digest(stream, "sha256").hexdigest()
            if actual != record["sha256"]:
                skipped.append({"path": record["path"], "reason": "edited copy retained"})
                continue
        # Validate the absolute destination and ancestors again at the mutation.
        safe(path, BASE).unlink()
        deleted.append(record)
        if time.monotonic() - last_report > 10:
            print(json.dumps({"removedFiles": len(deleted), "removedBytes": sum(r["bytes"] for r in deleted)}), flush=True)
            last_report = time.monotonic()
    # Only empty ancestors of files we removed, up to their individual instance.
    parents = {parent for record in deleted for parent in (BASE / record["path"]).parents
               if parent.is_relative_to(BASE) and parent != BASE and parent.parent != BASE}
    for parent in sorted(parents, key=lambda p: len(p.parts), reverse=True):
        safe(parent, BASE)
        try:
            parent.rmdir()
        except OSError:
            pass
    return {"deleted": deleted, "skipped": skipped, "removedBytes": sum(r["bytes"] for r in deleted)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    records = plan()
    report = {"plannedFiles": len(records), "plannedBytes": sum(r["bytes"] for r in records), "files": records}
    output = ROOT / "analysis/sandbox-cleanup"
    safe(output, ROOT).mkdir(parents=True, exist_ok=True)
    stamp = str(time.time_ns())
    target = output / f"{'cleanup' if args.apply else 'plan'}-{stamp}.json"
    target.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"plannedFiles": len(records), "plannedBytes": report["plannedBytes"], "report": str(target)}), flush=True)
    if args.apply:
        report.update(apply(records))
        target.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(json.dumps({"removedBytes": report["removedBytes"], "skipped": len(report["skipped"]), "report": str(target)}))
