"""Verify edited PVS/PAS routing in the actual isolated ReHLDS engine.

The separately compiled engine fixture swaps/restores recipient buffers and
temporarily makes two existing fake clients eligible for native event/audio
routing. It verifies real serialized bytes and event queues, not network receipt,
graphical rendering, audible output or authentication of real players.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('delivery_visibility', ROOT / 'tools/Exercise-EntityVisibility.py')
entity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entity)


def expand_once(rows, source):
    result = source
    pending = source
    while pending:
        bit = pending & -pending
        pending ^= bit
        result |= rows[bit.bit_length()]
    return result


class Map(entity.Map):
    def delivery_plan(self):
        samples = {}
        for leaf, record in enumerate(self.leaves[1:self.count + 1], 1):
            point = [(record[2 + a] + record[5 + a]) / 2 for a in range(3)]
            if self.box_leaves([v-2 for v in point], [v+2 for v in point]) == {leaf}:
                samples[leaf] = point
        original_pas = [expand_once(self.rows, row) for row in self.rows]
        for candidate in self.candidates():
            contacts = self.box_leaves(candidate['low'], candidate['high']) - {0}
            contact_bits = sum(1 << (n - 1) for n in contacts)
            component = contact_bits
            for leaf in contacts: component |= self.rows[leaf]
            visual = [row | component if row & contact_bits else row for row in self.rows]
            audible = [expand_once(visual, row) for row in visual]
            def pair(before, after, exclude=None):
                for leaf, point in samples.items():
                    added = after[leaf] & ~before[leaf]
                    if exclude is not None: added &= ~exclude[leaf]
                    target = next((p for n, p in samples.items() if added & (1 << (n-1))), None)
                    hidden = next((p for n, p in samples.items() if not after[leaf] & (1 << (n-1))), None)
                    if target is not None and hidden is not None:
                        return dict(source=point, target=target, hidden=hidden,
                                    before=before[leaf].bit_count(), after=after[leaf].bit_count())
            v, a = pair(self.rows, visual), pair(original_pas, audible, visual)
            if v and a:
                return dict(cut=(candidate['low'], candidate['high']), visual=v, audio=a,
                            contacts=sorted(contacts), totalLeaves=self.count)
        raise RuntimeError('No independent PVS and PAS expansion case found')


class Run(entity.Run):
    def __init__(self):
        super().__init__()
        self.report['scope'] = __doc__

    def prepare(self):
        super().prepare()
        self.copy(ROOT / 'build/rehlds/Delivery/swds.dll', entity.carving.rounds.mining.base.GAME / 'swds.dll')
        # The production engine copied by the common setup is replaced before
        # startup. Record only the engine that the process will actually load.
        self.artifacts.pop('dist/rehlds/swds.dll')
        self.report['engineFixture'] = 'build/rehlds/Delivery/swds.dll'

    def delivery(self, source, target, mode=0, options=0, repeats=1):
        coords = ' '.join(f'{value:.6f}' for value in (*source, *target))
        answer = self.command(f'gc_carve_delivery {self.slot} {self.other} {coords} {mode} {options} {repeats}')
        match = re.search(r'GC_DELIVERY (\{[^\r\n]+\})', answer)
        if not match: raise RuntimeError('Missing actual engine delivery result: ' + answer)
        result = json.loads(match[1])
        self.report.setdefault('delivery', []).append(dict(source=source,target=target,mode=mode,options=options,**result))
        return result

    @staticmethod
    def empty(result):
        return not result['datagram'] and not result['reliable'] and result['events'] == 0

    @staticmethod
    def effect(result, source, reliable=False):
        # Actual svc_tempentity/TE_SPARKS and three GoldSrc fixed-point coords.
        expected = bytes((23, 9)) + struct.pack('<3h', *(int(v * 8) for v in source))
        return bytes.fromhex(result['reliable' if reliable else 'datagram']) == expected

    def run(self):
        geometry = Map()
        plan = geometry.delivery_plan()
        self.report['mapPlan'] = plan
        self.start()
        self.other = int(re.search(r'slot=(\d+)',self.command('gc_headless_create extra'))[1])
        self.until(lambda: self.other in self.peer.actors, 'Second ordinary fake client')
        self.command('sv_gravity 0')
        v, a = plan['visual'], plan['audio']
        row = self.delivery(v['source'], v['target'])
        self.check('original PVS suppresses serialized effect to selected region', row['visible'] == 0 and self.empty(row))
        self.check('original reliable PVS also suppresses selected region',self.empty(self.delivery(v['source'],v['target'],2)))
        for mode, label in ((1,'PAS effect'),(4,'native sound'),(5,'ReAPI targeted sound'),(6,'ReAPI multicast sound'),(7,'weapon event')):
            row = self.delivery(a['source'], a['target'], mode)
            self.check('original PAS suppresses ' + label, row['audible'] == 0 and self.empty(row))
        self.cut(plan['cut'])
        row = self.delivery(v['source'], v['target'], repeats=65536)
        self.check('edited PVS sends exact effect bytes', row['visible'] == 1 and self.effect(row,v['source']))
        sounds = []
        row = self.delivery(v['source'],v['target'],2)
        self.check('reliable PVS uses its own origin and correct recipient buffer',self.effect(row,v['source'],True))
        row = self.delivery(a['source'],a['target'],2)
        self.check('reliable PVS does not incorrectly use audible-only recipients',row['visible'] == 0 and row['audible'] == 1 and self.empty(row))
        for mode, label in ((1,'PAS effect'),(3,'reliable PAS effect'),
                            (4,'native sound'),(5,'ReAPI targeted sound'),(6,'ReAPI multicast sound'),(7,'weapon event')):
            row = self.delivery(a['source'],a['target'],mode,repeats=65536 if mode == 4 else 1)
            valid = row['audible'] == 1 and not self.empty(row)
            if mode <= 3: valid &= self.effect(row,a['source'],mode in (2,3))
            elif mode <= 6:
                valid &= bytes.fromhex(row['datagram'])[0] == 6 and not row['reliable']
                sounds.append(row['datagram'])
            else: valid &= row['events'] == 1 and not row['datagram'] and not row['reliable']
            self.check('edited PAS routes ' + label, valid)
        self.check('native and both ReAPI routes serialize identical sound', len(set(sounds)) == 1)
        for mode in (1,4,5,6,7):
            row = self.delivery(a['source'],a['hidden'],mode)
            self.check(f'edited PAS still suppresses unrelated region for mode {mode}', row['audible'] == 0 and self.empty(row))
        for mode in (4,6,7):
            pair = v if mode == 0 else a
            self.check(f'native group filter survives for mode {mode}',self.empty(self.delivery(pair['source'],pair['target'],mode,1)))
        for mode in (0,1):
            pair = v if mode == 0 else a
            self.check(f'entity-less message retains native group behavior for mode {mode}',
                       self.effect(self.delivery(pair['source'],pair['target'],mode,1),pair['source']))
        row = self.delivery(a['source'],a['hidden'],4,2)
        self.check('proxy receives native sound outside PAS',row['audible'] == 1 and bool(row['datagram']))
        row = self.delivery(a['source'],a['target'],4,4)
        self.check('native invoker exclusion remains effective',row['senderBytes'] == 0 and bool(row['datagram']))
        for mode in (0,4):
            pair = v if mode == 0 else a
            row = self.delivery(pair['source'],pair['target'],mode,8)
            self.check(f'full recipient buffer is not overwritten for mode {mode}',self.empty(row))
        for mode in (8,9,10):
            row = self.delivery(a['source'],a['hidden'],mode)
            self.check(f'native global or explicit no-PAS route remains reliable for mode {mode}',bool(row['reliable']) and not row['datagram'])
        self.command('mc_map_mining_persist 1'); self.end(); self.restart()
        self.check('persist1 retains message routing across round',self.effect(self.delivery(v['source'],v['target']),v['source']))
        self.command('mc_map_mining_persist 0'); self.end()
        self.check('round end immediately restores original PVS messages',self.empty(self.delivery(v['source'],v['target'])))
        self.check('round end immediately restores original PAS sounds',self.empty(self.delivery(a['source'],a['target'],4)))
        self.restart()
        sealed = geometry.sealed()
        self.report['sealedCavity'] = sealed
        self.cut(([p-4 for p in sealed],[p+4 for p in sealed]))
        for mode in (0,1,4,5,6,7):
            row = self.delivery(sealed,sealed,mode)
            self.check(f'sealed cavity routes internally without old leaf IDs for mode {mode}',
                       row['visible'] == row['audible'] == row['fatVisible'] == row['fatAudible'] == 1 and not self.empty(row))
        for mode in (0,1,4,5,7):
            row = self.delivery(sealed,a['target'],mode)
            self.check(f'sealed cavity never leaks into old map for mode {mode}',row['audible'] == 0 and self.empty(row))
            row = self.delivery(a['source'],sealed,mode)
            self.check(f'old map never leaks into sealed cavity for mode {mode}',row['audible'] == 0 and self.empty(row))
        self.check('no map preparation or query errors',self.physics()['failures'] == 0)
        epoch = self.peer.world
        self.command('mc_map_mining_persist 1')
        for slot in (self.other,self.slot): self.command(f'kick #{self.peer.actors[slot]["userid"]}')
        self.peer.close(); self.peer = None
        self.command('changelevel cs_assault'); self.reconnect(previous_epoch=epoch)
        self.slot = int(re.search(r'slot=(\d+)',self.command('gc_headless_create'))[1])
        self.other = int(re.search(r'slot=(\d+)',self.command('gc_headless_create extra'))[1])
        self.check('map reload clears edited PVS routing',self.empty(self.delivery(v['source'],v['target'])))
        self.check('map reload clears edited PAS routing',self.empty(self.delivery(a['source'],a['target'],4)))
        base = entity.carving.rounds.mining.base
        self.report['runtimeErrors'] = {p.name:p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            for p in (base.AMXX/'logs').glob('error_*.log') if p.stat().st_size>self.error_offsets.get(p,0)}
        self.check('no AMXX runtime errors',not self.report['runtimeErrors'])
        self.report['passed'] = True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--locate-only',action='store_true')
    args = parser.parse_args()
    if args.locate_only:
        print(json.dumps(Map().delivery_plan(),indent=2)); sys.exit(0)
    run = Run()
    try:
        run.run()
    except Exception as error:
        run.report.update(error=str(error),passed=False)
        raise
    finally:
        run.close(ROOT/f'analysis/world-carving/delivery-native-{time.time_ns()}.json')
