"""Execute original MP CLightZombieBoss / lightzb_bomb instructions in Unicorn.

  A: AngleMod 11530220
  R: radius falloff 1152D8A0
  B: bomb hit gate 10984A60 (TakeDamage 1072D580 recorded, messages stubbed)
  K: bomb tracking 10988900
  J: jump flight fragment 10A291F7..10A292F4 (exit 10A292FA landed / 10A29572)
  E: Hide event 0 (10986760), O: Hide Tick (10987940), I: fade-in (109887B0)
  C: CrazyBomb Tick 10987630
  L: BombJump relocation 109859A0
  W: bomb TakeDamage reward fragment 109886A1..10988758

Stubbed: engine message/random/attachment calls, blackboard property object,
entity vtables (alive, vt128 points, vt6C, vtA0), RTTI vt0, 10823570
difficulty getter, nav lookup 11348AB0, TakeDamage, effects. x87 runs with the
Windows default control word 0x27F (53-bit precision).

usage: python -I oracle.py <mp.dll> <out.txt>
"""
import math, random, struct, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *

mp, out = sys.argv[1], sys.argv[2]
d = open(mp, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]; nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = pe + 24; base = struct.unpack_from('<I', d, opt + 28)[0]; size = struct.unpack_from('<I', d, opt + 56)[0]
so = opt + struct.unpack_from('<H', d, pe + 20)[0]
uc = Uc(UC_ARCH_X86, UC_MODE_32)
uc.mem_map(base, (size + 0xfff) & ~0xfff)
for i in range(nsec):
    _, vs, va, rs, rp = struct.unpack_from('<8sIIII', d, so + 40 * i)
    uc.mem_write(base + va, d[rp:rp + min(rs, vs if vs else rs)])
STACK, OBJ, HEAP = 0x00100000, 0x00300000, 0x00400000
uc.mem_map(STACK, 0x20000); uc.mem_map(OBJ, 0x40000); uc.mem_map(HEAP, 0x100000)
w32 = lambda a, v: uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
r32 = lambda a: struct.unpack('<I', uc.mem_read(a, 4))[0]
wf = lambda a, f: uc.mem_write(a, struct.pack('<f', f))
rf = lambda a: struct.unpack('<f', uc.mem_read(a, 4))[0]
f2b = lambda f: struct.unpack('<I', struct.pack('<f', f))[0]
def fv(a): return ','.join(f'{r32(a + 4 * i):08x}' for i in range(3))
def hx(f): return f'{f2b(f):08x}'

SENTINEL = OBJ + 0x3FF00
uc.mem_write(SENTINEL, b'\xC3')
RES = OBJ + 0x3FE00
FSTP = OBJ + 0x3FD00  # fstp dword [RES]; jmp SENTINEL
uc.mem_write(FSTP, bytes([0xD9, 0x1D]) + struct.pack('<I', RES) + b'\xE9' +
             struct.pack('<i', SENTINEL - (FSTP + 11)))

handlers = {}
next_stub = [OBJ + 0x30000]
def ret_bytes(n): return b'\xC3' if n == 0 else bytes([0xC2, n, 0])
def stub(n, fn):
    a = next_stub[0]; next_stub[0] += 0x20
    uc.mem_write(a, ret_bytes(n)); handlers[a] = fn; return a
def fld_stub(n, fn):
    """fn(esp, ecx) returns the float to load into st0."""
    a = next_stub[0]; next_stub[0] += 0x20; slot = a + 0x18
    uc.mem_write(a, bytes([0xD9, 0x05]) + struct.pack('<I', slot) + ret_bytes(n))
    handlers[a] = lambda esp, ecx: wf(slot, fn(esp, ecx)); return a
def patch(addr, n, fn):
    uc.mem_write(addr, ret_bytes(n)); handlers[addr] = fn
def patch_fld(addr, n, fn):
    slot = next_stub[0]; next_stub[0] += 0x20
    uc.mem_write(addr, bytes([0xD9, 0x05]) + struct.pack('<I', slot) + ret_bytes(n))
    handlers[addr] = lambda esp, ecx: wf(slot, fn(esp, ecx))
stops, stop_hit = set(), {}
def code_hook(u, addr, sz, ud):
    fn = handlers.get(addr)
    if fn: fn(u.reg_read(UC_X86_REG_ESP), u.reg_read(UC_X86_REG_ECX))
    if addr in stops: stop_hit['a'] = addr; u.emu_stop()
