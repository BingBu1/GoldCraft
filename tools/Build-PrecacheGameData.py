"""Build the precache catalog only for the independently verified engine copy."""
import argparse
import hashlib
import json
import runpy
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/gamedata/goldcraft-precache")
    args = parser.parse_args()
    engine, output = args.engine.resolve(strict=True), args.output.resolve()
    if not engine.is_relative_to(ROOT) or not output.is_relative_to(ROOT):
        raise ValueError("Engine input and catalog output must be inside the workspace")
    config = json.loads((ROOT / "native/client/precache_10210.json").read_text(encoding="utf-8"))
    identity = runpy.run_path(str(ROOT / "tools/Inspect-EngineIdentity.py"))["inspect"](engine)
    if (identity["sha256"] != config["sha256"] or
            identity["crc64xz"].lower() != config["crc64"]):
        raise ValueError("Unanalyzed engine identity; no precache offsets will be published")
    data = engine.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    sections = [struct.unpack_from("<IIII", data, pe + 24 + optional + i * 40 + 8) for i in range(count)]

    def read_rva(rva, length):
        for virtual_size, base, size, offset in sections:
            if base <= rva and rva + length <= base + size:
                return data[offset + rva - base:offset + rva - base + length]
        raise ValueError(f"Unbacked verification RVA {rva:#x}")

    for check in config["verification"]:
        expected = bytes.fromhex(check["bytes"])
        if read_rva(int(check["rva"], 16), len(expected)) != expected:
            raise ValueError(f"Reviewed instruction changed at {check['rva']}")
    records = {record["symbolName"]: record for record in config["records"]}
    readers = 0
    for name, record in records.items():
        if not name.startswith("GoldCraft_read_"):
            continue
        rva = int(record["payload"]["patch_rva"], 16)
        code = read_rva(rva, 5)
        target_name = "GoldCraft_MSG_ReadBits" if name == "GoldCraft_read_sound_index" else "GoldCraft_MSG_ReadShort"
        target = int(records[target_name]["payload"]["func_rva"], 16)
        if code[0] != 0xe8 or rva + 5 + struct.unpack_from("<i", code, 1)[0] != target:
            raise ValueError(f"Media reader is not a CALL to its verified decoder: {name}")
        readers += 1
    version = "goldcraft-precache-hl-10210"
    snapshot = {
        "schemaVersion": 5,
        "source": {"snapshotSchemaVersion": 8, "analysisOutputContractVersion": 3, "gameVersion": version},
        "binaries": {"engine": {"windows": {"crc64": config["crc64"]}}},
        "records": config["records"],
    }
    payload = (json.dumps(snapshot, indent=2) + "\n").encode("utf-8")
    index = {"schemaVersion": 4, "versions": [{
        "gameVersion": version, "url": version + ".json", "sha256": hashlib.sha256(payload).hexdigest(),
        "size": len(payload), "snapshotSchemaVersion": 8, "fileCount": len(snapshot["records"]),
    }]}
    output.mkdir(parents=True, exist_ok=True)
    (output / (version + ".json")).write_bytes(payload)
    (output / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"sha256": identity["sha256"], "symbols": len(snapshot["records"]),
                      "verifiedSpans": len(config["verification"]), "readerCalls": readers}))


if __name__ == "__main__":
    main()
