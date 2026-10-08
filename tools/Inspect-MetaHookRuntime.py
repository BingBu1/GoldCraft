"""Read-only audit of the installed MetaHook hook registry in a sandbox client.

Offsets and layouts come from the exact matching PDB, never a saved address.
Checks installation/chain integrity, not execution coverage of every callback.
No debugger attachment, memory writes, or simulated game/desktop input.
"""
import argparse
from collections import Counter, defaultdict
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent


class PE:
    def __init__(self, path):
        self.path = Path(path).resolve(strict=True)
        self.data = self.path.read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        pe = self.u32(0x3C)
        if self.data[:2] != b"MZ" or self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError(f"Invalid PE: {path}")
        self.timestamp = self.u32(pe + 8)
        count = self.u16(pe + 6)
        optional_size = self.u16(pe + 20)
        optional = pe + 24
        if self.u16(pe + 4) != 0x14C or self.u16(optional) != 0x10B:
            raise ValueError("Expected x86 PE32")
        self.image_size = self.u32(optional + 56)
        self.sections = []
        for i in range(count):
            p = optional + optional_size + i * 40
            self.sections.append({"name": self.data[p:p + 8].split(b"\0")[0].decode(),
                                  "size": max(self.u32(p + 8), self.u32(p + 16)),
                                  "rva": self.u32(p + 12), "raw": self.u32(p + 20),
                                  "flags": self.u32(p + 36)})
        self.codeview = None
        debug_rva, debug_size = struct.unpack_from("<II", self.data, optional + 96 + 6 * 8)
        if debug_rva:
            debug = self.offset(debug_rva)
            for p in range(debug, debug + debug_size, 28):
                if self.u32(p + 12) != 2:
                    continue
                raw, rva = self.u32(p + 24), self.u32(p + 20)
                if self.data[raw:raw + 4] == b"RSDS":
                    self.codeview = {"guid": str(uuid.UUID(bytes_le=self.data[raw + 4:raw + 20])),
                                     "age": self.u32(raw + 20), "rva": rva,
                                     "identity": self.data[raw:raw + 24].hex()}

    def u16(self, offset):
        return struct.unpack_from("<H", self.data, offset)[0]

    def u32(self, offset):
        return struct.unpack_from("<I", self.data, offset)[0]

    def offset(self, rva):
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + s["size"]:
                return s["raw"] + rva - s["rva"]
        raise ValueError(f"RVA outside PE sections: {rva:x}")


class Symbols:
    def __init__(self, pdb, pe):
        configured = ROOT / ".tools/clang-toolchain.json"
        compiler = os.environ.get("GOLDCRAFT_LLVM_ROOT")
        if not compiler and configured.is_file():
            compiler = json.loads(configured.read_text(encoding="utf-8-sig"))["root"]
        if not compiler:
            raise ValueError("Configure GOLDCRAFT_LLVM_ROOT or .tools/clang-toolchain.json")
        self.tool = str(Path(compiler) / "bin/llvm-pdbutil.exe")
        self.pdb = Path(pdb).resolve(strict=True)
        summary = self.dump("-summary")
        guid = re.search(r"GUID: \{([^}]+)\}", summary).group(1).lower()
        age = int(re.search(r"Age: (\d+)", summary).group(1))
        if not pe.codeview or (guid, age) != (pe.codeview["guid"], pe.codeview["age"]):
            raise ValueError("PDB GUID/Age does not match the actual MetaHook executable")
        self.identity = {"guid": guid, "age": age}
        self.globals = self.dump("-globals")
        self.sections = pe.sections

    def dump(self, *options):
        return subprocess.check_output([self.tool, "dump", *options, str(self.pdb)],
                                       encoding="utf-8", errors="strict")

    def global_rva(self, name):
        match = re.search(r"S_GDATA32[^\n]*`" + re.escape(name)
                          + r"`\s*\n[^\n]*addr = (\d+):(\d+)", self.globals)
        if not match:
            raise ValueError(f"Global missing from matching PDB: {name}")
        return self.sections[int(match[1]) - 1]["rva"] + int(match[2])

    def layout(self, name):
        match = re.search(r"S_UDT[^\n]*`" + re.escape(name)
                          + r"`\s*\n\s*original type = (0x[\dA-Fa-f]+)", self.globals)
        if not match:
            raise ValueError(f"Type missing from matching PDB: {name}")
        definition = self.dump("-types", "-type-index=" + match[1])
        size = int(re.search(r"sizeof (\d+)", definition)[1])
        fields = re.search(r"field list: (0x[\dA-Fa-f]+)", definition)[1]
        members = self.dump("-types", "-type-index=" + fields)
        offsets = {m[1]: int(m[2]) for m in re.finditer(
            r"LF_MEMBER \[name = `([^`]+)`[^\n]*offset = (\d+)", members)}
        return {"size": size, "members": offsets}