uc.hook_add(UC_HOOK_CODE, code_hook)
def set_eax(v): uc.reg_write(UC_X86_REG_EAX, v & 0xffffffff)
def reset_fpu():
    uc.reg_write(UC_X86_REG_FPCW, 0x27F)

def call(func, ecx, args, fstp=False, extra=None):
    esp = STACK + 0x18000
    for a in reversed(args):
        esp -= 4; w32(esp, a)
    esp -= 4; w32(esp, FSTP if fstp else SENTINEL)
    uc.reg_write(UC_X86_REG_ESP, esp); uc.reg_write(UC_X86_REG_EBP, STACK + 0x1C000)
    uc.reg_write(UC_X86_REG_ECX, ecx); reset_fpu()
    if extra: extra()
    try:
        uc.emu_start(func, SENTINEL, count=400000)
    except Exception:
        print(f'fault in {func:08x} at eip {uc.reg_read(UC_X86_REG_EIP):08x}', file=sys.stderr); raise
    return uc.reg_read(UC_X86_REG_EAX)

# Original cosine table initializer; its CRT cos import (11740350) takes and
# returns a double in xmm0 (same stub as tests/csol-revenant/oracle.py).
def crt_cos(esp, ecx):
    x = struct.unpack('<d', struct.pack('<Q', uc.reg_read(UC_X86_REG_XMM0) & 0xffffffffffffffff))[0]
    uc.reg_write(UC_X86_REG_XMM0, struct.unpack('<Q', struct.pack('<d', math.cos(x)))[0])
w32(0x11740350, stub(0, crt_cos))
uc.reg_write(UC_X86_REG_ESP, STACK + 0x10000); w32(STACK + 0x10000, 0x1133C630); reset_fpu()
uc.emu_start(0x1133C5F0, 0x1133C630)
assert rf(0x12227DE8) == 1.0

GLOB = OBJ + 0x100
w32(0x122788F4, GLOB)

# Tunable value pointers. Their static initializers (10148xxx: [ebp-4] =
# default, null std::function, 106DB260, atexit) use SEH through fs:[0], which
# this harness does not model, so each value pointer is set to its statically
# recovered default (analysis/csol-lightzombie-20261108/tunables-all.txt) and
# the std::function slot (pointer - 4) is checked to be the zero-filled .bss.
TUNABLES = {0x11EF6678: 0.0, 0x11EF65A0: 3.0, 0x11EF6B80: 180.0, 0x11EF6758: 1.0,
            0x11EF6930: 1000.0, 0x11EF6B30: 500.0, 0x11EF6A98: 1000.0, 0x11EF6890: 180.0}
TVALUES = OBJ + 0x200
for i, (pointer, value) in enumerate(TUNABLES.items()):
    assert r32(pointer - 4) == 0 and r32(pointer) == 0, hex(pointer)
    wf(TVALUES + 4 * i, value); w32(pointer, TVALUES + 4 * i)
def set_now(f): wf(GLOB, f)
rng = random.Random(20261109)
lines = []
def rfloat(lo, hi, specials=()):
    if specials and rng.random() < 0.15: return rng.choice(specials)
    return struct.unpack('<f', struct.pack('<f', rng.uniform(lo, hi)))[0]

# Common engine stubs.
for slot in (0x12278664, 0x1227866C, 0x12278674, 0x12278668, 0x12278688, 0x12278684):
    w32(slot, stub(0, lambda esp, ecx: None))
patch(0x1152C520, 0, lambda esp, ecx: set_eax(0))
patch(0x113E7AE0, 0, lambda esp, ecx: None)
patch(0x113F88F0, 0, lambda esp, ecx: None)
taken = []
patch(0x1072D580, 0x10, lambda esp, ecx: taken.append(r32(esp + 0xC)))
randoms, random_args = [], []
def random_float(esp, ecx):
    random_args.append((r32(esp + 4), r32(esp + 8))); return randoms.pop(0)
w32(0x12278720, fld_stub(0, random_float))

