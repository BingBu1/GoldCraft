"""Create small, independently named media for the real ReHLDS/B precache test.

Only 1,150 sprites, 1,150 WAVs and 2,150 generic files are generated. The opt-in
engine fixture reserves empty slots to exercise IDs 65535/65536 without 65k
copies. No generated media belongs in the public source repository.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parent.parent
STAGE = ROOT / "dist/precache-fixture"
INSTANCES = ("cs-server", "cs-client-b")


def guarded(path):
    path = path.absolute()
    if not path.is_relative_to(ROOT) or not path.resolve().is_relative_to(ROOT):
        raise ValueError("Fixture path escapes the workspace")
    for parent in (path, *path.parents):
        if parent.exists() and getattr(parent.lstat(), "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError(f"Fixture path traverses a reparse point: {parent}")
        if parent == ROOT:
            break
    return path


def stopped():
    command = "Get-CimInstance Win32_Process | Where-Object {$_.Name -in @('hlds.exe','MetaHook.exe')} | Select-Object -ExpandProperty ExecutablePath"
    paths = subprocess.check_output(["pwsh", "-NoProfile", "-Command", command], text=True)
    for instance in INSTANCES:
        if str(ROOT / "sandbox" / instance).lower() in paths.lower():
            raise RuntimeError("Stop the recorded ReHLDS/B processes before fixture installation/removal")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sprite(number):
    width = height = 32
    header = struct.pack("<4siiifiiifi", b"IDSP", 2, 2, 3, 23.0, width, height, 1, 0.0, 0)
    palette = bytearray(256 * 3)
    palette[3:6] = bytes((255, 32 + number % 192, 240))
    palette[6:9] = bytes((32, 224, 64 + number % 192))
    pixels = bytes(1 + ((x // 4 + y // 4 + number) % 2) if 2 < x < 29 and 2 < y < 29 else 255
                   for y in range(height) for x in range(width))
    # A private marker in an unused palette entry makes each test file distinct.
    palette[9:12] = (number + 1).to_bytes(3, "little")
    frame = struct.pack("<iiiii", 0, -16, 16, width, height)
    return header + struct.pack("<H", 256) + palette + frame + pixels


def sound(number, seconds=0.03):
    output = io.BytesIO()
    rate = 11025
    count = round(seconds * rate)
    samples = []
    for n in range(count):
        envelope = min(1.0, n / 100, (count - n) / 100)
        samples.append(round(12000 * envelope * math.sin(2 * math.pi * (440 + number) * n / rate)))
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(struct.pack(f"<{count}h", *samples))
    return output.getvalue()


def generate():
    guarded(STAGE).mkdir(parents=True, exist_ok=True)
    entries = []

    def write(name, data):
        target = guarded(STAGE / name)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        entries.append({"path": name, "sha256": digest(data), "bytes": len(data)})

    for number in range(1150):
        write(f"sprites/gc_probe/m{number:04d}.spr", sprite(number))
        write(f"sound/gc_probe/s{number:04d}.wav", sound(number))
    for number in range(2150):
        write(f"gc_probe/g{number:04d}.txt", f"GoldCraft generic resource {number}\n".encode())
    model = guarded(ROOT / "sandbox/cs-client-b/Half-Life/cstrike/models/w_hegrenade.mdl")
    data = model.read_bytes()
    if data[:8] != b"IDST\x0a\0\0\0":
        raise ValueError("The sandbox model must be GoldSrc Studio version10")
    write("models/gc_probe/high.mdl", data)
    write("sprites/gc_probe/high.spr", sprite(65536))
    write("sound/gc_probe/high_a.wav", sound(440, 0.8))
    write("sound/gc_probe/high_b.wav", sound(880, 0.8))
    report = {"schema": 1, "files": entries, "note": "1150 independent low resources per media type; sparse slots test65535/65536"}
    (STAGE / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Prepared {len(entries)} files, {sum(e['bytes'] for e in entries):,} bytes")
    return report


def deploy(report, remove=False):
    stopped()
    # Validate the complete set before any removal or overwrite.
    work = []
    for instance in INSTANCES:
        game = guarded(ROOT / "sandbox" / instance / "Half-Life/cstrike")
        for entry in report["files"]:
            relative = entry["path"]
            if not relative.startswith(("sprites/gc_probe/", "models/gc_probe/", "sound/gc_probe/", "gc_probe/")):
                raise ValueError("Unexpected fixture manifest path")
            source, target = guarded(STAGE / relative), guarded(game / relative)
            if target.exists() and digest(target.read_bytes()) != entry["sha256"]:
                raise RuntimeError(f"Preserving modified fixture file: {target}")
            if not remove and digest(source.read_bytes()) != entry["sha256"]:
                raise RuntimeError(f"Staging hash mismatch: {relative}")
            work.append((source, target))
    for source, target in work:
        if remove:
            if target.exists():
                target.unlink()
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    print(f"{'Removed' if remove else 'Installed'} {len(work)} verified fixture files in server/B only")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "install", "remove"))
    args = parser.parse_args()
    if args.action == "prepare":
        generate()
    else:
        report = json.loads(guarded(STAGE / "manifest.json").read_text())
        deploy(report, args.action == "remove")


if __name__ == "__main__":
    main()
