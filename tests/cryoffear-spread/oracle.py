# SPDX-License-Identifier: GPL-3.0-or-later
"""Run original CoF x86 instructions. Never distribute the private sample."""
import hashlib
import random
import struct

from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP, UC_X86_REG_MXCSR

SAMPLE_SHA256 = '0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63'


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def generate(sample, destination):
    binary = sample.read_bytes()
    if hashlib.sha256(binary).hexdigest() != SAMPLE_SHA256:
        raise ValueError('Original sample identity mismatch')
    pe = struct.unpack_from('<I', binary, 0x3c)[0]
    if struct.unpack_from('<H', binary, pe + 4)[0] != 0x14c:
        raise ValueError('Expected original x86 PE')
    sections, optional_size = struct.unpack_from('<H', binary, pe + 6)[0], struct.unpack_from('<H', binary, pe + 20)[0]
    optional = pe + 24
    image_base = struct.unpack_from('<I', binary, optional + 28)[0]
    image_size = struct.unpack_from('<I', binary, optional + 56)[0]
    if image_base != 0x10000000:
        raise ValueError('Unexpected image base')
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(image_base, (image_size + 4095) & ~4095)
    for i in range(sections):
        section = optional + optional_size + i * 40
        rva, size, raw = struct.unpack_from('<III', binary, section + 12)
        if size:
            machine.mem_write(image_base + rva, binary[raw:raw + size])
    code_sha = hashlib.sha256(machine.mem_read(0x1011BB50, 1132)).hexdigest()
    machine.mem_map(0x30000000, 0x10000)
    machine.mem_map(0x40000000, 0x10000)
    weapon, owner, pev, time = 0x40000000, 0x40001000, 0x40002000, 0x40003000

    def write(address, value):
        machine.mem_write(address, struct.pack('<I', value & 0xffffffff))

    def read(address):
        return struct.unpack('<I', machine.mem_read(address, 4))[0]

    write(owner + 4, pev)
    write(0x10223188, time)
    visited = set()

    def instruction(uc, address, size, user_data):
        if not 0x1011BB50 <= address < 0x1011BFBC:
            raise ValueError(f'Unexpected original external call {address:08x}')
        visited.add(address)

    machine.hook_add(UC_HOOK_CODE, instruction)
    flag_addresses = (0x10231F0A, 0x10231F0B, 0x10231F0E, 0x10231F0F)
    count = 0

    def execute(row):
        nonlocal count
        for offset, value in zip((0x19c, 0x1a0, 0x18c, 0x190, 0x194), row[:5]):
            write(weapon + offset, value)
        write(0x10231F5C, row[5])
        for address, value in zip(flag_addresses, row[6:10]):
            machine.mem_write(address, bytes([value]))
        write(pev + 0x20, row[10])
        write(pev + 0x24, row[11])
        write(time, row[12])
        write(pev + 0x1a8, row[13])
        write(weapon + 0x168, row[14])
        write(weapon + 0x160, owner if row[15] else 0)
        stack = 0x30008000
        write(stack, 0x50000000)
        machine.reg_write(UC_X86_REG_ESP, stack)
        machine.reg_write(UC_X86_REG_ECX, weapon)
        machine.reg_write(UC_X86_REG_MXCSR, 0x1f80)
        machine.emu_start(0x1011BB50, 0x50000000, count=1000)
        if machine.reg_read(UC_X86_REG_EIP) != 0x50000000:
            raise ValueError('Original instruction execution did not return')
        result = [read(weapon + offset) for offset in (0x19c, 0x1a0, 0x18c, 0x190, 0x194)]
        result += [read(0x10231F5C)]
        result += [machine.mem_read(address, 1)[0] for address in flag_addresses]
        destination.write(' '.join(f'{value & 0xffffffff:08x}' for value in row + result) + '\n')
        count += 1
        return result

    # Every bin edge, adjacent float, speed edge, flag combination and decay equality.
    scalars = [bits(float(i)) + delta for i in range(1, 12) for delta in (-1, 0, 1)]
    scalars += [bits(-2), 0, 0x80000000, 0x7fc12345, 0x7f800000, 0xff800000]
    for scalar in scalars:
        for speed in (bits(0), bits(10) - 1, bits(10), bits(10) + 1, bits(250)):
            for flags in (0, 0x200, 0x4000, 0x4200, 0x40000, 0x40200, 0x44000, 0x44200):
                for now in (bits(1) - 1, bits(1), bits(1) + 1):
                    execute([scalar, bits(1), bits(.1), bits(.2), bits(.3), 0, 0, 0, 0, 0,
                             speed, 0, now, flags, 9, 1])
    randomizer = random.Random(0xC0F11BB50)
    for _ in range(8000):
        state = [bits(randomizer.uniform(-2, 25)), bits(randomizer.uniform(0, 30)),
                 bits(.123), bits(.456), bits(.789), randomizer.choice([0, 2, 3, 7, 8, 0xffffffff])]
        state += [randomizer.randrange(2) for _ in range(4)]
        execute(state + [bits(randomizer.uniform(-500, 500)), bits(randomizer.uniform(-500, 500)),
                        bits(randomizer.choice([.1, 1, 8192, 1000000, 16777216])),
                        randomizer.getrandbits(20), randomizer.choice([9, 15]), randomizer.randrange(8) != 0])
    # Long ordered traces preserve output state, then alternate flags/owners/weapons.
    state = [bits(0), bits(0), bits(0), bits(0), bits(0), 0, 0, 0, 0, 0]
    for i in range(2000):
        phase = (i // 7) % 8
        state = execute(state + [bits(200 if phase % 2 else 0), bits(0), bits(i / 60),
                        (0x200, 0x200, 0x40200, 0x4200, 0, 0x4000, 0x44000, 0x200)[phase],
                        15 if i % 37 == 0 else 9, i % 29 != 0])
    # A genuine double+.1 counterexample prevents an all-float rewrite passing.
    precision_random = random.Random(1)
    def f32(value):
        return struct.unpack('<f', struct.pack('<f', value))[0]
    counterexample = next(value for value in
                         (f32(precision_random.uniform(0, 10)) for _ in range(100000))
                         if f32(value + .1) != f32(value + f32(.1)))
    execute([bits(3), bits(0), 0, 0, 0, 0, 0, 0, 0, 0,
             0, 0, bits(counterexample), 0x200, 9, 1])
    # Alternate two weapon states with original shared transitions, then with
    # separate transitions. The host API must support both without hidden state.
    for shared in (False, True):
        weapons = [[bits(0)] * 5 for _ in range(2)]
        transitions = [[0] * 5 for _ in range(2)]
        for i in range(200):
            player = i % 2
            transition_index = 0 if shared else player
            result = execute(weapons[player] + transitions[transition_index] +
                             [bits(200 if player == 0 else 0), 0, bits(i / 60),
                              0x40200 if player == 0 else 0x200, 9, 1])
            weapons[player] = result[:5]
            transitions[transition_index] = result[5:]
    required = (0x1011BBF0, 0x1011BC61, 0x1011BCAB, 0x1011BD21, 0x1011BD44,
                0x1011BD95, 0x1011BDAB, 0x1011BF4B, 0x1011BF6B, 0x1011BF9C, 0x1011BFAC)
    if not all(address in visited for address in required):
        raise ValueError('Incomplete original branch coverage')
    return dict(sampleSha256=SAMPLE_SHA256, functionVA='1011BB50', functionRVA='11BB50',
                functionSize=1132, functionSha256=code_sha, cases=count, checks=count * 10,
                visitedInstructions=len(visited), requiredInstructions=[f'{x:08x}' for x in required],
                deadlineDoubleRoundingCounterexample=counterexample,
                oracle='Unmodified original x86 instructions in Unicorn; no external calls permitted')