# ---------- A ----------
for v in [0.0, -0.0, 360.0, -360.0, 720.0, 360.5, -0.5, 1e-7, -1e-7, 359.99997, 3600.25, -3600.25] + \
         [rfloat(-2000, 2000) for _ in range(400)]:
    call(0x11530220, 0, [f2b(v)], fstp=True)
    lines.append(f'A {hx(v)} {r32(RES):08x}')

# ---------- R ----------
SRC, PT, ODIR = OBJ + 0x1000, OBJ + 0x1010, OBJ + 0x1020
for n in range(500):
    s = [rfloat(-500, 500) for _ in range(3)]
    p = [s[i] + rfloat(-260, 260, (0.0,)) for i in range(3)]
    dmg = rng.choice([300.0, 1000.0, 1.0, 0.0, rfloat(0, 2000)])
    rad = rng.choice([200.0, 100.0, 0.0, 1e-3, rfloat(0, 400)])
    for i in range(3): wf(SRC + 4 * i, s[i]); wf(PT + 4 * i, p[i])
    uc.mem_write(ODIR, b'\xAA' * 12)
    call(0x1152D8A0, 0, [SRC, PT, f2b(dmg), f2b(rad), ODIR], fstp=True)
    lines.append(f'R {fv(SRC)} {fv(PT)} {hx(dmg)} {hx(rad)} {r32(RES):08x} {fv(ODIR)}')

# ---------- shared entity objects ----------
BOMB, BPEV, ROWB, TGT, TPEV, TVT = (OBJ + o for o in (0x2000, 0x2400, 0x2800, 0x2A00, 0x2C00, 0x2E00))
w32(BOMB + 8, BPEV); w32(TGT + 8, TPEV); w32(TGT, TVT)
alive = {'v': 1}
w32(TVT + 0xA8, stub(0, lambda esp, ecx: set_eax(alive['v'])))

# ---------- B ----------
for n in range(700):
    taken.clear()
    w32(BOMB + 0xD8, ROWB); w32(BOMB + 0xDC, 0); w32(BOMB + 0x6C, 1); w32(TGT + 0x6C, 2)
    now = rfloat(1, 100); set_now(now)
    nxt = rng.choice([now - 1.0, now, now + 0.5, 0.0])
    wf(BOMB + 0xE8, nxt)
    b = [rfloat(-400, 400) for _ in range(3)]
    t = [b[0] + rfloat(-200, 200), b[1] + rfloat(-200, 200), b[2] + rfloat(-260, 160)]
    if rng.random() < 0.05: t[1] = b[1]; t[0] = b[0]
    mins = rng.choice([-36.0, -18.0, 0.0, rfloat(-40, 0)])
    yaw = rfloat(-360, 720, (0.0, 90.0, 180.0, 270.0, 360.0))
    dist = rng.choice([118.11, 236.22, 0.0, rfloat(0, 300)])
    height = rng.choice([118.11, 0.0, rfloat(0, 200)])
    cosv = rng.choice([0.0, -1.0, 1.0, 0.7071067690849304, rfloat(-1, 1)])
    dmg, force = rng.choice([200.0, 400.0]), rng.choice([1.0, 3.0, 0.0])
    for i in range(3): wf(BPEV + 8 + 4 * i, b[i]); wf(TPEV + 8 + 4 * i, t[i])
    wf(BPEV + 0xF4, mins); wf(BPEV + 0x54, yaw)
    for off, v in ((0x10, dmg), (0x14, cosv), (0x18, height), (0x1C, dist), (0x20, force)): wf(ROWB + off, v)
    r = call(0x10984A60, BOMB, [TGT]) & 0xff
    lines.append(f'B {hx(now)} {hx(nxt)} {fv(BPEV + 8)} {hx(mins)} {hx(yaw)} {fv(TPEV + 8)} '
                 f'{hx(dmg)} {hx(cosv)} {hx(height)} {hx(dist)} {hx(force)} {r} '
                 f'{(f"{taken[0]:08x}" if taken else "-")} {r32(BOMB + 0xE8):08x}')

