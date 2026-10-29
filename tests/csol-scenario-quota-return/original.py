"""Bounded 1139AC50 original ReturnPointQuota versus the new production method.

Mode-service and hash-container lookup are explicit substitutes. Signed key
construction, missing-node branch and dword decrement execute original bytes.
This does not exercise callers, death, priority gates, the entity pool or Free.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
sys.dont_write_bytecode = True
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EIP, UC_X86_REG_ESP

helper = Path(__file__).parents[1]/'csol-scenario/spawn_points_original.py'
spec = importlib.util.spec_from_file_location('frozen_quota_oracle_support', helper)
support = importlib.util.module_from_spec(spec); spec.loader.exec_module(support)
GETTER = support.STOP + 0x100
MODE_QUERY = support.STOP + 0x110
RULES = support.OBJECT + 0x3000
VTABLE = support.OBJECT + 0x4000

def key(team, point):
    # Independent integer interpretation for the substituted hash registry key.
    t = int.from_bytes((team & 255).to_bytes(1, 'little'), 'little', signed=True)
    p = int.from_bytes((point & 65535).to_bytes(2, 'little'), 'little', signed=True)
    return support.signed((t << 16) | p)

class OriginalReturn(support.Original):
    def __init__(self, path, metadata):
        super().__init__(path, metadata)
        self.u.mem_map(0x12278000, 0x1000)
        self.put(0x12278834, GETTER); self.put(RULES, VTABLE); self.put(VTABLE + 0x190, MODE_QUERY)
        self.u.hook_add(UC_HOOK_CODE, self.mode_service, begin=GETTER, end=MODE_QUERY)
        self.mode = 15

    def mode_service(self, u, address, size, unused):
        assert address in (GETTER, MODE_QUERY)
        if address == MODE_QUERY: assert u.reg_read(UC_X86_REG_ECX) == RULES
        esp = u.reg_read(UC_X86_REG_ESP)
        u.reg_write(UC_X86_REG_EAX, RULES if address == GETTER else self.mode)
        u.reg_write(UC_X86_REG_ESP, esp + 4); u.reg_write(UC_X86_REG_EIP, self.get(esp))

    def case(self, registered_team, registered_id, team, point, maximum, count, exists, repetitions, mode=15):
        self.mode = mode; self.present_key = key(registered_team, registered_id) if exists else None
        self.put(support.NODE + 0x28, maximum); self.put(support.NODE + 0x2c, count)
        for unused in range(repetitions):
            self.setup(team, point); self.stop_at = support.STOP
            self.u.emu_start(0x1139AC50, 0, count=150)
            assert self.stopped_at == support.STOP and self.u.reg_read(UC_X86_REG_EIP) == support.STOP
            assert self.u.reg_read(UC_X86_REG_ESP) == support.STACK + 12
            assert self.lookups == ([] if mode == 0x30 else [key(team, point)])
            assert self.get(support.NODE + 0x28) == maximum & 0xffffffff
        return [self.get(support.NODE + 0x2c), exists]

def main():
    parser = argparse.ArgumentParser()
    for name in ('mp', 'metadata', 'exe', 'output'): parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args(); original = OriginalReturn(args.mp, args.metadata)
    pairs = [(1, 7, 1, 7), (257, 65543, 1, 7), (1, -1, 127, -1), (-128, 1, 128, 65537),
        (0, 32768, 127, 32768), (255, 32767, -1, 32767), (1, 7, 2, 7), (1, 7, 1, 8)]
    counts = [0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff]
    cases = [(*pair, maximum, count, exists, (index % 3) + 1)
        for index, pair in enumerate(pairs) for maximum in (0, 2, -1) for count in counts for exists in (0, 1)]
    expected = [original.case(*case) for case in cases]
    commands = [' '.join(map(str, case)) for case in cases]
    result = subprocess.run([str(args.exe.resolve()), '--oracle'], input='\n'.join(commands)+'\n',
        text=True, capture_output=True, timeout=30)
    actual = [list(map(int, line.split())) for line in result.stdout.splitlines()]
    failures = [{'case': case, 'original': want, 'production': got}
        for case, want, got in zip(cases, expected, actual) if want != got]
    controls = [original.case(1, 7, 1, 7, 0, count, 1, 2, mode=0x30) for count in (0, 8)]
    assert controls == [[0, 1], [8, 1]]
    complete = result.returncode == 0 and len(actual) == len(expected)
    output = {'scope': 'mode15 ReturnPointQuota only; original x86 against production data-layer method',
        'cases': len(cases), 'complete': complete, 'returnCode': result.returncode, 'stderr': result.stderr, 'failures': failures,
        'originalCode': {'entry': '0x1139ac50', 'endExclusive': '0x1139ac9b',
            'sha256': hashlib.sha256(original.original(0x1139AC50, 0x4b)).hexdigest(), 'instructionLimitPerCall': 150,
            'stop': 'explicit code hook with EIP and ret8 stack verification'},
        'substitutes': ['mode service returns15', 'hash lookup with independent signed8/signed16 registry key'],
        'originalMode48Controls': controls, 'productionModeGateTested': False,
        'inputs': [support.evidence(path) for path in [args.mp, args.metadata, args.exe, Path(__file__), helper]],
        'nativeDllLoaded': False, 'deathOrEvictionCallerTested': False, 'liveGameplay': False}
    args.output.write_text(json.dumps(output, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'cases': len(cases), 'complete': complete, 'failures': len(failures), 'originalMode48Controls': 2}))
    return 0 if complete and not failures else 1

if __name__ == '__main__': raise SystemExit(main())
