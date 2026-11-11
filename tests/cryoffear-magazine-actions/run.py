#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay matched original magazine actions with actual view-model timings.

Requires private, authorized CoF hl.dll and original models. No original bytes
or assets are embedded in this test. Effects are observed at their ABI edge;
the original reload helper and duration instructions execute unmodified.
"""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import struct
import subprocess

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_MXCSR

ROOT = Path(__file__).resolve().parents[2]
SHA = "0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63"
BASE, WEAPON, OWNER, WPEV, OPEV, GLOBAL, STACK = 0x10000000, 0x30000000, 0x30001000, 0x30005000, 0x30006000, 0x30007000, 0x40008000
VTABLE, OVTABLE, STUB, MODEL, STOP = 0x30008000, 0x30009000, 0x50000000, 0x60000000, 0x70000000
FUNCS = {
    (0, 0): 0x100475B0, (0, 1): 0x1005F340, (0, 3): 0x10084AD0,
    (1, 0): 0x100476B0, (1, 1): 0x1005F440, (1, 3): 0x10084C20,
    (2, 0): 0x10047CF0, (2, 1): 0x1005FA40, (2, 3): 0x100857C0}
ORIGINAL_ID = {0: 3, 1: 7, 3: 19}
NAMES = {0: "glock", 1: "p345", 3: "vp70"}


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def mapped_image(binary):
    pe = u32(binary, 0x3C)
    if binary[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", binary, pe+4)[0] != 0x14C:
        raise ValueError("Expected original PE32 x86")
    optional = pe + 24
    if u32(binary, optional+28) != BASE:
        raise ValueError("Unexpected image base")
    image = bytearray(u32(binary, optional+56))
    table = optional + struct.unpack_from("<H", binary, pe+20)[0]
    for i in range(struct.unpack_from("<H", binary, pe+6)[0]):
        at = table + i*40
        rva, size, raw = struct.unpack_from("<III", binary, at+12)
        image[rva:rva+size] = binary[raw:raw+size]
    return image


def model_times(data):
    if data[:4] != b"IDST" or u32(data, 4) != 10 or u32(data, 72) != len(data):
        raise ValueError("Expected complete original studio v10")
    count, start = u32(data, 164), u32(data, 168)
    if not 0 < count <= 256 or start + count*176 > len(data):
        raise ValueError("Invalid sequence descriptors")
    result = []
    for i in range(38):
        if i >= count:
            result.append(0)
            continue
        frames = u32(data, start+i*176+56)
        fps = struct.unpack_from("<f", data, start+i*176+32)[0]
        result.append(bits(0 if frames <= 1 else (frames-1)/fps))
    return result


class Original:
    def __init__(self, image, models):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc.mem_map(BASE, (len(image)+4095) & ~4095)
        self.uc.mem_write(BASE, bytes(image))
        for address, size in ((WEAPON, 0x10000), (0x40000000, 0x10000),
                              (STUB, 0x10000), (MODEL, 0x800000), (STOP, 0x1000)):
            self.uc.mem_map(address, size)
        self.models = models
        self.write(WEAPON, VTABLE); self.write(OWNER, OVTABLE)
        self.write(WEAPON+4, WPEV); self.write(OWNER+4, OPEV)
        self.write(WEAPON+0x160, OWNER); self.write(WEAPON+0x1DC, 1)
        self.write(WPEV+0x20C, 10); self.write(OPEV+0x20C, 11)
        self.write(VTABLE+0x198, STUB); self.write(VTABLE+0x1D0, 0x1001F050)
        self.write(OVTABLE+8, STUB+0x10)
        self.write(0x10223188, GLOBAL); self.write(0x10223070, STUB+0x20)
        self.uc.hook_add(UC_HOOK_CODE, self.hook)
        self.visited = set()

    def write(self, address, value):
        self.uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def read(self, address):
        return u32(self.uc.mem_read(address, 4), 0)

    def byte(self, address, value=None):
        if value is None:
            return self.uc.mem_read(address, 1)[0]
        self.uc.mem_write(address, bytes([value]))

    def snapshot(self):
        return [self.read(WEAPON+at) for at in (0x1E4,)] + [self.read(OWNER+0x2544)] + \
            [self.read(WEAPON+at) for at in (0x128, 0x140, 0x138, 0x1F8)] + \
            [self.byte(WEAPON+at) for at in (0x178, 0x1A4)] + [self.read(WEAPON+0x1F0)] + \
            [self.read(WEAPON+at) for at in (0x1CC, 0x1D0, 0x1D4, 0x1D8)] + [self.read(OWNER+0x308)]

    def event(self, kind, argument=0, delay=0):
        self.events.append([kind, argument, delay, *self.snapshot()])

    def ret(self, arguments=0):
        sp = self.uc.reg_read(UC_X86_REG_ESP)
        self.uc.reg_write(UC_X86_REG_ESP, sp+4+arguments)
        self.uc.reg_write(UC_X86_REG_EIP, self.read(sp))

    def hook(self, uc, address, size, user):
        self.visited.add(address)
        sp = uc.reg_read(UC_X86_REG_ESP)
        if address == STUB:
            sequence = self.read(sp+4)
            self.event(0, sequence)
            self.write(OPEV+0x21C, sequence)
            self.ret(12)
        elif address == STUB+0x10:
            self.event(3, ORIGINAL_ID[self.pistol], self.read(sp+4))
            self.ret(4)
        elif address == STUB+0x20:
            uc.reg_write(UC_X86_REG_EAX, MODEL)
            self.ret()
        elif address == 0x100EA240:
            pointer = self.read(sp+4)
            text = bytes(uc.mem_read(pointer, 64)).split(b"\0", 1)[0]
            if b"melee" in text:
                self.event(4, ORIGINAL_ID[self.pistol])
            elif b"reload" in text:
                self.event(2 if b"empty" in text else 1, ORIGINAL_ID[self.pistol])
            else:
                raise ValueError(f"Unexpected player action {text}")
            self.ret(8)
        elif address == 0x100DFEA0:
            # This helper only returns gpGlobals.forward in a hidden Vector.
            # Its ignored result has no animation/timer effects in idle.
            self.ret(8)
        elif not BASE <= address < BASE+0x250000:
            raise ValueError(f"Unexpected external instruction {address:08X}")

    def run(self, row):
        op, self.pistol = row[:2]
        self.events = []
        s = row[2:16]
        for offset, value in zip((0x1E4, 0x128, 0x140, 0x138, 0x1F8), (s[0], *s[2:6])):
            self.write(WEAPON+offset, value)
        self.write(OWNER+0x2544, s[1])
        for offset, value in zip((0x178, 0x1A4), s[6:8]):
            self.byte(WEAPON+offset, value)
        self.write(WEAPON+0x1F0, s[8])
        for offset, value in zip((0x1CC, 0x1D0, 0x1D4, 0x1D8), s[9:13]):
            self.write(WEAPON+offset, value)
        self.write(OWNER+0x308, s[13]); self.write(GLOBAL, row[16])
        self.write(WEAPON+0x168, ORIGINAL_ID[self.pistol])
        self.uc.mem_write(MODEL, self.models[self.pistol])
        self.write(STACK, STOP)
        self.uc.reg_write(UC_X86_REG_ESP, STACK)
        self.uc.reg_write(UC_X86_REG_ECX, WEAPON)
        self.uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.uc.emu_start(FUNCS[(op, self.pistol)], STOP, count=2000)
        if self.uc.reg_read(UC_X86_REG_EIP) != STOP:
            raise ValueError("Original function failed to return")
        return [*self.snapshot(), len(self.events), *itertools.chain.from_iterable(self.events)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sample", required=True, type=Path)
    parser.add_argument("--models", required=True, type=Path)
    parser.add_argument("--ida-bytes", required=True, type=Path)
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT/"build"):
        parser.error("Use a new dedicated build directory")
    out.mkdir(parents=True)
    source = ("native/guest/cof/magazine_actions.hpp", "native/guest/cof/magazine_actions.cpp",
              "native/guest/cof/scenario_damage.hpp", "tests/cryoffear-magazine-actions/main.cpp",
              "tests/cryoffear-magazine-actions/run.py")
    def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
    before = {p: sha(ROOT/p) for p in source}
    original = args.sample.read_bytes()
    if sha(args.sample) != SHA:
        raise ValueError("Original sample identity mismatch")
    image = mapped_image(original)
    proof = json.loads(args.ida_bytes.read_text("utf-8"))
    if proof["sampleSha256"] != SHA:
        raise ValueError("IDA sample identity mismatch")
    required = {*FUNCS.values(), 0x1011C560, 0x10120210, 0x1000F390, 0x1001F050, 0x1011AB20, 0x100DFEA0}
    for address in required:
        item = next(item for item in proof["regions"] if int(item["va"], 16) == address)
        data = bytes.fromhex(item["hex"])
        if not data or hashlib.sha256(data).hexdigest() != item["sha256"] or image[address-BASE:address-BASE+len(data)] != data:
            raise ValueError(f"Original/IDA bytes mismatch {address:X}")
    models = {key: (args.models/name/("v_"+name+".mdl")).read_bytes() for key, name in NAMES.items()}
    times = {key: model_times(data) for key, data in models.items()}
    oracle = Original(image, models)
    env = os.environ.copy()
    env.update({k: v for k, v in json.loads(args.environment.read_text("utf-8-sig")).items()
                if k in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    compiler = Path(json.loads((ROOT/".tools/clang-toolchain.json").read_text("utf-8-sig"))["root"])/"bin/clang-cl.exe"
    flags = ["/nologo", "--target=i686-pc-windows-msvc", "/std:c++20", "/EHsc", "/MT", "/utf-8", "/W4", "/WX",
             "/O2", "/clang:-O3", "/clang:-flto=thin", "/clang:-fuse-ld=lld", "/arch:AVX2", "/fp:precise",
             "/clang:-ffp-contract=off", "-Wno-overriding-option"]
    exe = out/"actions.exe"
    command = [str(compiler), *flags, "/I"+str(ROOT), str(ROOT/source[1]), str(ROOT/source[3]), "/Fe:"+str(exe), "/link", "/threads:1"]
    build = subprocess.run(command, cwd=out, env=env, capture_output=True)
    (out/"compile.log").write_bytes(build.stdout+build.stderr)
    build.check_returncode()
    safety = subprocess.run([str(exe), "--safety"], cwd=out, capture_output=True)
    (out/"safety.txt").write_bytes(safety.stdout+safety.stderr)
    safety.check_returncode()
    cases, expected = [], []
    for operation, pistol in FUNCS:
        cap = {0: 15, 1: 8, 3: 18}[pistol]
        for clip, reserve, action, special, aimed, pending, locked in itertools.product(
                (0, 1, cap-1, cap), (0, 1, cap+1), (0, 1, 2, 5), (0, 1), (0, 1), (0, 1), (0, 1, 2, 3, 4)):
            now = bits(10.25)
            clocks = [now-1]*5
            if locked: clocks[locked-1] = now+1
            row = [operation, pistol, clip, reserve, action, special, 3, 2,
                   aimed, pending, 0, *clocks, now]
            cases.append(row); expected.append(oracle.run(row))
        # Exact equality and large-time rounding are distinct from early/late.
        for when in (.10506151616573334, 0, 10.25, 8192, 1000000, 16777216):
            now = bits(when)
            row = [operation, pistol, 1, cap, 0, 0, 3, 2, 0, 0, 0, *([now]*5), now]
            cases.append(row); expected.append(oracle.run(row))
    inputs = "".join(" ".join(map(str, times[key]))+"\n" for key in NAMES)
    inputs += "".join(" ".join(map(str, row))+"\n" for row in cases)
    run = subprocess.run([str(exe)], cwd=out, input=inputs, capture_output=True, text=True, timeout=40)
    actual = [list(map(int, line.split())) for line in run.stdout.splitlines()]
    failures = [dict(case=case, expected=wanted, actual=got) for case, wanted, got in zip(cases, expected, actual) if wanted != got]
    after = {p: sha(ROOT/p) for p in source}
    success = run.returncode == 0 and len(actual) == len(expected) and not failures and before == after
    receipt = dict(revision=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT).decode().strip(),
        sampleSha256=SHA, modelSha256={NAMES[k]: hashlib.sha256(v).hexdigest() for k,v in models.items()},
        originalRegions=len(required), originalCases=len(cases), rows=len(actual), failures=failures[:12],
        visitedInstructions=len(oracle.visited), compilerSha256=sha(compiler), command=command,
        safety=safety.stdout.decode().strip(), runExit=run.returncode, stderr=run.stderr,
        before=before, after=after, success=success, sourceCopies=False,
        boundary="Actual original reload/secondary/idle with intercepted effects; no host DLL, gameplay or performance acceptance")
    (out/"receipt.json").write_text(json.dumps(receipt, indent=2), "utf-8")
    print(json.dumps({k:receipt[k] for k in ("originalCases", "rows", "safety", "failures", "success")}))
    raise SystemExit(0 if success else 1)


if __name__ == "__main__":
    main()