# ---------- K ----------
KCTX, KBB, KBBV, TURN = OBJ + 0x3000, OBJ + 0x3040, OBJ + 0x3080, OBJ + 0x3100
BVT = OBJ + 0x3200
w32(BOMB, BVT); w32(BOMB + 0xD4, KCTX); w32(KCTX + 4, KBB); w32(KBB, KBBV)
w32(KBBV + 0x28, stub(8, lambda esp, ecx: set_eax(7)))
patch(0x115306A0, 0, lambda esp, ecx: set_eax(TGT))
w32(BVT + 0x128, stub(0, lambda esp, ecx: set_eax(TURN)))
for n in range(600):
    o = [rfloat(-400, 400) for _ in range(3)]
    turn = [o[0] + rfloat(-2, 2, (0.0,)), o[1] + rfloat(-2, 2, (0.0,)), o[2]]
    t = [o[0] + rfloat(-300, 300, (0.0, 0.005)), o[1] + rfloat(-300, 300, (0.0, -0.005)), o[2] + rfloat(-50, 50)]
    vel = [rfloat(-800, 800, (0.0,)), rfloat(-800, 800, (0.0,)), rfloat(-300, 300, (0.0,))]
    yaw = rfloat(-180, 540, (0.0, 90.0, 180.0, 270.0))
    maxs = rng.choice([800.0, 0.0, rfloat(0, 1000)])
    dt = rng.choice([0.03333, 0.1, rfloat(0, 0.2)])
    for i in range(3):
        wf(BPEV + 8 + 4 * i, o[i]); wf(TURN + 4 * i, turn[i]); wf(TPEV + 8 + 4 * i, t[i]); wf(BPEV + 0x20 + 4 * i, vel[i])
    wf(BPEV + 0x54, yaw); wf(BPEV + 0x50, 7.0); wf(BPEV + 0x58, 9.0); wf(BPEV + 0x240, maxs)
    call(0x10988900, BOMB, [f2b(dt)])
    lines.append(f'K {hx(dt)} {hx(yaw)} {",".join(hx(x) for x in turn)} {",".join(hx(x) for x in o)} '
                 f'{",".join(hx(x) for x in vel)} {hx(maxs)} {",".join(hx(x) for x in t)} '
                 f'{r32(BPEV + 0x54):08x} {r32(BPEV + 0x50):08x} {r32(BPEV + 0x58):08x} {fv(BPEV + 0x20)}')

# ---------- J ----------
IMP, MON, MPEV, CENTER = OBJ + 0x4000, OBJ + 0x6000, OBJ + 0x6400, OBJ + 0x6800
w32(IMP + 0x1D58, MON); w32(MON + 8, MPEV)
stops.update({0x10A292FA, 0x10A29572})
for n in range(500):
    dest = [rfloat(-500, 500) for _ in range(3)]
    cen = [rfloat(-500, 500) for _ in range(3)]
    if rng.random() < 0.1: cen = list(dest)
    prog = rng.choice([0.0, 0.5, 0.99, 1.0, rfloat(0, 1)])
    step = rng.choice([0.01, 1.0, 0.5, rfloat(0, 1)])
    speed = rng.choice([1000.0, 600.0, rfloat(0, 1500)])
    thr = rng.choice([20.0, 0.0, -20.0])
    for i in range(3): wf(IMP + 0x1BE4 + 4 * i, dest[i]); wf(CENTER + 4 * i, cen[i])
    wf(IMP + 0x1E78, speed); wf(IMP + 0x1E7C, step); wf(IMP + 0x1E80, prog); wf(IMP + 0x1E84, thr)
    esp = STACK + 0x18000
    uc.reg_write(UC_X86_REG_ESP, esp); uc.reg_write(UC_X86_REG_EDI, IMP); uc.reg_write(UC_X86_REG_EAX, CENTER)
    reset_fpu()
    stop_hit.clear()
    uc.emu_start(0x10A291F7, 0xFFFFFFFF, count=20000)
    assert stop_hit.get('a') in (0x10A292FA, 0x10A29572)
    landed = int(stop_hit['a'] == 0x10A292FA)
    lines.append(f'J {",".join(hx(x) for x in dest)} {",".join(hx(x) for x in cen)} {hx(prog)} {hx(step)} '
                 f'{hx(speed)} {hx(thr)} {fv(MPEV + 0x20)} {r32(IMP + 0x1E80):08x} {landed}')
stops.clear()

