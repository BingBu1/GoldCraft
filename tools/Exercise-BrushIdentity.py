"""Real ReHLDS/GameDLL brush identity encoding plus exact hw.dll publication.

Uses a fixture-only engine command, real AddToFullPack and production sideband
encoding with a controlled packet. No datagram is transmitted. The client probe
executes the hash-checked engine's entity-state publication with model/animation
adapters. This is not B networking, dynamic carving/rendering or FPS acceptance.
"""
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('brush_delivery', ROOT / 'tools/Exercise-MapDelivery.py')
delivery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(delivery)


class Run(delivery.Run):
    def __init__(self):
        super().__init__()
        self.report['scope'] = __doc__

    def identity(self, slot=0, mode=0, repeats=1):
        answer = self.command(f'gc_brush_identity {self.slot} {slot} {mode} {repeats}')
        match = re.search(r'GC_BRUSH (\{[^\r\n]+\})', answer)
        if not match:
            raise RuntimeError('Missing native brush identity: ' + answer)
        row = json.loads(match[1])
        self.report.setdefault('identities', []).append(dict(mode=mode,repeats=repeats,**row))
        if row['prefix'] != 1:
            raise AssertionError('Native datagram prefix was overwritten')
        return row

    def decode(self, row):
        wire = bytes.fromhex(row['wire'])
        if len(wire) < 26 or len(wire) != wire[1] + 2:
            raise ValueError('Brush identity message framing')
        epoch, revision, sequence, total, first = struct.unpack_from('<QQIHH', wire, 2)
        if epoch != self.peer.world or sequence != 731 or first or len(wire) != 26 + 10*total:
            raise ValueError('Brush identity header')
        entries = [struct.unpack_from('<HII', wire, 26+i*10) for i in range(total)]
        return revision, entries

    def run(self):
        self.start()
        original = self.identity()
        slot = original['slot']
        self.check('real door packs as native BSP and produces no unedited traffic',
                   original['packed'] == 1 and original['solid'] == 4 and not original['wire'])
        box = ([v-1 for v in original['low']], [v+1 for v in original['high']])
        self.cut(box, slot)
        row = self.identity(slot, repeats=65536)
        revision, entries = self.decode(row)
        self.check('edited native door carries exact epoch revision sequence serial and inline model',
                   entries == [(slot, original['serial'], original['model'])] and revision == self.physics()['revision'])
        payload = ROOT / 'analysis/world-carving/brush-server-payload.bin'
        payload.write_bytes(bytes.fromhex(row['wire']))
        probe = ROOT / 'build/native-x86/Release/goldcraft_brush_engine_tests.exe'
        engine = ROOT / 'sandbox/cs-client-b/Half-Life/hw.dll'
        result = subprocess.run([str(probe),str(engine),str(payload)],capture_output=True,text=True,timeout=30)
        self.report['clientPublication'] = dict(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr)
        self.check('actual client publication consumes real server bytes with no user-field mutation',
                   result.returncode == 0 and '"replayedServerBytes":true' in result.stdout)
        for mode, label in ((1,'ordinary client'),(2,'wrong capability version'),(6,'insufficient datagram space'),
                            (7,'client before full connection'),(8,'fake client')):
            self.check(label + ' gets no partial sideband', not self.identity(slot,mode)['wire'])
        for mode, label in ((3,'changed server serial'),(4,'plugin-overridden outgoing model'),(5,'plugin-overridden solidity')):
            _, records = self.decode(self.identity(slot,mode))
            self.check(label + ' cannot inherit old cuts', not records)
        self.check('temporary fixture changes restore the original identity', self.decode(self.identity(slot))[1] == entries)
        self.command('mc_map_mining_persist 1'); self.end(); self.restart()
        self.check('persist1 preserves identity across native rounds',self.decode(self.identity(slot))[1] == entries)
        self.command('mc_map_mining_persist 0'); self.end()
        self.check('restore0 removes identity traffic at round end',not self.identity(slot)['wire'])
        self.restart(); self.cut(box,slot)
        self.check('later cut reuses identity with a newer ledger revision',self.decode(self.identity(slot))[0] > revision)
        self.check('no map physics preparation or query failures',self.physics()['failures'] == 0)
        self.command('mc_map_mining_persist 1')
        epoch = self.peer.world
        self.command(f'kick #{self.peer.actors[self.slot]["userid"]}')
        self.peer.close(); self.peer = None
        self.command('changelevel cs_assault'); self.reconnect(previous_epoch=epoch)
        self.slot = int(re.search(r'slot=(\d+)',self.command('gc_headless_create'))[1])
        self.check('map reload clears identity traffic even with persist1',not self.identity()['wire'])
        base = delivery.entity.carving.rounds.mining.base
        self.report['runtimeErrors'] = {p.name:p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            for p in (base.AMXX/'logs').glob('error_*.log') if p.stat().st_size>self.error_offsets.get(p,0)}
        self.check('no AMXX runtime errors',not self.report['runtimeErrors'])
        self.report['passed'] = True


if __name__ == '__main__':
    run = Run()
    try:
        run.run()
    except Exception as error:
        run.report.update(error=str(error),passed=False)
        raise
    finally:
        run.close(ROOT/f'analysis/world-carving/brush-native-{time.time_ns()}.json')
