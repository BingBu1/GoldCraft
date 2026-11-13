"""Publish verified CS client symbols for the sandbox's MetaHook catalog.

These addresses come from IDA analysis of the exact module below. A different
client must be analyzed separately; this tool never scans or guesses offsets.
"""
import argparse
import hashlib
import json
import runpy
import struct
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
    ("StudioGetAnim", 4, 0x67CD0, 0x9A),
    ("StudioSetupBones", 6, 0x4BF00, 0xCF3),
    ("StudioSaveBones", 8, 0x68620, 0xB7),
    ("StudioMergeBones", 9, 0x67D70, 0x280),
    ("StudioCalcRotations", 17, 0x66910, 0x23D),
    ("StudioRenderModel", 18, 0x68580, 0x98),
    ("StudioRenderFinal", 19, 0x68390, 0x18),
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, default=ROOT / "sandbox/cs-client-b/Half-Life/cstrike/cl_dlls/client.dll")
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
    # Particle camera host mapping. Original CSOL camera selection is separate
    # from its ordinary-pool time cache. These globals are the matched native
    # client's actual observer and two independently updated view-angle vectors.
    # IDA: HUD_ProcessPlayerState100489A0; normal-view slice1006BBFA..1006BC48.
    particle_fields = (
        ("Observer", 0x110E10, 0x48B12, bytes.fromhex("a3 10 0e 11 10"), 1),
        ("RenderAngles", 0x134AF8, 0x6BC09, bytes.fromhex("f3 0f 11 05 f8 4a 13 10"), 4),
        ("ChaseAngles", 0x134AEC, 0x6BC30, bytes.fromhex("f3 0f 11 05 ec 4a 13 10"), 4),
    )
    for name, rva, reference, expected, operand in particle_fields:
        if raw(reference, len(expected)) != expected:
            raise ValueError("Matched particle camera reference changed: " + name)
        signature = " ".join(f"{byte:02X}" if index < operand else "??"
                             for index, byte in enumerate(expected))
        records.append({"platform": "windows", "module": "client",
                        "symbolName": "GoldCraftParticle_" + name, "kind": "global",
                        "payload": {"gv_rva": hex(rva), "gv_va": hex(IMAGE_BASE + rva),
                            "gv_sig": signature, "gv_sig_va": hex(IMAGE_BASE + reference),
                            "gv_inst_offset": "0x0", "gv_inst_disp": hex(operand),
                            "gv_inst_length": hex(len(expected))}})
    # The three observer values are a checked contiguous group, not a guessed
    # entity-state offset shared with the incompatible CSOL client layout.
    if raw(0x48B1D, 5) != bytes.fromhex("a3 14 0e 11 10") or \
            raw(0x48B28, 5) != bytes.fromhex("a3 18 0e 11 10"):
        raise ValueError("Matched particle observer group changed")
    # Original CoF aim-enabled gate suppresses VM bob. The matched native CS
    # instruction below loads only the VM scalar, after camera bob; retain all
    # native camera, FOV, shake, stairs and subsequent smoothing instructions.
    bob_load = 0x6B67C
    if raw(bob_load, 6) != bytes.fromhex("f3 0f 10 74 24 2c"):
        raise ValueError("CoF host viewmodel bob instruction changed")
    records.append({"platform": "windows", "module": "client",
                    "symbolName": "GoldCraftCof_ViewmodelBobLoad", "kind": "patch",
                    "payload": {"patch_rva": hex(bob_load)}})
    punch_stage = 0x6B80E
    if raw(punch_stage, 7) != bytes.fromhex("83 be c4 00 00 00 00"):
        raise ValueError("CoF host post-punch view stage changed")
    records.append({"platform": "windows", "module": "client",
                    "symbolName": "GoldCraftCof_AfterNativePunch", "kind": "patch",
                    "payload": {"patch_rva": hex(punch_stage)}})
    # Matched native mouse entry and its actual input references. Runtime
    # validates loaded operands after relocation; these are disk-image checks.
    mouse_sites = (
        ("Stage", 0x3605D, "a1 3c 0f 11 10"),
        ("Resume", 0x362A9, "8b 0d 30 77 12 10"),
        ("HistoryReference", 0x360EE, "89 35 54 0f 11 10 89 3d 58 0f 11 10"),
        ("DeltasReference", 0x360DE, "f3 0f 11 05 64 0f 11 10 f3 0f 11 15 68 0f 11 10"),
        ("AccumReference", 0x36065, "c7 05 5c 0f 11 10 00 00 00 00 c7 05 60 0f 11 10 00 00 00 00"),
        ("YawReference", 0x36280, "8b 15 64 76 12 10"),
    )
    for name, rva, encoded in mouse_sites:
        expected = bytes.fromhex(encoded)
        if raw(rva, len(expected)) != expected:
            raise ValueError("CoF native mouse reference changed: " + name)
        records.append({"platform": "windows", "module": "client",
                        "symbolName": "GoldCraftCof_Mouse" + name, "kind": "patch",
                        "payload": {"patch_rva": hex(rva)}})
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

    # Matched Setup/GetAnim object reads. Merge uses its explicit model argument.
    for name, member, offset in (("Header", "studioHeader", 0x44), ("SetupModel", "setupModel", 0x34)):
        records.append({"platform": "windows", "module": "client",
                        "symbolName": "CSOL_StudioRenderer_" + name, "kind": "structMember",
                        "payload": {"struct_name": "GameStudioRenderer", "member_name": member, "offset": hex(offset)}})

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
    # Integer vitals: real message readers, two HUD number calls and their
    # native int fields. Keep the second SpecHealth2 player-index read intact.
    vitals = json.loads((ROOT / "native/client/vitals_10210.json").read_text())
    if vitals["clientSha256"] != identity["sha256"]:
        raise ValueError("Integer vitals catalog identity mismatch")
    vitals_functions = {name: int(rva, 16) for name, rva, _ in vitals["functions"]}
    for rva, expected in vitals["verification"]:
        expected = bytes.fromhex(expected)
        if raw(int(rva, 16), len(expected)) != expected:
            raise ValueError(f"Integer vitals ABI verification failed at {rva}")
    for name, rva, size in vitals["functions"]:
        raw(int(rva, 16), int(size, 16))
        records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraftVitals_" + name,
                        "kind": "function", "payload": {"func_rva": rva, "func_size": size}})
    for name, rva, function in vitals["calls"]:
        call = int(rva, 16)
        instruction = raw(call, 5)
        if instruction[0] != 0xe8 or call + 5 + struct.unpack_from("<i", instruction, 1)[0] != vitals_functions[function]:
            raise ValueError(f"Integer vitals CALL changed: {name}")
        records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraftVitals_" + name,
                        "kind": "patch", "payload": {"patch_rva": rva}})
    for name, rva, reference in vitals["globals"]:
        address = IMAGE_BASE + int(rva, 16)
        if raw(int(reference, 16), 5) != b"\xa3" + struct.pack("<I", address):
            raise ValueError(f"Integer vitals global changed: {name}")
        records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraftVitals_" + name,
                        "kind": "global", "payload": {"gv_rva": rva, "gv_va": hex(address),
                            "gv_sig": "A3 ?? ?? ?? ??", "gv_sig_va": hex(IMAGE_BASE + int(reference, 16)),
                            "gv_inst_offset": "0x0", "gv_inst_disp": "0x1", "gv_inst_length": "0x5"}})
    motion = json.loads((ROOT / "native/client/motion_10210.json").read_text())
    if motion["clientSha256"] != identity["sha256"]:
        raise ValueError("Motion catalog identity mismatch")
    for rva, expected in motion["verification"]:
        expected = bytes.fromhex(expected)
        if raw(int(rva, 16), len(expected)) != expected:
            raise ValueError(f"Motion ABI verification failed at {rva}")
    for name, rva, size in motion["functions"]:
        raw(int(rva, 16), int(size, 16))
        records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraftMotion_" + name,
                        "kind": "function", "payload": {"func_rva": rva, "func_size": size}})
    menu = json.loads((ROOT / "native/client/spectator_menu_10210.json").read_text())
    if menu["sha256"] != identity["sha256"]:
        raise ValueError("Menu guard catalog identity mismatch")
    for entry in menu["functions"]:
        rva, size = int(entry["rva"], 16), int(entry["size"], 16)
        if hashlib.sha256(raw(rva, size)).hexdigest() != entry["sha256"]:
            raise ValueError("Menu guard function changed: " + entry["name"])
        records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraftMenu_" + entry["name"],
                        "kind": "function", "payload": {"func_rva": entry["rva"], "func_size": entry["size"]}})
    clock = json.loads((ROOT / "native/client/hud_clock_10210.json").read_text())
    if clock["sha256"] != identity["sha256"]:
        raise ValueError("HUD clock catalog identity mismatch")
    for entry in clock["verification"]:
        if hashlib.sha256(raw(int(entry["rva"], 16), int(entry["size"], 16))).hexdigest() != entry["sha256"]:
            raise ValueError("HUD clock native code changed")
    address = IMAGE_BASE + int(clock["stateRva"], 16)
    if raw(int(clock["accessorRva"], 16), 7) != b"\xd9\x05" + struct.pack("<I", address) + b"\xc3":
        raise ValueError("HUD clock accessor changed")
    records.append({"platform": "windows", "module": "client", "symbolName": "GoldCraftHudClock_State",
                    "kind": "global", "payload": {"gv_rva": clock["stateRva"], "gv_va": hex(address),
                        "gv_sig": "D9 05 ?? ?? ?? ?? C3", "gv_sig_va": hex(IMAGE_BASE + int(clock["accessorRva"], 16)),
                        "gv_inst_offset": "0x0", "gv_inst_disp": "0x2", "gv_inst_length": "0x6"}})
    shared_random = json.loads((ROOT / "native/client/shared_random_10210.json").read_text())
    if shared_random["clientSha256"] != identity["sha256"]:
        raise ValueError("Shared random catalog identity mismatch")
    for entry in shared_random["verification"]:
        if hashlib.sha256(raw(int(entry["rva"], 16), int(entry["size"], 16))).hexdigest() != entry["sha256"]:
            raise ValueError("Shared random native code or data changed: " + entry["name"])
    for entry in shared_random["functions"]:
        records.append({"platform": "windows", "module": "client", "symbolName": entry["name"],
                        "kind": "function", "payload": {"func_rva": entry["rva"], "func_size": entry["size"]}})
    # The extended predictor must consume the host client's CRT stream,
    # not a second /MT runtime in the plugin. Verify the exact PE import.
    import_rva, import_size = struct.unpack_from("<II", data, pe + 24 + 96 + 8)
    for entry in shared_random["imports"]:
        iat, name_rva = int(entry["iatRva"], 16), int(entry["nameRva"], 16)
        matched = False
        for offset in range(0, import_size - 19, 20):
            lookup, _, _, module, address = struct.unpack("<IIIII", raw(import_rva + offset, 20))
            if not module:
                break
            module_name = entry["module"].encode("ascii") + b"\0"
            if raw(module, len(module_name)).lower() != module_name.lower() or iat < address:
                continue
            displacement = iat - address
            if displacement % 4:
                continue
            imported = struct.unpack("<I", raw((lookup or address) + displacement, 4))[0]
            if imported != name_rva or struct.unpack("<I", raw(iat, 4))[0] != name_rva:
                continue
            expected = entry["importName"].encode("ascii") + b"\0"
            matched = raw(name_rva + 2, len(expected)) == expected
            if matched:
                break
        if not matched:
            raise ValueError("Client CRT import changed: " + entry["name"])
        # IDA: the matched host's direct rand call at 1003743A references
        # 100BF220. NormalizeGlobal requires the complete operand contract,
        # even when the resolver will use the exact module's RVA directly.
        reference_rva = 0x3743A
        if raw(reference_rva, 6) != b"\xff\x15" + struct.pack("<I", IMAGE_BASE + iat):
            raise ValueError("Client CRT import reference changed: " + entry["name"])
        records.append({"platform": "windows", "module": "client", "symbolName": entry["name"],
                        "kind": "global", "payload": {
                            "gv_rva": hex(iat), "gv_va": hex(IMAGE_BASE + iat),
                            "gv_sig": "FF 15 ?? ?? ?? ??", "gv_sig_va": hex(IMAGE_BASE + reference_rva),
                            "gv_inst_offset": "0x0", "gv_inst_disp": "0x2", "gv_inst_length": "0x6"}})
    # Native viewmodel mirror controls: their producers identify knife29 and
    # Elite state bit3; native StudioDrawModel mutates handedness only on RENDER.
    for name, rva, reference, expected in (
        ("KnifeMirrorFlag", 0x10F861, 0x66BF9, bytes.fromhex("80 3d 61 f8 10 10 01")),
        ("EliteLeftFlag", 0x10F860, 0x66BCF, bytes.fromhex("80 3d 60 f8 10 10 00"))):
        if raw(reference, len(expected)) != expected:
            raise ValueError("Native viewmodel mirror reference changed: " + name)
        records.append({"platform": "windows", "module": "client",
                        "symbolName": "GoldCraftViewmodel_" + name, "kind": "global",
                        "payload": {"gv_rva": hex(rva), "gv_va": hex(IMAGE_BASE + rva),
                            "gv_sig": "80 3D ?? ?? ?? ?? " + f"{expected[-1]:02X}",
                            "gv_sig_va": hex(IMAGE_BASE + reference), "gv_inst_offset": "0x0",
                            "gv_inst_disp": "0x2", "gv_inst_length": "0x7"}})
    # Native HUD owners are specific to this client, including its delete[]
    # thunk and VGUI_Surface026 instance. Never borrow the plugin CRT/surface.
    native_hud = json.loads((ROOT / "native/client/native_hud_client_10210.json").read_text())
    if (native_hud["sha256"] != identity["sha256"] or
            native_hud["crc64"].lower() != identity["crc64xz"].lower()):
        raise ValueError("Native HUD client catalog identity mismatch")
    for check in native_hud["verification"]:
        expected = bytes.fromhex(check["bytes"])
        if raw(int(check["rva"], 16), len(expected)) != expected:
            raise ValueError("Native HUD client instruction changed at " + check["rva"])
    records.extend(native_hud["records"])
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
                address_key = {"global": "gv_rva", "patch": "patch_rva", "structMember": "offset"}.get(verified["kind"], "func_rva")
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
        # Content-addressed local snapshots must remain stable across preflight
        # and deployment; the optional publication timestamp is not needed.
        "snapshotSchemaVersion": 8, "fileCount": len(records),
    }]}
    output.mkdir(parents=True, exist_ok=True)
    (output / (version + ".json")).write_bytes(snapshot_bytes)
    (output / "index.json").write_text(json.dumps(index, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"clientSha256": identity["sha256"], "clientCrc64": EXPECTED_CRC64,
                      "verifiedSymbols": len(records) + len(provided), "supplementalSymbols": len(records),
                      "providedByCommonPlugins": sorted(provided), "output": str(output)}))


if __name__ == "__main__":
    main()