# ---------- E / O / I (Hide) ----------
S, SROW, HCTX, HBB, HBBV, IVT, TL = (OBJ + o for o in (0x7000, 0x7100, 0x7200, 0x7240, 0x7280, 0x7300, 0x7400))
w32(S + 8, IMP); w32(S + 0x10, TL); w32(MON + 0x1328, SROW)
w32(HCTX + 4, HBB); w32(HBB, HBBV)
sets = []
hide_get = {'v': 0.0}
w32(HBBV + 0x50, stub(0xC, lambda esp, ecx: sets.append(r32(esp + 8))))
w32(HBBV + 0x2C, fld_stub(8, lambda esp, ecx: hide_get['v']))
rtti = {'v': 0x11EF6BA0}
w32(IMP, IVT); w32(IVT, stub(0, lambda esp, ecx: set_eax(rtti['v'])))
heap = [HEAP]
def op_new(esp, ecx):
    n = r32(esp + 4); a = heap[0]; heap[0] = (a + n + 15) & ~15
    uc.mem_write(a, b'\0' * n); set_eax(a)
patch(0x11590470, 0, op_new); patch(0x115904A0, 0, lambda esp, ecx: None)
def memmove(esp, ecx):
    dst, src, n = r32(esp + 4), r32(esp + 8), r32(esp + 12)
    if n: uc.mem_write(dst, bytes(uc.mem_read(src, n)))
    set_eax(dst)
def memset(esp, ecx):
    dst, val, n = r32(esp + 4), r32(esp + 8) & 0xff, r32(esp + 12)
    if n: uc.mem_write(dst, bytes([val]) * n)
    set_eax(dst)
w32(0x11740294, stub(0, memmove)); w32(0x11740298, stub(0, memset))
for n in range(300):
    dmg = rng.choice([1.0, 0.1, 0.05, 0.0, -1.0, 2.2, float('nan'), rfloat(0, 5)])
    wf(SROW + 0x10, dmg); w32(MPEV + 0x160, 0); wf(MPEV + 0x164, 0.0); sets.clear()
    call(0x10986760, S + 0xC, [0, HCTX])
    lines.append(f'E {hx(dmg)} {r32(MPEV + 0x160)} {r32(MPEV + 0x164):08x} {" ".join(f"{v:08x}" for v in sets) or "-"}')
for n in range(500):
    heap[0] = HEAP; uc.mem_write(TL, b'\0' * 0x20); w32(TL + 4, OBJ + 0x7500)
    mode = rng.choice([4, 4, 4, 0])
    amt = rng.choice([255.0, 0.0, 1.0, 8.5, rfloat(-5, 255)])
    hv = rng.choice([-1.0, 0.0, 8.5, 255.0, rfloat(0, 20)])
    rush = rng.choice([10.0, 0.0, rfloat(0, 20)])
    rtti['v'] = 0x11EF6BA0 if rng.random() < 0.85 else 0
    now = rfloat(0, 100); set_now(now)
    w32(MPEV + 0x160, mode); wf(MPEV + 0x164, amt); w32(MPEV + 0x174, 22); wf(IMP + 0x1E88, -1.0)
    wf(SROW + 0x44, rush); hide_get['v'] = hv; sets.clear()
    r = call(0x10987940, S, [OBJ + 0x7600, HCTX]) & 0xff
    lines.append(f'O {mode} {hx(amt)} {hx(hv)} {hx(rush)} {int(rtti["v"] != 0)} {hx(now)} {r} '
                 f'{r32(MPEV + 0x164):08x} {r32(MPEV + 0x174)} {r32(IMP + 0x1E88):08x} '
                 f'{r32(TL):08x}/{r32(TL + 0x14)} {" ".join(f"{v:08x}" for v in sets) or "-"}')
patch(0x10A29100, 0, lambda esp, ecx: None)
restores = []
w32(IVT + 0x130, stub(0, lambda esp, ecx: restores.append(1)))
for n in range(400):
    mode = rng.choice([4, 4, 0])
    amt = rng.choice([0.0, 250.0, 254.9, 255.0, rfloat(0, 260)])
    now = rfloat(0, 100); set_now(now)
    re = rng.choice([-1.0, now - 0.1, now, now + 1.0, float('nan')])
    w32(MPEV + 0x160, mode); wf(MPEV + 0x164, amt); w32(MPEV + 0x174, 0); wf(IMP + 0x1E88, re); restores.clear()
    call(0x109887B0, IMP, [])
    lines.append(f'I {mode} {hx(amt)} {hx(now)} {hx(re)} {r32(MPEV + 0x164):08x} {r32(MPEV + 0x174)} '
                 f'{r32(IMP + 0x1E88):08x} {len(restores)}')

