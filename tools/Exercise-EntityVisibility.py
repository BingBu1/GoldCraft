"""Actual ReHLDS PVS and ReGameDLL entity packing after world edits.

Uses an isolated non-LAN dedicated server, real fake-client edicts and a
fixture-only cut producer. No graphical client, packet delivery, PAS/audio or
dynamic-brush acceptance is inferred. Restores temporary runtime files on exit.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import random
import re
import struct
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("entity_carving", ROOT / "tools/Exercise-WorldCarving.py")
carving = importlib.util.module_from_spec(spec)
spec.loader.exec_module(carving)


class Map(carving.Map):
    def __init__(self):
        super().__init__()
        data = (ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp").read_bytes()
        offset, size = struct.unpack_from('<II', data, 4 + 4 * 8)
        self.vis = data[offset:offset + size]
        self.count = self.models[0][13]
        self.rows = [self.row(i) for i in range(self.count + 1)]

    def row(self, leaf):
        at = self.leaves[leaf][1]
        if at < 0:
            return (1 << self.count) - 1
        out = bytearray()
        width = (self.count + 7) // 8
        while len(out) < width:
            value = self.vis[at]; at += 1
            if value:
                out.append(value)
            else:
                count = self.vis[at]; at += 1
                if not count:
                    raise ValueError('Zero PVS run')
                out.extend(bytes(min(count, width - len(out))))
        return int.from_bytes(out, 'little') & ((1 << self.count) - 1)

    def box_leaves(self, low, high):
        stack, result = [self.models[0][9]], set()
        while stack:
            at = stack.pop()
            if at < 0:
                result.add(-1 - at)
                continue
            branch = self.nodes[at]; plane = self.planes[branch[0]]
            minimum = maximum = -plane[3]
            for axis in range(3):
                a, b = plane[axis] * low[axis], plane[axis] * high[axis]
                minimum += min(a, b); maximum += max(a, b)
            if maximum >= 0: stack.append(branch[1])
            if minimum <= 0: stack.append(branch[2])
        return result

    def fat(self, eye):
        stack, bits = [self.models[0][9]], 0
        while stack:
            at = stack.pop()
            if at < 0:
                leaf = -1 - at
                if leaf: bits |= self.rows[leaf]
                continue
            branch = self.nodes[at]; plane = self.planes[branch[0]]
            distance = sum(a*b for a, b in zip(plane[:3], eye)) - plane[3]
            if distance >= -8: stack.append(branch[1])
            if distance <= 8: stack.append(branch[2])
        return bits

    def plan(self):
        samples = {}
        for record in self.leaves[1:self.count + 1]:
            point = [(record[2 + a] + record[5 + a]) / 2 for a in range(3)]
            touched = self.box_leaves([v-2 for v in point], [v+2 for v in point])
            if len(touched) == 1 and 0 not in touched:
                samples[next(iter(touched))] = point
        for candidate in self.candidates():
            contacts = self.box_leaves(candidate['low'], candidate['high']) - {0}
            contact_bits = sum(1 << (leaf - 1) for leaf in contacts)
            expansion = contact_bits
            for leaf in contacts: expansion |= self.rows[leaf]
            for eye in samples.values():
                original = self.fat(eye)
                if not original & contact_bits: continue
                for leaf, target in samples.items():
                    bit = 1 << (leaf - 1)
                    if not expansion & bit or original & bit: continue
                    body = self.box_leaves([target[a]-[17,17,37][a] for a in range(3)],
                                           [target[a]+[17,17,37][a] for a in range(3)]) - {0}
                    if any(original & (1 << (n-1)) for n in body): continue
                    hidden = next((p for n, p in samples.items() if not (original | expansion) & (1 << (n-1))), None)
                    if hidden:
                        cavity = None
                        for d in range(-70, 10, 2):
                            p = [c+n*d for c, n in zip(candidate['center'], candidate['normal'])]
                            if self.box_leaves([v-2 for v in p], [v+2 for v in p]) == {0}:
                                cavity = p; break
                        if cavity:
                            return dict(cut=(candidate['low'], candidate['high']), eye=eye,
                                        target=target, hidden=hidden, cavity=cavity, contacts=sorted(contacts),
                                        originalVisible=original.bit_count(), expandedVisible=(original | expansion).bit_count(),
                                        totalLeaves=self.count, sampledLeaves=len(samples))
        raise RuntimeError('No independent hidden-to-visible map fixture found')

    def sealed(self):
        rng = random.Random(9412)
        for _ in range(20000):
            point = [rng.uniform(self.models[0][a] + 64, self.models[0][a+3] - 64) for a in range(3)]
            if self.box_leaves([v-20 for v in point], [v+20 for v in point]) == {0}:
                return point
        raise RuntimeError('No sealed solid fixture found')


class Run(carving.Run):
    def __init__(self):
        super().__init__()
        self.report['scope'] = __doc__

    def visibility(self, eye, target, options=0, slot=0, repeats=1):
        values = ' '.join(f'{v:.6f}' for v in (*eye, *target))
        answer = self.command(f'gc_carve_visibility {self.slot} {slot} {values} {options} {repeats}')
        match = re.search(r'visible=(\d+) packet=(\d+) custom=(\d+) restored=(-?\d+) leafs=(\d+) head=(-?\d+) repeats=(\d+) seen=(\d+) us=([\d.]+)', answer)
        if not match: raise RuntimeError('Missing actual visibility result: ' + answer)
        row = dict(zip(('visible','packet','custom','restored','leafs','head','repeats','seen'), map(int,match.groups()[:8])))
        row['us'] = float(match[9])
        self.report.setdefault('visibility', []).append(dict(eye=eye,target=target,options=options,slot=slot,**row))
        return row

    def run(self):
        geometry = Map()
        plan = geometry.plan()
        self.report['mapPlan'] = plan
        self.start()
        self.check('production engine excludes map-delivery fixture command',
                   bool(re.search(r'\b0 Commands for \[gc_carve_delivery\]',
                                  self.command('cmdlist gc_carve_delivery'))))
        self.command('sv_gravity 0')
        eye, target = plan['eye'], plan['target']
        before = self.visibility(eye, target)
        self.check('original PVS hides the independently selected entity', before['visible'] == before['packet'] == 0)
        before = self.visibility(eye, plan['cavity'])
        self.check('entity inside original solid has no original leaf membership', before['leafs'] == 0 and before['visible'] == 0)
        other = int(re.search(r'slot=(\d+)',self.command('gc_headless_create extra'))[1])
        self.command('gc_headless_team 1')
        self.until(lambda: other in self.peer.actors, 'Ordinary opposing player lifecycle')
        row = self.visibility(eye,target,slot=other)
        self.check('ordinary opposing player is hidden by original PVS', row['visible'] == row['packet'] == 0)
        self.cut(plan['cut'])
        opened = self.visibility(eye, target, repeats=65536)
        self.check('edited PVS reaches actual ReGameDLL entity packing', opened['visible'] > 0 and opened['packet'] == 1 and opened['seen'] == 65536)
        self.check('custom empty PVS stays authoritative', opened['custom'] == 0)
        row = self.visibility(eye,target,slot=other)
        self.check('ordinary opposing player is packed through the new opening', row['visible'] > 0 and row['packet'] == 1)
        row = self.visibility(eye,target,16,slot=other)
        self.check('same-frame restore also invalidates recent player visibility', row['packet'] == 1 and row['restored'] == 0)
        self.cut(plan['cut'])
        # ReGameDLL's intentional one-second visibility grace is retained within
        # a revision. Changing target position waits it out for this negative.
        time.sleep(1.05)
        hidden = self.visibility(eye, plan['hidden'])
        self.check('expansion does not expose an unrelated map region', hidden['visible'] == hidden['packet'] == 0)
        cavity = self.visibility(eye, plan['cavity'])
        self.check('new cavity entity with no old leaves is packed', cavity['leafs'] == 0 and cavity['visible'] == cavity['packet'] == 1)
        for options, name in ((1,'NODRAW'), (4,'skip-local-owner'), (8,'group')):
            row = self.visibility(eye, target, options)
            self.check('native ' + name + ' filter remains effective', row['packet'] == 0)
        row = self.visibility(eye, plan['hidden'], 2)
        self.check('proxy observer retains native all-visible semantics', row['visible'] == row['packet'] == 1)
        self.visibility(eye, target)
        self.command('mc_map_mining_persist 1')
        self.end(); self.restart()
        self.check('persist1 retains entity visibility across a real round', self.visibility(eye,target)['packet'] == 1)
        self.command('mc_map_mining_persist 0')
        self.end()
        row = self.visibility(eye,target)
        self.check('persist0 restores original entity visibility', row['visible'] == row['packet'] == 0)
        self.restart()
        self.cut(plan['cut'])
        row = self.visibility(eye,target,16)
        self.check('same-frame restore invalidates recent-PVS cache without observer movement', row['packet'] == 1 and row['restored'] == 0)
        sealed = geometry.sealed()
        self.report['sealedCavity'] = sealed
        self.cut(([v-4 for v in sealed],[v+4 for v in sealed]))
        row = self.visibility(sealed,sealed,repeats=65536)
        self.check('sealed cavity eye and entity use virtual visibility', row['leafs'] == 0 and row['visible'] == row['packet'] == 1 and row['seen'] == 65536)
        time.sleep(1.05)
        row = self.visibility(sealed,target)
        self.check('sealed cavity does not leak original map entities', row['visible'] == row['packet'] == 0)
        self.check('no physics preparation or query failure', self.physics()['failures'] == 0)
        epoch = self.peer.world
        self.command('mc_map_mining_persist 1')
        self.command(f'kick #{self.peer.actors[other]["userid"]}')
        self.command(f'kick #{self.peer.actors[self.slot]["userid"]}')
        self.peer.close(); self.peer=None
        self.command('changelevel cs_assault'); self.reconnect(previous_epoch=epoch)
        self.slot = int(re.search(r'slot=(\d+)',self.command('gc_headless_create'))[1])
        self.check('map reload removes virtual visibility', self.visibility(sealed,sealed)['visible'] == 0)
        self.check('map reload restores original entity PVS', self.visibility(eye,target)['visible'] == 0)
        self.report['runtimeErrors'] = {p.name:p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            for p in (carving.rounds.mining.base.AMXX/'logs').glob('error_*.log') if p.stat().st_size>self.error_offsets.get(p,0)}
        self.check('no AMXX runtime errors',not self.report['runtimeErrors'])
        self.report['passed'] = True


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--locate-only', action='store_true')
    args=parser.parse_args()
    if args.locate_only:
        print(json.dumps(Map().plan(),indent=2)); sys.exit(0)
    run=Run()
    try:
        run.run()
    except Exception as error:
        run.report.update(error=str(error),passed=False)
        raise
    finally:
        run.close(ROOT/f'analysis/world-carving/entity-native-{time.time_ns()}.json')
