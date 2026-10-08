"""Execute the identity-matched CS client's actual x86 HUD readers in Unicorn.

Uses only a workspace binary copy. Engine sprite/VGUI callbacks are inert;
parsing, integer stores, player indexing and stack cleanup execute real code.
"""
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parent.parent


def main():
    config = json.loads((ROOT / "native/client/vitals_10210.json").read_text())
    data = (ROOT / "analysis/engine/cs-client-10210/client.dll").read_bytes()
    assert hashlib.sha256(data).hexdigest() == config["clientSha256"]
    pe = struct.unpack_from('<I', data, 60)[0]
    count, optional = struct.unpack_from('<H', data, pe + 6)[0], struct.unpack_from('<H', data, pe + 20)[0]
    base, size = struct.unpack_from('<I', data, pe + 52)[0], struct.unpack_from('<I', data, pe + 80)[0]
    mu = Uc(UC_ARCH_X86, UC_MODE_32)
    mu.mem_map(base, (size + 4095) & ~4095)
    for i in range(count):
        _, rva, length, offset = struct.unpack_from('<IIII', data, pe + 24 + optional + i * 40 + 8)
        if length:
            mu.mem_write(base + rva, data[offset:offset + length])
    scratch = 0x20000000
    mu.mem_map(scratch, 0x20000)
    sp, payload, callback, done = scratch + 0x10000, scratch + 0x11000, scratch + 0x12000, scratch + 0x12010
    mu.mem_write(callback, b'\xc3')

    def put(address, value):
        mu.mem_write(address, struct.pack('<I', value))

    def get(address):
        return struct.unpack('<I', mu.mem_read(address, 4))[0]

    # VGUI observer refresh: preserve the actual stores, stub only virtual UI refresh.
    put(base + 0x139f58, scratch)
    put(scratch, scratch + 0x100)
    put(scratch + 0x124, callback)
    put(base + 0x110e14, 31)
    checks = {}

    def message(rva, data):
        mu.mem_write(payload, data)
        mu.mem_write(sp, struct.pack('<IIII', done, 0, len(data), payload))
        mu.reg_write(UC_X86_REG_ESP, sp)
        mu.emu_start(base + rva, done, count=1000)
        assert mu.reg_read(UC_X86_REG_ESP) == sp + 4
        assert mu.reg_read(UC_X86_REG_EAX) == 1

    message(0x4ff60, struct.pack('<i', 1000))
    checks['stockReaderTruncates1000'] = get(base + 0x117d50) == 232
    for name, rva, target in config['calls'][:4]:
        address = base + int(rva, 16)
        assert mu.mem_read(address, 1) == b'\xe8'
        mu.mem_write(address + 1, struct.pack('<i', base + 0x5d150 - address - 5))
    mu.ctl_remove_cache(base, base + size)
    for n in (0, 1, 255, 256, 512, 999, 1000, 32767, 32768, 65535, 65536, 1000000, 2147483647):
        data = struct.pack('<i', n)
        message(0x4ff60, data)
        message(0x472e0, data)
        message(0x51d60, data)
        message(0x51d10, data + bytes([32]))
        checks[f'actualReadersAndPlayerSlots{n}'] = (get(base + 0x117d50) == n and get(base + 0x119644) == n
            and get(base + 0x12581c + 116 * 31) == n and get(base + 0x12581c + 116 * 32) == n)
    # Exercise native digit drawing and its ret 28 with an exact synthetic HUD.
    hud, sprites, rects = scratch + 0x2000, scratch + 0x8000, scratch + 0x9000
    put(hud + 4565 * 4, 0); put(hud + 94 * 4, sprites); put(hud + 95 * 4, rects)
    for digit in range(11):
        put(sprites + digit * 4, digit)
        mu.mem_write(rects + digit * 16, struct.pack('<iiii', 0, 12, 0, 20))
    put(base + 0x117870 + 48 * 4, callback)
    put(base + 0x117870 + 51 * 4, callback + 32)
    mu.mem_write(callback + 32, b'\xc3')
    drawn = []

    def on_code(uc, address, size, user):
        if address == callback:
            drawn.append(get(uc.reg_read(UC_X86_REG_ESP) + 4))

    mu.hook_add(UC_HOOK_CODE, on_code)
    mu.ctl_remove_cache(base, base + size)
    mu.ctl_remove_cache(scratch, scratch + 0x20000)

    def draw(value, flags=1):
        drawn.clear()
        mu.mem_write(sp, struct.pack('<8I', done, 10, 20, flags, value, 255, 160, 0))
        mu.reg_write(UC_X86_REG_ESP, sp); mu.reg_write(UC_X86_REG_ECX, hud)
        mu.emu_start(base + 0x53ad0, done, count=1000)
        assert mu.reg_read(UC_X86_REG_ESP) == sp + 32
        return list(drawn)

    checks['stockDraw1000EscapesDigits'] = draw(1000)[0] == 10
    checks['nativeSingleDigitsAndX86Stack'] = all(draw(n) == [n] for n in range(10))
    out = ROOT / 'analysis/vitals/client-x86.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    report = {'clientSha256': config['clientSha256'], 'checks': checks, 'passed': all(checks.values())}
    out.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