# ---------- C (CrazyBomb Tick) ----------
OWN, OPEV = OBJ + 0x7800, OBJ + 0x7A00
w32(OWN + 8, OPEV)
crazy_get = {'interval': 0.0, 'rhand': 0}
CBBV = OBJ + 0x7C00; CBB = OBJ + 0x7C80; CCTX = OBJ + 0x7CC0
w32(CCTX + 4, CBB); w32(CBB, CBBV)
csets = []
w32(CBBV + 0x2C, fld_stub(8, lambda esp, ecx: crazy_get['interval']))
w32(CBBV + 0x28, stub(8, lambda esp, ecx: set_eax(crazy_get['rhand'])))
w32(CBBV + 0x50, stub(0xC, lambda esp, ecx: csets.append(('f', r32(esp + 8)))))
w32(CBBV + 0x4C, stub(8, lambda esp, ecx: csets.append(('i', r32(esp + 8)))))
attach, throws = [], []
patch(0x113F8490, 0x10, lambda esp, ecx: attach.append(r32(esp + 4)))
def throw(esp, ecx):
    dp = r32(esp + 8); throws.append((fv(dp), r32(esp + 0xC)))
patch(0x10987BB0, 0xC, throw)
for n in range(500):
    seq = rng.choice([22, 22, 22, 23])
    now = rfloat(0, 100); set_now(now)
    crazy_get['interval'] = rng.choice([-1.0, now - 0.1, now, now + 0.2])
    crazy_get['rhand'] = rng.choice([0, 1, 2])
    yaw = rfloat(-360, 720, (0.0, 90.0, 270.0, 359.0))
    ry = rfloat(-90, 90, (0.0, 90.0, -90.0))
    rs = rfloat(500, 1000, (500.0, 1000.0))
    delay = rng.choice([0.1, 0.5, rfloat(0, 1)])
    w32(OPEV + 0x138, seq); wf(MPEV + 0x54, yaw); wf(SROW + 0xC, delay)
    randoms[:] = [ry, rs]; random_args.clear(); attach.clear(); throws.clear(); csets.clear()
    r = call(0x10987630, S, [OWN, CCTX]) & 0xff
    tail = '-'
    if throws:
        (dirs, speed) = throws[0]
        tail = (f'{attach[0]} {random_args[0][0]:08x},{random_args[0][1]:08x} '
                f'{random_args[1][0]:08x},{random_args[1][1]:08x} {dirs} {speed:08x} '
                + ' '.join(f'{k}{v:08x}' for k, v in csets))
    lines.append(f'C {seq} {hx(now)} {hx(crazy_get["interval"])} {crazy_get["rhand"]} {hx(yaw)} {hx(ry)} {hx(rs)} '
                 f'{hx(delay)} {r} {tail}')

# ---------- L (relocation) ----------
HANDLE, TENT, TEVT, TPT, MVT, OPT, OUTP, AREA = (OBJ + o for o in (0x8000, 0x8040, 0x8080, 0x8100, 0x8140, 0x8180, 0x81C0, 0x8200))
w32(TENT, TEVT); w32(TEVT + 0x128, stub(0, lambda esp, ecx: set_eax(TPT)))
w32(MON, MVT); w32(MVT + 0x128, stub(0, lambda esp, ecx: set_eax(OPT)))
jumping = {'v': 0}
w32(IVT + 0x6C, stub(0xC, lambda esp, ecx: None))
w32(IVT + 0xA0, stub(0, lambda esp, ecx: set_eax(jumping['v'])))
nav = {'area': 0}; nav_args = []
def nav_find(esp, ecx):
    nav_args.append((ecx, fv(r32(esp + 4)), r32(esp + 8))); set_eax(nav['area'])
