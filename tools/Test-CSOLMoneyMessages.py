"""Exercise native AddAccount serialization and the matched CS x86 Money reader.

The SDK function and reward call expressions are extracted unchanged into a
small executable; only engine message sinks and unrelated player fields are
stubbed. The client reader executes its original machine code in Unicorn.
No game process, deployment, or UI is used. Requires an initialized x86 Clang
environment plus capstone/unicorn in the selected Python interpreter.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parent.parent
CLIENT_SHA = 'b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2'
CONSUMERS = ('scenario_monster_rewards.cpp', 'scenario_death_owner.cpp', 'scenario_wall_rewards.cpp')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def native_fixture(output, clang):
    source = ROOT / 'external/ReGameDLL_CS/regamedll/dlls/player.cpp'
    text = source.read_text(encoding='utf-8-sig')
    begin = text.index('void EXT_FUNC CBasePlayer::__API_HOOK(AddAccount)')
    end = text.index('\n#else', begin)
    function = text[begin:end]
    calls, inputs = [], [source]
    for i, name in enumerate(CONSUMERS):
        path = ROOT / 'native/csol/server' / name
        inputs.append(path)
        matches = re.findall(r'\b(?:attacker|entity)->AddAccount\(money, RT_NONE, (?:true|false)\);', path.read_text())
        assert len(matches) == 1, name
        calls.append(f'void reward{i}(CBasePlayer* attacker, int money){{auto* entity=attacker; {matches[0]}}}')
    fixture = r'''
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>
enum RewardType {RT_NONE, RT_INTO_GAME, RT_PLAYER_JOIN, RT_PLAYER_RESET, RT_PLAYER_SPEC_JOIN};
struct entvars_t {};
struct {float value=16000;} maxmoney;
using std::clamp;
struct CBasePlayer {int m_iAccount=0; entvars_t* pev=nullptr; void AddAccount(int,RewardType,bool);};
std::vector<std::vector<unsigned char>> messages;
void Begin(int,int,const float*,entvars_t*){messages.emplace_back();}
void Long(int value){for(int i=0;i<4;++i)messages.back().push_back(static_cast<std::uint32_t>(value)>>(8*i));}
void Byte(int value){messages.back().push_back(value);}
void End(){}
#define EXT_FUNC
#define __API_HOOK(name) name
#define MSG_ONE 1
#define gmsgMoney 42
#define MESSAGE_BEGIN Begin
#define WRITE_LONG Long
#define WRITE_BYTE Byte
#define MESSAGE_END End
'''
    fixture += function + '\n' + '\n'.join(calls)
    fixture += r'''
void Emit(const char* name, int before, int amount, int maximum, int track, int type, int repeats, int consumer=-1){
    CBasePlayer player;player.m_iAccount=before;maxmoney.value=static_cast<float>(maximum);messages.clear();
    for(int i=0;i<repeats;++i){
        if(consumer==0)reward0(&player,amount);
        else if(consumer==1)reward1(&player,amount);
        else if(consumer==2)reward2(&player,amount);
        else player.AddAccount(amount,static_cast<RewardType>(type),track!=0);
    }
    std::printf("{\"name\":\"%s\",\"before\":%d,\"after\":%d,\"packets\":[",name,before,player.m_iAccount);
    for(std::size_t i=0;i<messages.size();++i){if(i)std::printf(",");std::printf("\"");for(auto b:messages[i])std::printf("%02x",unsigned(b));std::printf("\"");}
    std::printf("]}\n");
}
int main(){
    Emit("hit",800,250,16000,1,RT_NONE,1,0);
    Emit("kill",800,500,16000,1,RT_NONE,1,1);
    Emit("wall-three-thresholds",800,100,16000,1,RT_NONE,3,2);
    Emit("partial-cap",15900,500,16000,1,RT_NONE,1,0);
    Emit("at-cap",16000,500,16000,1,RT_NONE,1,1);
    Emit("zero",800,0,16000,1,RT_NONE,1,0);
    Emit("negative-clamped",100,-300,16000,1,RT_NONE,1,1);
    Emit("signed32-balance",60000,1000,1000000,1,RT_NONE,1,0);
    Emit("silent-sync",800,200,16000,0,RT_NONE,1);
    Emit("join-no-message",123,800,16000,0,RT_INTO_GAME,1);
    Emit("reset-no-delta",123,800,16000,0,RT_PLAYER_RESET,1);
}
'''
    generated = output / 'native-account-fixture.cpp'
    generated.write_text(fixture, encoding='utf-8')
    obj, exe = output / 'native-account-fixture.obj', output / 'native-account-fixture.exe'
    commands = [
        [clang, '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/O2',
         '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '/c', str(generated), '/Fo'+str(obj)],
        [clang, '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT', str(obj), '/Fe'+str(exe),
         '/link', '/OPT:REF', '/OPT:LLDLTO=3', '/threads:1'],
    ]
    for i, command in enumerate(commands):
        result = subprocess.run(command, capture_output=True, text=True)
        (output / f'native-build-{i}.log').write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'Native fixture build failed: native-build-{i}.log')
    result = subprocess.run([str(exe)], capture_output=True, text=True, check=True)
    (output / 'native-packets.jsonl').write_text(result.stdout)
    (output / 'native-build-commands.json').write_text(json.dumps(commands, indent=2))
    return [json.loads(line) for line in result.stdout.splitlines()], inputs


def reader(binary, output):
    data = binary.read_bytes()
    assert hashlib.sha256(data).hexdigest() == CLIENT_SHA
    pe = struct.unpack_from('<I', data, 60)[0]
    count, optional = struct.unpack_from('<H', data, pe+6)[0], struct.unpack_from('<H', data, pe+20)[0]
    base, size = struct.unpack_from('<I', data, pe+52)[0], struct.unpack_from('<I', data, pe+80)[0]
    assert base == 0x10000000
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (size+4095)&~4095)
    for i in range(count):
        _, rva, length, offset = struct.unpack_from('<IIII', data, pe+24+optional+i*40+8)
        if length:
            machine.mem_write(base+rva, data[offset:offset+length])
    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    for name, start, length in [('money-register', 0x44f10, 0x13), ('money-reader', 0x46530, 0x7c),
                                ('money-draw', 0x43690, 0x3c6)]:
        lines = [f'{x.address:08x} {x.mnemonic} {x.op_str}' for x in
                 disassembler.disasm(bytes(machine.mem_read(base+start, length)), base+start)]
        (output / (name+'.asm')).write_text('\n'.join(lines)+'\n')
    scratch = 0x20000000
    machine.mem_map(scratch, 0x20000)
    sp, payload, callback, done = scratch+0x10000, scratch+0x11000, scratch+0x12000, scratch+0x12010
    # Only the VGUI account refresh callback is inert; all parser code runs.
    put = lambda address, value: machine.mem_write(address, struct.pack('<I', value & 0xffffffff))
    put(base+0x1101d0, scratch)
    put(scratch, scratch+0x100)
    put(scratch+0x104, callback)
    machine.mem_write(callback, b'\xc3')

    def receive(packet):
        machine.mem_write(payload, packet)
        machine.mem_write(sp, struct.pack('<IIII', done, 0, len(packet), payload))
        machine.reg_write(UC_X86_REG_ESP, sp)
        machine.emu_start(base+0x46530, done, count=1000)
        assert machine.reg_read(UC_X86_REG_ESP) == sp+4
        assert machine.reg_read(UC_X86_REG_EAX) == 1
        get = lambda rva: struct.unpack('<i', machine.mem_read(base+rva, 4))[0]
        return dict(balance=get(0x11bafc), delta=get(0x11bb0c), track=get(0x11bb18))
    return receive


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Evidence output must remain in workspace/build')
    output.mkdir(parents=True, exist_ok=True)
    rows, inputs = native_fixture(output, args.clang)
    receive = reader(args.client, output)
    expected = {
        'hit': (1050, [250], [1]), 'kill': (1300, [500], [1]),
        'wall-three-thresholds': (1100, [100, 100, 100], [1, 1, 1]),
        'partial-cap': (16000, [100], [1]), 'at-cap': (16000, [0], [0]),
        'zero': (800, [0], [0]), 'negative-clamped': (0, [-100], [1]),
        'signed32-balance': (61000, [1000], [1]), 'silent-sync': (1000, [200], [0]),
        'join-no-message': (800, [], []), 'reset-no-delta': (800, [677], [0]),
    }
    for row in rows:
        receive(struct.pack('<iB', row['before'], 0))
        row['client'] = [receive(bytes.fromhex(packet)) for packet in row['packets']]
        balance, deltas, flags = expected[row['name']]
        assert row['after'] == balance, row
        assert [r['delta'] for r in row['client']] == deltas, row
        assert [r['track'] for r in row['client']] == flags, row
        assert all(len(bytes.fromhex(p)) == 5 for p in row['packets']), row
        if row['client']:
            assert row['client'][-1]['balance'] == balance, row
    report = dict(passed=True, cases=len(rows), messages=sum(len(x['packets']) for x in rows), rows=rows,
                  clientSha256=CLIENT_SHA, clientReaderRva='0x46530', nativeFunctionExtractedUnchanged=True,
                  engineSinksStubbed=True, realClientInstructions=True, liveGameplay=False,
                  inputs=[dict(path=str(p.relative_to(ROOT)), sha256=digest(p)) for p in inputs])
    (output/'results.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(dict(passed=True, cases=report['cases'], messages=report['messages'], liveGameplay=False)))


if __name__ == '__main__':
    main()
