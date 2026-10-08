"""Publish verified CS client symbols for the sandbox's MetaHook catalog.

These addresses come from IDA analysis of the exact module below. A different
client must be analyzed separately; this tool never scans or guesses offsets.
"""
import argparse
import hashlib
import json
import runpy
import struct
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXPECTED_SHA256 = "b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2"
EXPECTED_CRC64 = "124D6034A6B28B40"
IMAGE_BASE = 0x10000000
VTABLE_RVA = 0xCF164
# Name, vtable slot, function RVA, IDA function extent.
FUNCTIONS = (
    ("StudioDrawModel", 2, 0x66B50, 0x47A),
    ("StudioDrawPlayer", 3, 0x4B450, 0x362),
    ("StudioSetupBones", 6, 0x4BF00, 0xCF3),
    ("StudioSaveBones", 8, 0x68620, 0xB7),
    ("StudioMergeBones", 9, 0x67D70, 0x280),
    ("StudioRenderModel", 18, 0x68580, 0x98),
    ("StudioRenderFinal", 19, 0x68390, 0x18),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=ROOT / "analysis/engine/cs-client-10210/client.dll")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/gamedata/goldcraft-cs")
    parser.add_argument("--existing-catalog", type=Path, help="Omit matching symbols already supplied by the pinned common plugins")
    args = parser.parse_args()
    client = args.client.resolve(strict=True)
    output = args.output.resolve()
    if not client.is_relative_to(ROOT) or not output.is_relative_to(ROOT):
        raise ValueError("Read an independent workspace copy and publish inside the workspace")
    inspector = runpy.run_path(str(ROOT / "tools/Inspect-EngineIdentity.py"))
    identity = inspector["inspect"](client)
    if identity["sha256"] != EXPECTED_SHA256 or identity["crc64xz"] != EXPECTED_CRC64:
        raise ValueError("Unanalyzed CS client identity; refusing to publish unrelated addresses")
    data = client.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    sections = []
    for i in range(section_count):
        off = pe + 24 + optional_size + i * 40
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", data, off + 8)
        sections.append((rva, raw_size, raw_offset))

    def raw(rva, size):
        for base, length, offset in sections:
            if base <= rva and rva + size <= base + length:
                return data[offset + rva - base:offset + rva - base + size]
        raise ValueError(f"RVA {rva:#x} not in a file-backed section")

    records = []
    for name, slot, rva, size in FUNCTIONS:
        target = struct.unpack("<I", raw(VTABLE_RVA + slot * 4, 4))[0]
        if target != IMAGE_BASE + rva:
            raise ValueError(f"Verified vtable slot changed: {name}")
        raw(rva, size)
        records.append({
            "platform": "windows", "module": "client",
            "symbolName": "GameStudioRenderer_" + name, "kind": "virtualFunction",
            "payload": {"vfunc_index": slot, "vtable_name": "GameStudioRenderer",
                        "func_rva": hex(rva), "func_size": hex(size)},
        })

    # ScoreInfo's store of frags: mov word ptr [eax+g_PlayerExtraInfo], bx.
    # ScoreAttrib independently confirms VIP +12, dead +60, defuse +112,
    # with 116-byte stride. Teamnumber is +42; HUD_GetPlayerTeam uses team_id +4.
    global_rva, reference_rva = 0x1257D8, 0xACC54
    instruction = raw(reference_rva, 7)
    if instruction[:3] != b"\x66\x89\x98" or struct.unpack_from("<I", instruction, 3)[0] != IMAGE_BASE + global_rva:
        raise ValueError("Player array reference no longer matches the verified instruction")
    records.append({
        "platform": "windows", "module": "client", "symbolName": "g_PlayerExtraInfo", "kind": "global",
        "payload": {
            "gv_rva": hex(global_rva), "gv_va": hex(IMAGE_BASE + global_rva),
            "gv_sig": "66 89 98 ?? ?? ?? ??", "gv_sig_va": hex(IMAGE_BASE + reference_rva),
            "gv_inst_offset": "0x0", "gv_inst_disp": "0x3", "gv_inst_length": "0x7",
        },
    })
    # Matched Brass parser: the annotation at5315a is the result store;
    # the actual READ_SHORT CALL starts at53155. Preserve all other shorts.
    brass_call, read_short, read_long, bad_read = 0x53155, 0x5D1C0, 0x5D150, 0x127C08
    instruction = raw(brass_call, 5)
    if instruction != b"\xe8\x66\xa0\x00\x00" or brass_call + 5 + struct.unpack_from("<i", instruction, 1)[0] != read_short:
        raise ValueError("Brass media read is no longer the verified CALL")
    if raw(0x5D1D2, 10) != b"\xc7\x05" + struct.pack("<I", IMAGE_BASE + bad_read) + b"\x01\x00\x00\x00":
        raise ValueError("Client message bad-read store changed")
    for name, rva, size in (("ReadShort", read_short, 0x4A), ("ReadLong", read_long, 0x53)):
        raw(rva, size)
        records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraft_CS_" + name,
                        "kind": "function", "payload": {"func_rva": hex(rva), "func_size": hex(size)}})
    records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraft_CS_BrassModelRead",
                    "kind": "patch", "payload": {"patch_rva": hex(brass_call)}})
    records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraft_CS_BadRead", "kind": "global",
                    "payload": {"gv_rva": hex(bad_read), "gv_va": hex(IMAGE_BASE + bad_read),
                                "gv_sig": "C7 05 ?? ?? ?? ?? 01 00 00 00", "gv_sig_va": hex(IMAGE_BASE + 0x5D1D2),
                                "gv_inst_offset": "0x0", "gv_inst_disp": "0x2", "gv_inst_length": "0xa"}})
    # ShadowIdx reads a long, then the verified setter stores eax directly.
    # Observe the real client field to catch truncation in server-side storage.
    shadow_index, shadow_store = 0x12E974, 0x686E6
    if raw(shadow_store, 5) != b"\xa3" + struct.pack("<I", IMAGE_BASE + shadow_index):
        raise ValueError("Client ShadowIdx setter changed")
    records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraft_CS_ShadowSprite", "kind": "global",
                    "payload": {"gv_rva": hex(shadow_index), "gv_va": hex(IMAGE_BASE + shadow_index),
                                "gv_sig": "A3 ?? ?? ?? ??", "gv_sig_va": hex(IMAGE_BASE + shadow_store),
                                "gv_inst_offset": "0x0", "gv_inst_disp": "0x1", "gv_inst_length": "0x5"}})
    provided = set()
    if args.existing_catalog:
        catalog = args.existing_catalog.resolve(strict=True)
        if not catalog.is_relative_to(ROOT):
            raise ValueError("Inspect only an independent workspace catalog")
        wanted = {record["symbolName"]: record for record in records}
        for path in catalog.rglob("*.json"):
            if "goldcraft-cs" in path.relative_to(catalog).parts:
                continue
            snapshot = json.loads(path.read_text(encoding="utf-8-sig"))
            crc = snapshot.get("binaries", {}).get("client", {}).get("windows", {}).get("crc64", "")
            if crc.lower() != EXPECTED_CRC64.lower():
                continue
            for candidate in snapshot.get("records", []):
                name = candidate.get("symbolName")
                if name not in wanted or candidate.get("module") != "client" or candidate.get("platform") != "windows":
                    continue
                verified = wanted[name]
                address_key = {"global": "gv_rva", "patch": "patch_rva"}.get(verified["kind"], "func_rva")
                payload = candidate.get("payload", {})
                if (candidate.get("kind") != verified["kind"] or
                    int(str(payload.get(address_key, "-1")), 0) != int(str(verified["payload"][address_key]), 0) or
                    (verified["kind"] == "virtualFunction" and payload.get("vfunc_index") != verified["payload"]["vfunc_index"])):
                    raise ValueError(f"Upstream symbol contradicts the verified matching client: {name}")
                provided.add(name)
        records = [record for record in records if record["symbolName"] not in provided]
    version = "cs-client-10210-goldcraft"
    snapshot = {
        "schemaVersion": 5,
        "source": {"snapshotSchemaVersion": 8, "analysisOutputContractVersion": 3, "gameVersion": version},
        "binaries": {"client": {"windows": {"crc64": EXPECTED_CRC64.lower()}}},
        "records": records,
    }
    snapshot_bytes = (json.dumps(snapshot, indent=2) + "\n").encode("utf-8")
    index = {"schemaVersion": 4, "versions": [{
        "gameVersion": version, "url": version + ".json",
        "sha256": hashlib.sha256(snapshot_bytes).hexdigest(), "size": len(snapshot_bytes),
        "snapshotSchemaVersion": 8, "fileCount": len(records), "lastPublishTime": datetime.now(timezone.utc).isoformat(),
    }]}
    output.mkdir(parents=True, exist_ok=True)
    (output / (version + ".json")).write_bytes(snapshot_bytes)
    (output / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"clientSha256": identity["sha256"], "clientCrc64": EXPECTED_CRC64,
                      "verifiedSymbols": len(records) + len(provided), "supplementalSymbols": len(records),
                      "providedByCommonPlugins": sorted(provided), "output": str(output)}))


if __name__ == "__main__":
    main()
