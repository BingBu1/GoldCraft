#!/usr/bin/env python3
"""Execute SHA-pinned original x86 basis/clip/intersection/mip instructions.

Private original bytes are supplied locally, never copied into this source.
Compare exact float bits and polygon vertex order, not just area/extents.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import unicorn
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_MXCSR

SHA = "d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00"
DATA, STACK, STOP = 0x20000000, 0x30000000, 0x40000000


def u32(v):
    return struct.pack("<I", v)


def vec(v):
    return struct.pack("<3f", *v)


class Original:
    def __init__(self, path):
        raw = path.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == SHA
        pe = struct.unpack_from("<I", raw, 60)[0]
        assert struct.unpack_from("<H", raw, pe + 4)[0] == 0x14C
        count = struct.unpack_from("<H", raw, pe + 6)[0]
        opt = pe + 24
        assert struct.unpack_from("<I", raw, opt + 28)[0] == 0x10000000
        size = struct.unpack_from("<I", raw, opt + 56)[0]
        uc = self.uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        uc.mem_map(0x10000000, (size + 4095) & ~4095)
        sections = opt + struct.unpack_from("<H", raw, pe + 20)[0]
        for i in range(count):
            rva, length, offset = struct.unpack_from("<III", raw, sections + i * 40 + 12)
            uc.mem_write(0x10000000 + rva, raw[offset:offset + length])
        uc.mem_map(DATA, 0x10000)
        uc.mem_map(STACK, 0x10000)
        uc.mem_map(STOP, 0x1000)
        uc.mem_write(0x100B42C0, b"\xc3")  # cookie check only
        uc.reg_write(UC_X86_REG_FPCW, 0x37F)
        uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        self.draw_output = []
        self.draw_uv = b""
        uc.mem_write(0x10060E10, b"\xc3")  # texture bind wrapper, cdecl
        for pointer, stub, pop in ((0x101BE648, STOP + 0x200, 4),
                                   (0x101BE6A0, STOP + 0x220, 0),
                                   (0x101BE770, STOP + 0x240, 12),
                                   (0x101BE74C, STOP + 0x260, 4)):
            uc.mem_write(pointer, u32(stub))
            uc.mem_write(stub, b"\xc2" + struct.pack("<H", pop))

        def gl_hook(uc, address, size, user):
            sp = uc.reg_read(UC_X86_REG_ESP)
            if address == STOP + 0x240:
                assert struct.unpack("<I", uc.mem_read(sp + 4, 4))[0] == 0x84C0
                self.draw_uv = bytes(uc.mem_read(sp + 8, 8))
            elif address == STOP + 0x260:
                point = struct.unpack("<I", uc.mem_read(sp + 4, 4))[0]
                self.draw_output.append(bytes(uc.mem_read(point, 12)) + self.draw_uv)

        uc.hook_add(unicorn.UC_HOOK_CODE, gl_hook, begin=STOP + 0x240, end=STOP + 0x260)

    def call(self, address, args):
        self.uc.mem_write(STACK + 0x8000, u32(STOP) + args)
        self.uc.reg_write(UC_X86_REG_ESP, STACK + 0x8000)
        self.uc.emu_start(address, STOP, count=300000)
        return self.uc.reg_read(UC_X86_REG_EAX)

    def basis(self, n):
        self.call(0x10058E40, vec(n) + u32(DATA) + u32(DATA + 12))
        return bytes(self.uc.mem_read(DATA, 24))

    def clip(self, points, n, p):
        self.uc.mem_write(DATA, b"".join(vec(v) for v in points))
        count = self.call(0x100579C0, u32(DATA) + u32(len(points)) + vec(n) + vec(p) + u32(DATA + 0x1000))
        assert count <= 68
        return u32(count) + bytes(self.uc.mem_read(DATA + 0x1000, count * 12))

    def intersection(self, a, b, n, p):
        self.uc.mem_write(DATA, vec(a) + vec(b) + vec(n) + vec(p))
        self.call(0x10058D20, b"".join(u32(DATA + x) for x in (0, 12, 24, 36, 48)))
        return bytes(self.uc.mem_read(DATA + 48, 12))

    def texture(self, tga):
        # Original 100697D9 byte swap; original top-origin assets do not flip.
        rgb = bytearray(tga[18:])
        for i in range(0, len(rgb), 3):
            rgb[i], rgb[i + 2] = rgb[i + 2], rgb[i]
        if not tga[17] & 32:
            rgb = bytearray(b"".join(rgb[y * 192:(y + 1) * 192] for y in reversed(range(64))))
        out = bytes(rgb)
        self.uc.mem_write(DATA, bytes(rgb))
        width = 64
        while width > 1:
            self.call(0x10069F50, u32(DATA) + u32(width) + u32(width))
            width //= 2
            out += bytes(self.uc.mem_read(DATA, width * width * 3))
        return out

    def draw(self, origin, normal):
        self.draw_output = []
        surface, polygon, variant, mark = (DATA + x for x in (0, 0x100, 0x300, 0x400))
        self.uc.mem_write(surface, u32(77) + bytes(32) + u32(polygon))
        points = [(-2048, -2048, 0), (2048, -2048, 0), (2048, 2048, 0), (-2048, 2048, 0)]
        self.uc.mem_write(polygon, bytes(8) + u32(4) + bytes(4) + b"".join(vec(v) + bytes(16) for v in points))
        self.uc.mem_write(variant, u32(1) + u32(3) + u32(3) + struct.pack("<f", 1.0))
        self.uc.mem_write(mark, u32(surface) + vec(origin) + vec(normal) + u32(variant))
        self.uc.mem_write(0x104CDD44, u32(77))
        self.call(0x100587D0, u32(mark))
        return u32(len(self.draw_output)) + b"".join(self.draw_output)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sample", type=Path, required=True)
    ap.add_argument("--runner", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    original = Original(args.sample)
    rng = random.Random(0xC0FDEC)
    rows, expected, kinds = [], [], {}

    def add(kind, data, result):
        rows.append(u32(kind) + data)
        expected.append(result)
        kinds[kind] = kinds.get(kind, 0) + 1

    normals = [(0, 0, 0), (0, 0, 1), (0, 0, -1), (1, 0, 0), (0, 1, 0), (-0.0, 0, 1)]
    normals += [tuple(rng.randrange(-8, 9) / 8 for _ in range(3)) for _ in range(700)]
    for n in normals:
        add(0, vec(n), original.basis(n))
    for _ in range(700):
        a, b, n, p = [tuple(rng.uniform(-100, 100) for _ in range(3)) for _ in range(4)]
        add(2, vec(a) + vec(b) + vec(n) + vec(p), original.intersection(a, b, n, p))
    for a, b, n, p in [((0, 0, 0), (0, 0, 1), (1, 0, 0), (3, 0, 0)),
                        ((-1, 0, 0), (1, 0, 0), (1, 0, 0), (0, 0, 0))]:
        add(2, vec(a) + vec(b) + vec(n) + vec(p), original.intersection(a, b, n, p))
    square = [(-4, -4, 0), (4, -4, 0), (4, 4, 0), (-4, 4, 0)]
    cases = [(square, n, p) for n in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, 0, 1))
             for p in ((0, 0, 0), (4, 4, 0), (-4, -4, 0), (9, 9, 0))]
    for _ in range(800):
        count = rng.randrange(3, 17)
        points = [(math.cos(i * 2 * math.pi / count) * 8, math.sin(i * 2 * math.pi / count) * 8, 0)
                  for i in range(count)]
        n = (rng.uniform(-1, 1), rng.uniform(-1, 1), 0)
        p = (rng.uniform(-10, 10), rng.uniform(-10, 10), 0)
        cases.append((points, n, p))
    for points, n, p in cases:
        add(1, u32(len(points)) + vec(n) + vec(p) + b"".join(vec(v) for v in points), original.clip(points, n, p))
    for top in (0, 32):
        tga = bytearray(18 + 64 * 64 * 3)
        tga[2], tga[12], tga[14], tga[16], tga[17] = 2, 64, 64, 24, top
        tga[18:] = rng.randbytes(64 * 64 * 3)
        add(3, bytes(tga), original.texture(tga))
    for _ in range(250):
        origin = (rng.randrange(-10000, 10000) / 8, rng.randrange(-10000, 10000) / 8, rng.randrange(-16, 16) / 8)
        normal = (rng.randrange(-3, 4) / 8, rng.randrange(-3, 4) / 8, rng.randrange(6, 9) / 8)
        add(4, vec(origin) + vec(normal), original.draw(origin, normal))
    result = subprocess.run([str(args.runner), "--binary"], input=b"".join(rows), capture_output=True, check=True)
    offset = 0
    for index, wanted in enumerate(expected):
        actual = result.stdout[offset:offset + len(wanted)]
        if actual != wanted:
            raise AssertionError(f"original differential mismatch case {index} kind {struct.unpack_from('<I', rows[index])[0]} expected={wanted.hex()} actual={actual.hex()}")
        offset += len(wanted)
    assert offset == len(result.stdout)
    receipt = dict(sampleSha256=SHA, cases=len(rows), kinds=kinds, exactFloatBits=True,
                   exactPolygonOrder=True, addresses=["10058E40", "100579C0", "10058D20", "10069F50", "100587D0"],
                   gameplay=False, livePerformance=False)
    args.output.write_text(json.dumps(receipt, indent=2))
    print(f"Original x86 differential: {len(rows)} cases passed")


if __name__ == "__main__":
    main()