class Process:
    def __init__(self, pid, instance):
        self.kernel = C.WinDLL("kernel32", use_last_error=True)
        psapi = C.WinDLL("psapi", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.kernel.OpenProcess.restype = W.HANDLE
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.kernel.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        self.kernel.ReadProcessMemory.restype = W.BOOL
        self.kernel.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
        psapi.EnumProcessModulesEx.argtypes = [W.HANDLE, C.POINTER(W.HMODULE), W.DWORD, C.POINTER(W.DWORD), W.DWORD]
        psapi.GetModuleFileNameExW.argtypes = [W.HANDLE, W.HMODULE, W.LPWSTR, W.DWORD]

        class ModuleInfo(C.Structure):
            _fields_ = [("base", C.c_void_p), ("size", W.DWORD), ("entry", C.c_void_p)]

        psapi.GetModuleInformation.argtypes = [W.HANDLE, W.HMODULE, C.POINTER(ModuleInfo), W.DWORD]
        self.handle = self.kernel.OpenProcess(0x410, False, pid)
        if not self.handle:
            raise C.WinError(C.get_last_error())
        try:
            path = C.create_unicode_buffer(32768)
            length = W.DWORD(len(path))
            if not self.kernel.QueryFullProcessImageNameW(self.handle, 0, path, C.byref(length)):
                raise C.WinError(C.get_last_error())
            self.expected = (ROOT / "sandbox" / instance / "Half-Life/MetaHook.exe").resolve(strict=True)
            if Path(path.value).resolve() != self.expected:
                raise ValueError("PID does not belong to the selected sandbox client")
            array = (W.HMODULE * 1024)()
            needed = W.DWORD()
            if not psapi.EnumProcessModulesEx(self.handle, array, C.sizeof(array), C.byref(needed), 3):
                raise C.WinError(C.get_last_error())
            if needed.value > C.sizeof(array):
                raise ValueError("Module enumeration exceeds audit bound")
            self.modules = []
            for module in array[:needed.value // C.sizeof(W.HMODULE)]:
                name = C.create_unicode_buffer(32768)
                info = ModuleInfo()
                if not psapi.GetModuleFileNameExW(self.handle, module, name, len(name)):
                    raise C.WinError(C.get_last_error())
                if not psapi.GetModuleInformation(self.handle, module, C.byref(info), C.sizeof(info)):
                    raise C.WinError(C.get_last_error())
                self.modules.append({"path": Path(name.value), "base": info.base, "size": info.size})
        except BaseException:
            self.close()
            raise

    def close(self):
        self.kernel.CloseHandle(self.handle)

    def read(self, address, count):
        buffer = C.create_string_buffer(count)
        received = C.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buffer, count, C.byref(received)) or received.value != count:
            raise C.WinError(C.get_last_error())
        return buffer.raw

    def u32(self, address):
        return struct.unpack("<I", self.read(address, 4))[0]

    def location(self, address):
        module = next((m for m in self.modules if m["base"] <= address < m["base"] + m["size"]), None)
        return {"address": hex(address), "module": module["path"].name if module else None,
                "rva": hex(address - module["base"]) if module else None}

    def jump(self, address):
        data = self.read(address, 8)
        if data[0] == 0xE9:
            return (address + 5 + struct.unpack_from("<i", data, 1)[0]) & 0xFFFFFFFF
        if data[0] == 0xEB:
            return (address + 2 + struct.unpack_from("<b", data, 1)[0]) & 0xFFFFFFFF
        if data[:2] == b"\xff\x25":
            return self.u32(struct.unpack_from("<I", data, 2)[0])
        return None


def inspect(pid, instance, pdb):
    process = Process(pid, instance)
    try:
        pe = PE(process.expected)
        symbols = Symbols(pdb, pe)
        host = next(m for m in process.modules if m["path"].resolve() == process.expected)
        if process.read(host["base"] + pe.codeview["rva"], 24).hex() != pe.codeview["identity"]:
            raise ValueError("Running MetaHook identity differs from its file/PDB")
        layouts = {name: symbols.layout(name) for name in
                   ("hook_s", "tagVTABLEDATA", "tagIATDATA", "tagINLINEDATA", "tagINLINEPATCHDATA")}
        registry = host["base"] + symbols.global_rva("g_pHookBase")
        cursor = process.u32(registry)
        visited, records = set(), []
        members = layouts["hook_s"]["members"]
        while cursor:
            if cursor in visited or len(visited) >= 8192:
                raise ValueError("Cyclic/oversized hook registry")
            visited.add(cursor)
            data = process.read(cursor, layouts["hook_s"]["size"])
            get = lambda name: struct.unpack_from("<I", data, members[name])[0]
            kind, old, new, original = (get(n) for n in ("iType", "pOldFuncAddr", "pNewFuncAddr", "pOrginalCall"))
            item = {"node": cursor, "type": kind, "committed": bool(data[members["bCommitted"]]),
                    "old": old, "new": new, "originalPointer": original,
                    "original": process.u32(original) if original else None}
            union = members["HookData"]
            get_union = lambda layout, field: struct.unpack_from("<I", data, union + layouts[layout]["members"][field])[0]
            if kind == 1:
                item["trampoline"] = get_union("tagINLINEDATA", "pTrampolineCall")
                item["entry"] = old
            elif kind in (2, 3):
                layout, field = ("tagVTABLEDATA", "pVirtualFuncAddr") if kind == 2 else ("tagIATDATA", "pImportFuncAddr")
                item["slot"] = get_union(layout, field)
                item["entry"] = process.u32(item["slot"])
            elif kind == 4:
                item["instruction"] = get_union("tagINLINEPATCHDATA", "pInstructionAddress")
                size = get_union("tagINLINEPATCHDATA", "PatchLength")
                if not 1 <= size <= 15:
                    raise ValueError("Invalid registered inline-patch length")
                offset = union + layouts["tagINLINEPATCHDATA"]["members"]["NewCodeBytes"]
                item["expectedBytes"] = data[offset:offset + size].hex()
                item["actualBytes"] = process.read(item["instruction"], size).hex()
            else:
                raise ValueError(f"Unknown hook type: {kind}")
            records.append(item)
            cursor = get("pNext")
        # Register the original-call edges of all wrappers. Multiple plugins may
        # legitimately share one slot or entry, including transactional order.
        originals = defaultdict(set)
        for item in records:
            if item["original"]:
                originals[item["new"]].add(item["original"])

        def reachable(entry, expected):
            pending, seen = [entry], set()
            while pending and len(seen) < 128:
                current = pending.pop()
                if current == expected:
                    return True
                if not current or current in seen:
                    continue
                seen.add(current)
                pending.extend(originals.get(current, ()))
                try:
                    target = process.jump(current)
                except OSError:
                    target = None
                if target:
                    pending.append(target)
            return False

        findings, counts, output = [], Counter(), []
        for item in records:
            owner = process.location(item["new"])["module"] or "anonymous"
            counts[owner] += 1
            reasons = []
            if not item["committed"]:
                reasons.append("uncommitted")
            if item["type"] == 4:
                if item["actualBytes"] != item["expectedBytes"]:
                    reasons.append("patch-bytes-differ")
            elif not reachable(item["entry"], item["new"]):
                reasons.append("replacement-not-reachable-in-registered-chain")
            if item["type"] == 1 and item["originalPointer"] and item["original"] != item["trampoline"]:
                reasons.append("original-call-pointer-differs-from-trampoline")
            row = {key: process.location(value) if key in
                   ("node", "old", "new", "originalPointer", "original", "trampoline", "slot", "entry", "instruction")
                   and value is not None else value for key, value in item.items()}
            row["findings"] = reasons
            output.append(row)
            if reasons:
                findings.append(row)
        plugin_list = process.expected.parent / "cstrike/metahook/configs/plugins.lst"
        configured = [line.strip() for line in plugin_list.read_text(encoding="utf-8-sig").splitlines()
                      if line.strip() and not line.lstrip().startswith(("//", "#", ";"))]
        plugins = []
        for name in configured:
            stem = Path(name).stem.removesuffix("_AVX2").lower()
            matches = [m for m in process.modules if m["path"].stem.removesuffix("_AVX2").lower() == stem]
            plugins.append({"configured": name, "loaded": [
                {"file": m["path"].name, "sha256": hashlib.sha256(m["path"].read_bytes()).hexdigest(),
                 "base": hex(m["base"]), "registeredHooks": counts[m["path"].name]} for m in matches]})
        return {"pid": pid, "instance": instance, "metaHookSha256": pe.sha256,
                "pdb": symbols.identity, "layouts": layouts,
                "plugins": plugins, "registeredHooks": len(records), "hooksByModule": dict(counts),
                "findings": findings, "hooks": output,
                "allConfiguredPluginsLoaded": all(p["loaded"] for p in plugins),
                "allRegisteredChainsIntact": not findings,
                "scope": "Installation/registered continuation-chain integrity only; public callback rewrites, raw patches and execution coverage require separate evidence."}
    finally:
        process.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--instance", choices=("cs-client-a", "cs-client-b"), default="cs-client-b")
    parser.add_argument("--pdb", type=Path, default=ROOT / "dist/metahook/MetaHook.pdb")
    args = parser.parse_args()
    report = inspect(args.pid, args.instance, args.pdb)
    destination = ROOT / "analysis/native-regressions" / f"hook-audit-{time.time_ns()}.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({key: report[key] for key in ("registeredHooks", "hooksByModule", "plugins", "allConfiguredPluginsLoaded", "allRegisteredChainsIntact")} | {
        "findings": len(report["findings"]), "report": str(destination)}, indent=2))
    return 0 if report["allConfiguredPluginsLoaded"] and report["allRegisteredChainsIntact"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
