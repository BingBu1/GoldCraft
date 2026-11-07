"""Sample the verified sandbox client's native model cache without writing memory.

The bounded linked-list walk is observational, not an atomic engine snapshot.
Inconsistent reads are retained as rejected samples, never repaired or counted
as evictions. A disappeared allocation alone does not identify its release cause.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import time

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("runtime_audit", ROOT / "tools/Inspect-MetaHookRuntime.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)

# Exact HL10210 host PE; these are RVAs, never addresses in another process.
# Evidence/layout and observation limits: docs/2026-11-07_诊断-客户端模型缓存换图占用-report.md.
HOST_SHA = "9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295"
HEAD = 0xF6ADC0
HUNK_HIGH = 0xF6AE20
HEADER_BYTES = 0x58
MAX_NODES = 8192


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def snapshot(process, base):
    globals_before = process.read(base + HUNK_HIGH, 16)
    high, low, size, hunk = struct.unpack("<4I", globals_before)
    if not hunk or not 0 < size <= 0x7FFFFFFF or low + high > size or hunk + size > 0x100000000:
        raise ValueError("invalid or changing hunk bounds")
    head_address = base + HEAD
    head = process.read(head_address, HEADER_BYTES)
    cursor, previous = u32(head, 0x4C), head_address
    nodes, raw_nodes = {}, {}
    total = 0
    while cursor != head_address:
        if cursor in nodes or len(nodes) >= MAX_NODES:
            raise ValueError("cyclic or oversized cache chain")
        if not hunk + low <= cursor <= hunk + size - high - HEADER_BYTES:
            raise ValueError("cache node outside observed hunk bounds")
        data = process.read(cursor, HEADER_BYTES)
        length, owner = struct.unpack_from("<2I", data)
        if length < HEADER_BYTES or cursor + length > hunk + size - high or not owner:
            raise ValueError("invalid cache block size or owner")
        if u32(data, 0x48) != previous:
            raise ValueError("changing or inconsistent cache predecessor")
        if previous != head_address and previous + nodes[previous]["bytes"] > cursor:
            raise ValueError("overlapping or unordered cache blocks")
        if process.u32(owner) != cursor + HEADER_BYTES:
            raise ValueError("cache owner changed during observation")
        name = data[8:72].split(b"\0", 1)[0].decode("utf-8", errors="replace")
        nodes[cursor] = {"address": cursor, "bytes": length, "owner": owner, "name": name}
        raw_nodes[cursor] = data
        total += length
        previous, cursor = cursor, u32(data, 0x4C)
    if u32(head, 0x48) != previous:
        raise ValueError("changing cache tail")
    lru, previous, seen = u32(head, 0x54), head_address, set()
    while lru != head_address:
        if lru not in nodes or lru in seen:
            raise ValueError("LRU chain differs from allocation chain")
        data = raw_nodes[lru]
        if u32(data, 0x50) != previous:
            raise ValueError("inconsistent LRU predecessor")
        seen.add(lru)
        previous, lru = lru, u32(data, 0x54)
    if seen != set(nodes) or u32(head, 0x50) != previous:
        raise ValueError("inconsistent LRU tail or membership")
    # A double read rejects many races without pausing the game. It cannot
    # exclude ABA changes between reads and is not an engine-held lock.
    for address, original in raw_nodes.items():
        if process.read(address, HEADER_BYTES) != original:
            raise ValueError("cache allocation changed during observation")
        if process.u32(nodes[address]["owner"]) != address + HEADER_BYTES:
            raise ValueError("cache owner changed after chain walk")
    if process.read(head_address, HEADER_BYTES) != head or process.read(base + HUNK_HIGH, 16) != globals_before:
        raise ValueError("cache head or hunk changed during observation")
    capacity = size - low - high
    return {"hunkBytes": size, "lowBytes": low, "highBytes": high,
            "cacheCapacityBytes": capacity, "allocatedBytes": total,
            "unallocatedBytes": capacity - total, "nodes": list(nodes.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=45)
    parser.add_argument("--interval", type=float, default=.25)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not 0 < args.seconds <= 120 or not .1 <= args.interval <= 5:
        parser.error("use 0 < seconds <= 120 and .1 <= interval <= 5")
    if not output.is_relative_to(ROOT / "build") or output.exists():
        parser.error("choose an unused output under build")
    managed = json.loads((ROOT / "sandbox/cs-client-b/process-CsClient.json").read_text(encoding="utf-8-sig"))
    process = AUDIT.Process(managed["pid"], "cs-client-b")
    try:
        module = next(m for m in process.modules if m["path"].name.lower() == "hw.dll")
        expected = (ROOT / "sandbox/cs-client-b/Half-Life/hw.dll").resolve()
        if module["path"].resolve() != expected:
            raise ValueError("host module is not the selected sandbox copy")
        pe = AUDIT.PE(expected)
        if pe.sha256 != HOST_SHA or not pe.codeview:
            raise ValueError("unsupported host PE identity")
        base = module["base"]
        if process.read(base + pe.codeview["rva"], 24).hex() != pe.codeview["identity"]:
            raise ValueError("running host RSDS differs from the verified PE")
        started = time.monotonic()
        result = {"hostSha256": pe.sha256, "pid": managed["pid"], "base": base,
                  "startedMonotonicSeconds": started, "startedUnixNs": time.time_ns(),
                  "intervalSeconds": args.interval, "scope": __doc__, "samples": []}
        print(json.dumps({"sampling": True, "pid": managed["pid"], "seconds": args.seconds}), flush=True)
        while time.monotonic() - started < args.seconds:
            at = time.monotonic()
            record = {"seconds": at - started}
            try:
                record.update(snapshot(process, base))
            except (OSError, ValueError) as error:
                record["error"] = str(error)
            record["readSeconds"] = time.monotonic() - at
            result["samples"].append(record)
            time.sleep(max(0, args.interval - record["readSeconds"]))
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as file:
            json.dump(result, file, indent=2)
            file.write("\n")
        good = [r for r in result["samples"] if "error" not in r]
        print(json.dumps({"samples": len(result["samples"]), "consistent": len(good),
                          "maxReadSeconds": max(r["readSeconds"] for r in result["samples"]),
                          "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}), flush=True)
        if not good:
            raise RuntimeError("No consistent cache samples; see retained errors")
    finally:
        process.close()


if __name__ == "__main__":
    main()