patch(0x11348AB0, 8, nav_find)
for n in range(600):
    has_target = rng.random() < 0.8
    w32(IMP + 0x1D30, HANDLE if has_target else 0); w32(HANDLE + 4, 1); w32(IMP + 0x1D34, TENT)
    o = [rfloat(-1500, 1500) for _ in range(3)]
    tp = [o[0] + rfloat(-1500, 1500, (0.0,)), o[1] + rfloat(-1500, 1500, (0.0,)), o[2] + rfloat(-100, 100)]
    dest = [o[0] + rfloat(-800, 800), o[1] + rfloat(-800, 800), o[2]]
    for i in range(3): wf(OPT + 4 * i, o[i]); wf(TPT + 4 * i, tp[i]); wf(IMP + 0x1BE4 + 4 * i, dest[i])
    jumping['v'] = int(rng.random() < 0.3)
    yaw = rfloat(-180, 540, (0.0, 90.0)); wf(MPEV + 0x54, yaw)
    ry = rfloat(-90, 90, (0.0, -90.0, 90.0)); randoms[:] = [ry]; random_args.clear(); nav_args.clear()
    has_area = rng.random() < 0.6
    lo = [rng.choice([tp[i] - rfloat(0, 1200), -1e9]) for i in range(3)]
    hi = [rng.choice([tp[i] + rfloat(0, 1200), 1e9]) for i in range(3)]
    ce = [(lo[i] + hi[i]) * 0.5 if abs(lo[i]) < 1e8 and abs(hi[i]) < 1e8 else tp[i] for i in range(3)]
    for i in range(3): wf(AREA + 8 + 4 * i, lo[i]); wf(AREA + 0x14 + 4 * i, hi[i]); wf(AREA + 0x20 + 4 * i, ce[i])
    nav['area'] = AREA if has_area else 0
    point = TPT if has_target else OPT
    call(0x109859A0, S, [OUTP])
    lines.append(f'L {int(has_target)} {jumping["v"]} {fv(OPT)} {fv(point)} {fv(IMP + 0x1BE4)} {hx(yaw)} {hx(ry)} '
                 f'{int(has_area)} {fv(AREA + 8)} {fv(AREA + 0x14)} {fv(AREA + 0x20)} '
                 f'{random_args[0][0]:08x},{random_args[0][1]:08x} {nav_args[0][0]:08x} {nav_args[0][1]} '
                 f'{nav_args[0][2]:08x} {fv(OUTP)}')

# ---------- W (reward fragment) ----------
params = {}
patch_fld(0x10823570, 0, lambda esp, ecx: params[r32(esp + 4)])
PLAYER, SVC, ROOM, RVT = OBJ + 0x9000, OBJ + 0xB800, OBJ + 0xB900, OBJ + 0xBA00
option = {'v': 0}
w32(0x12278A14, SVC); w32(SVC + 0x1EC, ROOM); w32(ROOM, RVT)
w32(RVT + 0x1C4, stub(0, lambda esp, ecx: set_eax(option['v'])))
for n in range(500):
    for c in (22, 23, 24, 25, 26):
        params[c] = rng.choice([0.0, 1.0, 0.5, 10.0, 100.0, rfloat(0, 50), rfloat(0, 3)])
    if rng.random() < 0.05: params[23] = float('nan')
    dmg = rng.choice([0.0, 1.0, 37.5, 1500.0, rfloat(0, 3000), 1e10])
    money = rfloat(0, 5000, (0.0, 0.999, 1e10))
    flag = int(rng.random() < 0.5); option['v'] = rng.choice([0x320, 0, 1])
    uc.mem_write(PLAYER + 0x20AE, bytes([flag]))
    randoms[:] = [money]; random_args.clear()
    ebp = STACK + 0x1C000
    wf(ebp + 0x10, dmg)
    uc.reg_write(UC_X86_REG_EBP, ebp); uc.reg_write(UC_X86_REG_ESP, ebp - 0x40)
    uc.reg_write(UC_X86_REG_EDI, PLAYER); reset_fpu()
    uc.emu_start(0x109886A1, 0x10988758, count=20000)
    pts = struct.unpack('<i', uc.mem_read(ebp - 0x18, 4))[0]
    mon = struct.unpack('<i', struct.pack('<I', uc.reg_read(UC_X86_REG_ESI)))[0]
    lines.append(f'W {" ".join(hx(params[c]) for c in (22, 23, 24, 25, 26))} {hx(dmg)} {hx(money)} {flag} '
                 f'{option["v"]} {random_args[0][0]:08x},{random_args[0][1]:08x} {pts} {mon}')

open(out, 'w', encoding='ascii').write('\n'.join(lines) + '\n')
print(len(lines), 'lines')
