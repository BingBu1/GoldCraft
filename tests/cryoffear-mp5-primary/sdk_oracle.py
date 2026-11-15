# SPDX-License-Identifier: GPL-3.0-or-later
"""Original MP5 primary observed at the existing SDK fixture's ABI edges."""
import importlib.util
from pathlib import Path
import random

spec = importlib.util.spec_from_file_location('mp5_sdk_original', Path(__file__).with_name('oracle.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


class Original(shared.MP5):
    def event(self, kind, *values):
        # SDK animation/direction hooks are checked separately; use its exact
        # effect observation format, without the pure fixture's extra probes.
        if kind not in (32, 33, 34, 35, 40):
            self.events += [kind, len(values), *values]


def generate(sample, cases, expected):
    original = Original(sample)
    rng = random.Random(527901)
    for i in range(320):
        now = shared.bits(rng.choice((.1, 1.1, 13.37, 4096.125, 8192.3)))
        row = [now, rng.choice((1, 2, 30, 75, 511, 10000)), 0, 0, rng.randrange(2),
               0, 0, rng.randrange(2), rng.randrange(2), rng.choice((0, 0x4000)), shared.bits(rng.random()*3), 0]
        branch = i % 10
        if branch == 0:
            row[2] = shared.bits(shared.number(now) + 1)
        elif branch == 1:
            row[3] = 1
        elif branch == 3:
            row[5] = 3
        elif branch == 4:
            row[1], row[6] = 0, 1
        elif branch == 5:
            row[1] = 0
        state = [row[0], row[1], row[2], row[3], 0, row[5], row[6], row[7], row[8], row[9],
                 row[10], 0, shared.bits(37), 2, 2, 0, 17, 0]
        result = original.run(state)
        # The public SDK fixture separately tests the private empty deadline.
        result[4] = 0
        cases.write(' '.join(f'{value:08x}' for value in row) + '\n')
        expected.write(' '.join(f'{value:08x}' for value in result[:9] + result[11:]) + '\n')
    return dict(originalFunction='1005B400', sampleSha256=shared.SHA, cases=320,
                recoilMutatesPunch=True, boundary='Complete original primary through controlled SDK-compatible effect edges')


def generate_transitions(sample, cases, expected):
    module=shared.load('mp5_sdk_transition_edges',Path(__file__).parents[1]/'cryoffear-tmp-weapon/transition_oracle.py')
    class MP5Transitions(module.Transitions):
        original_id=27
        sequence_count=20
        enter=0x1005A820
        exit=0x1005A550
        functions={0:0x1005AB10,1:0x1005C240,2:0x1005C240,3:0x1005A510}
    result=module.generate(sample,cases,expected,MP5Transitions)
    result['functions']=['1005AB10','1005A820','1005A550','1005C240','1005A510']
    return result
