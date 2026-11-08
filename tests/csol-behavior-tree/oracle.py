"""Execute original MP CBaseZombieBossImprov behavior-tree instructions in Unicorn.

  C: composites Sequence 10625040, Selector 10625090, Parallel 10624F80 with
     stub children (vt+8 returns a scripted byte; call order recorded)
  T: skill timeline on the original std::deque code: push 10A26FC0 (with the
     map growth 10657CE0), clear 10A27640, begin 10A26FB0, advance 10A29580
  K: ZombieBoss_SkillSelector tick 10A212D0 with the original lookup 10A20F90;
     only the unordered_map find 10820530 is stubbed (int16 key compare)
  U: default ZombieBoss_TimerSkill Start 10A20F10 and Update 10A21030
     (timeline advance, IsJumpEnd latch 10A29120, row-cleared result) with
     stub OnEvent / OnJumpEnd secondary vtable slots

Stubbed outside the code under test: operator new/delete, memmove/memset,
the blackboard property object (GetInt vt28 / SetInt vt4C), child skill
vtables, owner/improv RTTI vt0 and 10A27670.

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
uc.mem_map(STACK, 0x20000); uc.mem_map(OBJ, 0x40000); uc.mem_map(HEAP, 0x200000)
w32 = lambda a, v: uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
r32 = lambda a: struct.unpack('<I', uc.mem_read(a, 4))[0]
f2b = lambda f: struct.unpack('<I', struct.pack('<f', f))[0]
SENTINEL = OBJ + 0x3FF00
uc.mem_write(SENTINEL, b'\xC3')

handlers = {}
next_stub = [OBJ + 0x30000]
def stub(ret_bytes, fn):
    addr = next_stub[0]; next_stub[0] += 0x10
    uc.mem_write(addr, b'\xC3' if ret_bytes == 0 else bytes([0xC2, ret_bytes, 0]))
    handlers[addr] = fn
    return addr
def patch(addr, ret_bytes, fn):
    uc.mem_write(addr, b'\xC3' if ret_bytes == 0 else bytes([0xC2, ret_bytes, 0]))
    handlers[addr] = fn
def code_hook(u, addr, sz, ud):
    fn = handlers.get(addr)
    if fn: fn(u.reg_read(UC_X86_REG_ESP), u.reg_read(UC_X86_REG_ECX))
uc.hook_add(UC_HOOK_CODE, code_hook)
def set_eax(v): uc.reg_write(UC_X86_REG_EAX, v & 0xffffffff)
def set_al(v): uc.reg_write(UC_X86_REG_EAX, (uc.reg_read(UC_X86_REG_EAX) & ~0xff) | (v & 0xff))

# Allocator and CRT imports used by the deque growth path.
heap = [HEAP]
def op_new(esp, ecx):
    n = r32(esp + 4); a = heap[0]; heap[0] = (a + n + 15) & ~15
    uc.mem_write(a, b'\0' * n); set_eax(a)
patch(0x11590470, 0, op_new)
patch(0x115904A0, 0, lambda esp, ecx: None)
def memmove(esp, ecx):
    dst, src, n = r32(esp + 4), r32(esp + 8), r32(esp + 12)
    if n: uc.mem_write(dst, bytes(uc.mem_read(src, n)))
    set_eax(dst)
def memset(esp, ecx):
    dst, val, n = r32(esp + 4), r32(esp + 8) & 0xff, r32(esp + 12)
    if n: uc.mem_write(dst, bytes([val]) * n)
    set_eax(dst)
def watson(esp, ecx): raise RuntimeError('_invoke_watson reached')
w32(0x11740294, stub(0, memmove)); w32(0x11740298, stub(0, memset)); w32(0x117403AC, stub(0, watson))

GLOB = OBJ + 0x100
w32(0x122788F4, GLOB)
def set_now(f): uc.mem_write(GLOB, struct.pack('<f', f))

def call(func, ecx, args):
    esp = STACK + 0x18000
    for a in reversed(args):
        esp -= 4; w32(esp, a)
    esp -= 4; w32(esp, SENTINEL)
    uc.reg_write(UC_X86_REG_ESP, esp); uc.reg_write(UC_X86_REG_EBP, STACK + 0x1C000)
    uc.reg_write(UC_X86_REG_ECX, ecx)
    uc.emu_start(func, SENTINEL, count=500000)
    return uc.reg_read(UC_X86_REG_EAX)

rng = random.Random(20261108)
lines = []

# ---------- C: composites ----------
COMP = OBJ + 0x1000; KIDS = OBJ + 0x1100; KVT = OBJ + 0x1400
order, results = [], []
kid_stubs = []
for k in range(8):
    def tick(esp, ecx, k=k):
        order.append(k); set_al(results[k])
    s = stub(8, tick)
    w32(KVT + k * 0x20 + 8, s)
    w32(KIDS + 0x100 + k * 0x10, KVT + k * 0x20)
for name, func in (('seq', 0x10625040), ('sel', 0x10625090), ('par', 0x10624F80)):
    cases = [[]] + [[a] for a in (0, 1)] + [[a, b] for a in (0, 1) for b in (0, 1)]
    cases += [[rng.randint(0, 1) for _ in range(rng.randint(0, 8))] for _ in range(300)]
    for rs in cases:
        results[:] = rs + [0] * (8 - len(rs)); order.clear()
        for k in range(len(rs)): w32(KIDS + 4 * k, KIDS + 0x100 + k * 0x10)
        w32(COMP + 8, KIDS); w32(COMP + 0xC, KIDS + 4 * len(rs))
        r = call(func, COMP, [OBJ + 0x2F00, OBJ + 0x2F80]) & 0xff
        lines.append(f'C {name} {len(rs)} {"".join(map(str, rs)) or "-"} {r} {"".join(map(str, order)) or "-"}')

# ---------- T: timeline ----------
TL = OBJ + 0x3000; HND = OBJ + 0x3100; HVT = OBJ + 0x3140
fired = []
w32(HVT, stub(8, lambda esp, ecx: fired.append(struct.unpack('<i', uc.mem_read(esp + 4, 4))[0])))
w32(HND, HVT)
specials = [0.0, -0.0, 0.5, 1.0, 1.43, 2.0, 3.26, 0.1, 10.0, -1.0, float('nan'), 1e-7]
for case in range(220):
    heap[0] = HEAP
    uc.mem_write(TL, b'\0' * 0x20); w32(TL + 4, OBJ + 0x3200)
    ops = []
    now = 0.0
    for _ in range(rng.randint(4, 28)):
        r = rng.random()
        if r < 0.38:
            delay = rng.choice(specials) if rng.random() < 0.4 else round(rng.uniform(0, 6), 3)
            id_ = rng.choice([0, 1, 2, 3, 1000, 1001, -5, 7])
            call(0x10A26FC0, TL, [f2b(delay), id_ & 0xffffffff])
            ops.append(f'P{f2b(delay):08x}:{id_}')
        elif r < 0.46:
            call(0x10A27640, TL, []); ops.append('X')
        elif r < 0.6:
            now = rng.choice([now, now + rng.choice([0.0, 0.25, 1.0, 2.5]), float('nan')]) if rng.random() < 0.15 else now + rng.uniform(0, 2)
            set_now(now); call(0x10A26FB0, TL, []); ops.append(f'B{f2b(now):08x}')
        else:
            now = now + rng.choice([0.0, 0.05, 0.1, 0.5, 1.0, 3.0]) if rng.random() < 0.8 else now - 0.5
            if math.isnan(now): now = 0.0
            set_now(now); fired.clear(); call(0x10A29580, TL, [HND, 0])
            ops.append(f'A{f2b(now):08x}:{fired[0] if fired else "n"}')
        start, count = r32(TL), r32(TL + 0x14)
        ops[-1] += f'/{start:08x}/{count}'
    lines.append('T ' + ' '.join(ops))

# ---------- K: SkillSelector ----------
SEL, CHILDREN, NODES, SVT = OBJ + 0x4000, OBJ + 0x4100, OBJ + 0x4300, OBJ + 0x4800
BB, BBVT, CTX, OWN, OVT, IMP, IVT = (OBJ + o for o in (0x5000, 0x5040, 0x5100, 0x5200, 0x5300, 0x5400, 0x5500))
NAMES = {0x1180CA88: 'next_skill', 0x1181AC14: 'cur_skill'}
for addr, s in NAMES.items():
    assert bytes(uc.mem_read(addr, len(s) + 1)) == s.encode() + b'\0'
board, log = {}, []
def bb_get(esp, ecx):
    name = NAMES[r32(esp + 4)]; set_eax(board.get(name, r32(esp + 8)))
def bb_set(esp, ecx):
    name = NAMES[r32(esp + 4)]; v = r32(esp + 8); board[name] = v
w32(BBVT + 0x28, stub(8, bb_get)); w32(BBVT + 0x4C, stub(8, bb_set))
w32(BB, BBVT); w32(CTX + 4, BB)
types, scripts = [], []
def find(esp, ecx):
    outp, keyp = r32(esp + 4), r32(esp + 8)
    key = struct.unpack('<h', uc.mem_read(keyp, 2))[0]
    node = 0
    for i, t in enumerate(types):
        if t == key: node = NODES + i * 0x20; break
    if node == 0 and rng.random() < 0.5: node = r32(SEL + 0x18)
    w32(outp + 4, node); set_eax(outp)
patch(0x10820530, 0xC, find)
def bbval(name):
    v = board.get(name); return 'u' if v is None else str(struct.unpack('<i', struct.pack('<I', v))[0])
METHODS = {8: ('tick', 8), 0x10: ('bind', 4), 0x14: ('start', 4), 0x18: ('end', 4), 0x1C: ('update', 4)}
child_stubs = {}
for k in range(6):
    for off, (m, rb) in METHODS.items():
        def fn(esp, ecx, k=k, m=m):
            arg = r32(esp + 4)
            log.append(f'{k}{m}@{bbval("cur_skill")},{bbval("next_skill")}')
            if m in ('tick', 'update'): set_al(scripts[k][m].pop(0))
        w32(SVT + k * 0x40 + off, stub(rb, fn))
owner_flags = {}
w32(OVT, stub(0, lambda esp, ecx: set_eax(0x12228E60 if owner_flags['entity'] else 0)))
w32(OWN, OVT)
w32(IVT, stub(0, lambda esp, ecx: set_eax(0x11F0A554 if owner_flags['boss'] else 0)))
w32(IMP, IVT)
patch(0x10A27670, 0, lambda esp, ecx: log.append('endskill'))
for case in range(500):
    count = rng.randint(0, 5)
    pool = [321, 322, 323, 324, 325, 326, 327, 328, 329, 330, 0, 5]
    types[:] = rng.sample(pool, count)
    for i, t in enumerate(types):
        w32(CHILDREN + 4 * i, OBJ + 0x4600 + i * 0x10)
        w32(OBJ + 0x4600 + i * 0x10, SVT + i * 0x40)
        w32(NODES + i * 0x20 + 0xC, i)
    w32(SEL + 8, CHILDREN); w32(SEL + 0xC, CHILDREN + 4 * count); w32(SEL + 0x18, OBJ + 0x4F00)
    owner_flags['entity'] = rng.random() < 0.8; owner_flags['boss'] = rng.random() < 0.8
    owner = 0 if rng.random() < 0.1 else OWN
    w32(OWN + 0x1294, 0 if rng.random() < 0.1 else IMP)
    board.clear()
    candidates = types + [-1, 999, 0x10000 + (types[0] if types else 321), 0xFFFF, 0]
    for name in ('next_skill', 'cur_skill'):
        if rng.random() < 0.85: board[name] = rng.choice(candidates) & 0xffffffff
    init = f'{bbval("next_skill")} {bbval("cur_skill")}'
    ticks = rng.randint(1, 4)
    scripts[:] = [{'update': [rng.randint(0, 1) for _ in range(ticks)], 'tick': [rng.randint(0, 1) for _ in range(ticks)]}
                  for _ in range(6)]
    script_text = ';'.join(''.join(map(str, s['update'])) + '/' + ''.join(map(str, s['tick'])) for s in scripts[:count]) or '-'
    tick_out = []
    for t in range(ticks):
        log.clear()
        r = call(0x10A212D0, SEL, [owner, CTX]) & 0xff
        if t + 1 < ticks and rng.random() < 0.3 and types:
            board['next_skill'] = rng.choice(types)
            log.append(f'inject{board["next_skill"]}')
        tick_out.append(f'{r}|{"|".join(log) or "-"}|{bbval("next_skill")},{bbval("cur_skill")}')
    flags = f'{int(owner != 0)}{int(owner_flags["entity"])}{int(r32(OWN + 0x1294) != 0)}{int(owner_flags["boss"])}'
    lines.append(f'K {",".join(map(str, types)) or "-"} {flags} {init} {ticks} {script_text} ' + ' '.join(tick_out))

# ---------- U: default timer skill Start 10A20F10 / Update 10A21030 ----------
# Object layout from the disassembly: +8 improv, +0C secondary vtable pointer
# (OnEvent +0, OnJumpEnd +4), +10 timeline; improv+1D58 monster; +1328 row.
NAMES[0x117FCBC8] = 'IsJumpEnd'
assert bytes(uc.mem_read(0x117FCBC8, 10)) == b'IsJumpEnd\0'
SK, SECV, IMPV, MON, ROW, TL2 = (OBJ + o for o in (0x6000, 0x6080, 0x6100, 0x6200, 0x6300, 0x6400))
def on_event(esp, ecx):
    log.append(f'ev{struct.unpack("<i", uc.mem_read(esp + 4, 4))[0]}')
def on_jump(esp, ecx):
    log.append(f'jump@{bbval("IsJumpEnd")}')
w32(SECV, stub(8, on_event)); w32(SECV + 4, stub(4, on_jump))
w32(SK + 8, IMPV); w32(SK + 0xC, SECV); w32(SK + 0x10, TL2); w32(IMPV + 0x1D58, MON)
for case in range(300):
    heap[0] = HEAP
    uc.mem_write(TL2, b'\0' * 0x20); w32(TL2 + 4, OBJ + 0x6500)
    junk = rng.randint(0, 3)
    for j in range(junk): call(0x10A26FC0, TL2, [f2b(1.0), 50 + j])
    ani = rng.choice([1.0, 1.43, 2.0, 2.2, 3.26, 0.5, 0.0, round(rng.uniform(0, 4), 2)])
    dd = rng.choice([0.5, 0.43, 0.56, 1.0, 1.5, 6.0, 0.0, round(rng.uniform(0, 4), 2)])
    uc.mem_write(ROW + 8, struct.pack('<ff', ani, dd)); w32(MON + 0x1328, ROW)
    board.clear(); log.clear()
    now = round(rng.uniform(0, 100), 3); set_now(now)
    call(0x10A20F10, SK, [CTX])
    head = f'U {junk} {f2b(ani):08x} {f2b(dd):08x} {f2b(now):08x} {r32(TL2):08x}/{r32(TL2 + 0x14)}'
    ticks = []
    for t in range(rng.randint(1, 12)):
        now += rng.choice([0.0, 0.1, 0.25, 0.5, 1.0, 2.0]); set_now(now)
        jump = rng.choice(['u', 'u', '0', '1', '2'])
        if jump == 'u': board.pop('IsJumpEnd', None)
        else: board['IsJumpEnd'] = int(jump)
        clear = int(rng.random() < 0.15)
        if clear: w32(MON + 0x1328, 0)
        log.clear()
        r = call(0x10A21030, SK, [CTX]) & 0xff
        ticks.append(f'{f2b(now):08x}:{jump}:{clear}:{r}:{"|".join(log) or "-"}:{bbval("IsJumpEnd")}:{r32(TL2 + 0x14)}')
    lines.append(head + ' ' + ' '.join(ticks))

open(out, 'w', encoding='ascii').write('\n'.join(lines) + '\n')
print(len(lines), 'lines')
