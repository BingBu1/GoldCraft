"""Update only swds.dll/PDB and its registration in the local dedicated sandbox."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
SERVER = ROOT / "sandbox/cs-server"
GAME = SERVER / "Half-Life"


def safe(path):
    path = Path(path).absolute()
    if not path.resolve().is_relative_to(ROOT):
        raise ValueError("Deployment path escapes workspace")
    current = path
    while current != ROOT:
        if current.is_symlink() or (hasattr(current, "is_junction") and current.is_junction()):
            raise ValueError("Deployment path contains a reparse point")
        current = current.parent
    return path


def identity(path):
    data = safe(path).read_bytes()
    return {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}


def atomic_copy(source, target):
    source, target = safe(source), safe(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        shutil.copy2(source, temporary)
        if temporary.stat().st_nlink != 1 or identity(temporary) != identity(source):
            raise ValueError("Independent temporary copy verification failed")
        if target.exists() and os.name == "nt":
            replace = ctypes.WinDLL("kernel32", use_last_error=True).ReplaceFileW
            replace.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p,
                                ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p]
            replace.restype = ctypes.c_int
            if not replace(str(target), str(temporary), None, 0, None, None):
                raise ctypes.WinError(ctypes.get_last_error())
        else:
            os.replace(temporary, target)
    finally:
        if temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "build/rehlds/Release")
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    source = safe(args.source)
    receipt_path = safe(SERVER / "rehlds-deployment.json")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8-sig"))
    for entry in receipt["files"]:
        actual = identity(GAME / entry["name"])
        if actual["sha256"].lower() != entry["sha256"].lower() or actual["bytes"] != entry["bytes"]:
            raise ValueError("Runtime differs from registration: " + entry["name"])
    names = ("swds.dll", "swds.pdb")
    plan = {name: {"before": identity(GAME / name), "after": identity(source / name)} for name in names}
    if not args.apply:
        print(json.dumps({"applied": False, "files": plan}, indent=2))
        return
    query = "Get-CimInstance Win32_Process | Select-Object -ExpandProperty ExecutablePath"
    running = subprocess.check_output(["pwsh", "-NoProfile", "-Command", query], text=True, encoding="utf-8-sig")
    if any(line.lower().startswith(str(GAME).lower() + os.sep) for line in running.splitlines()):
        raise ValueError("Stop the dedicated sandbox before updating its engine")
    output = safe(ROOT / "build/rehlds")
    output.mkdir(parents=True, exist_ok=True)
    backup = safe(SERVER / "backups" / ("engine-" + plan["swds.dll"]["before"]["sha256"][:16]))
    backup.mkdir(parents=True, exist_ok=True)
    for name in (*names, "rehlds-deployment.json"):
        original = receipt_path if name.endswith(".json") else GAME / name
        saved = backup / name
        if saved.exists() and identity(saved) != identity(original):
            raise ValueError("Existing engine backup differs; preserved for inspection")
        if not saved.exists():
            atomic_copy(original, saved)
    changed = []
    try:
        for name in names:
            if identity(source / name) != plan[name]["after"]:
                raise ValueError("Build output changed after planning")
            atomic_copy(source / name, GAME / name)
            changed.append(name)
        for entry in receipt["files"]:
            if entry["name"] == "swds.dll":
                entry.update(plan["swds.dll"]["after"])
        receipt["engineSourceCommit"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        receipt["engineSourceUncommitted"] = True
        candidate = output / "deployment-registration.json"
        candidate.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        atomic_copy(candidate, receipt_path)
    except Exception:
        for name in reversed(changed):
            atomic_copy(backup / name, GAME / name)
        atomic_copy(backup / "rehlds-deployment.json", receipt_path)
        raise
    (output / "deployment.json").write_text(json.dumps({"applied": True, "files": plan,
        "backup": str(backup), "scope": "Local sandbox engine only; no gameplay assertion"}, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"applied": True, "engineSha256": identity(GAME / "swds.dll")["sha256"]}))


if __name__ == "__main__":
    main()
