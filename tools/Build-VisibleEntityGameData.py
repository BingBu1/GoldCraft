"""Validate the exact copied engine and publish its visible-entity patch catalog."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import struct

ROOT = Path(__file__).resolve().parent.parent


def engine_bytes(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3c)[0]
    if data[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", data, pe+4)[0] != 0x14c:
        raise ValueError("Expected the verified x86 PE engine")
    count = struct.unpack_from("<H", data, pe+6)[0]
    optional = struct.unpack_from("<H", data, pe+20)[0]
    sections = [struct.unpack_from("<IIII", data, pe+24+optional+i*40+8) for i in range(count)]

    def read(rva, length):
        for _, base, size, offset in sections:
            if base <= rva and rva+length <= base+size:
                return data[offset+rva-base:offset+rva-base+length]
        raise ValueError(f"Unbacked engine RVA {rva:#x}")
    return read


def validate(path):
    config = json.loads((ROOT / "native/client/visible_entities_10210.json").read_text())
    identity = runpy.run_path(str(ROOT / "tools/Inspect-EngineIdentity.py"))["inspect"](path)
    if identity["sha256"] != config["sha256"] or identity["crc64xz"].lower() != config["crc64"]:
        raise ValueError("Unverified engine: refusing visible-entity patch catalog")
    read = engine_bytes(path)
    # Match MetaHook GameData.cpp NormalizeGlobal/NormalizeFunction, including
    # the original global operand identity. RVA-only globals are not a catalog.
    for record in config["records"]:
        payload = record["payload"]
        if record["kind"] == "global":
            for field in ("gv_rva", "gv_sig", "gv_sig_va", "gv_va", "gv_inst_offset", "gv_inst_disp", "gv_inst_length"):
                if not isinstance(payload.get(field), str):
                    raise ValueError(f"Incomplete global metadata: {record['symbolName']} / {field}")
            base = int(payload["gv_va"], 16) - int(payload["gv_rva"], 16)
            operand = int(payload["gv_sig_va"], 16) - base + int(payload["gv_inst_offset"], 16) + int(payload["gv_inst_disp"], 16)
            if struct.unpack("<I", read(operand, 4))[0] != int(payload["gv_va"], 16):
                raise ValueError(f"Global instruction operand mismatch: {record['symbolName']}")
        elif record["kind"] == "function":
            if int(payload.get("func_size", "0"), 16) <= 0:
                raise ValueError(f"Missing function span: {record['symbolName']}")
    for check in config["verification"]:
        expected = bytes.fromhex(check["bytes"])
        if read(int(check["rva"], 16), len(expected)) != expected:
            raise ValueError(f"Visible-entity instruction changed at {check['rva']}")
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/gamedata/goldcraft-visible")
    args = parser.parse_args()
    engine, output = args.engine.resolve(strict=True), args.output.resolve()
    if not engine.is_relative_to(ROOT) or not output.is_relative_to(ROOT):
        raise ValueError("Input and output must remain in the workspace")
    config = validate(engine)
    version = "goldcraft-visible-hl-10210"
    snapshot = {"schemaVersion": 5,
                "source": {"snapshotSchemaVersion": 8, "analysisOutputContractVersion": 3, "gameVersion": version},
                "binaries": {"engine": {"windows": {"crc64": config["crc64"]}}},
                "records": config["records"]}
    payload = (json.dumps(snapshot, indent=2) + "\n").encode()
    index = {"schemaVersion": 4, "versions": [{"gameVersion": version, "url": version + ".json",
             "sha256": hashlib.sha256(payload).hexdigest(), "size": len(payload),
             "snapshotSchemaVersion": 8, "fileCount": len(config["records"])}]}
    output.mkdir(parents=True, exist_ok=True)
    (output / (version + ".json")).write_bytes(payload)
    (output / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"symbols": len(config["records"]), "verifiedInstructions": len(config["verification"]),
                      "capacity": config["capacity"], "sha256": config["sha256"]}))


if __name__ == "__main__":
    main()
