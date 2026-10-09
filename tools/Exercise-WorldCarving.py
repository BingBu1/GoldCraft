"""Real ReHLDS carved-world traces, player movement and round restoration.

Only the separately compiled fixture GameDLL can submit cuts while client/MC/
Renderer integration is pending. The normal mining capability remains disabled.
Uses one native fake client on an isolated non-LAN dedicated server. All temporary
runtime files are restored and the owned process is stopped; main/B stay off.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import math
import random
import re
import struct
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("carving_rounds", ROOT / "tools/Exercise-MapMiningRounds.py")
rounds = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rounds)


class Map:
    def __init__(self):
        data = (ROOT / "sandbox/cs-client-b/Half-Life/cstrike/maps/cs_assault.bsp").read_bytes()
        if struct.unpack_from("<I", data)[0] != 30 or zlib.crc32(data) != 0xf6725c06:
            raise ValueError("Unexpected isolated BSP fixture")
        lumps = [struct.unpack_from("<II", data, 4 + i * 8) for i in range(15)]
        def records(lump, fmt):
            offset, size = lumps[lump]
            return list(struct.iter_unpack(fmt, data[offset:offset + size]))
        self.planes = records(1, "<4fi")
        self.vertices = records(3, "<3f")
        self.nodes = records(5, "<ihh6h2H")
        self.clips = records(9, "<ihh")
        self.leaves = records(10, "<ii6h2H4B")
        self.faces = records(7, "<Hhihh4Bi")
        self.edges = records(12, "<HH")
        self.surfedges = [row[0] for row in records(13, "<i")]
        self.models = records(14, "<9f7i")

    def contents(self, hull, point):
        node = self.models[0][9 + hull]
        while node >= 0:
            branch = (self.nodes if hull == 0 else self.clips)[node]
            plane = self.planes[branch[0]]
            distance = sum(a*b for a, b in zip(plane[:3], point)) - plane[3]
            node = branch[1 if distance >= 0 else 2]
            if hull == 0 and node < 0:
                return self.leaves[-1-node][0]
        return node

    def candidates(self):
        for index, face in enumerate(self.faces[:self.models[0][15]]):
            plane = self.planes[face[0]]
            if abs(plane[2]) > .001:
                continue
            normal = [n * (-1 if face[1] else 1) for n in plane[:3]]
            axis = 0 if abs(normal[0]) > .99 else 1
            if abs(normal[axis]) != 1:
                continue
            tangent = 1-axis
            points = []
            for edge in self.surfedges[face[2]:face[2]+face[3]]:
                points.append(self.vertices[self.edges[abs(edge)][1 if edge < 0 else 0]])
            if not points or any(max(p[a] for p in points)-min(p[a] for p in points) < bound for a, bound in ((tangent,80),(2,120))):
                continue
            center = [sum(p[a] for p in points)/len(points) for a in range(3)]
            start = [c+n*96 for c,n in zip(center,normal)]
            end = [c-n*96 for c,n in zip(center,normal)]
            if any(self.contents(h,p) != -1 for h in (0,1,3) for p in (start,end)):
                continue
            occupied = [d for d in range(-96,97,4) if self.contents(1,[c+n*d for c,n in zip(center,normal)]) == -2]
            if not occupied or min(occupied) < -64 or max(occupied) > 32:
                continue
            low = [c-48 for c in center]
            high = [c+48 for c in center]
            low[axis] = min(center[axis]-normal[axis]*80,center[axis]+normal[axis]*16)
            high[axis] = max(center[axis]-normal[axis]*80,center[axis]+normal[axis]*16)
            low[tangent] = center[tangent]-32
            high[tangent] = center[tangent]+32
            first_high = high.copy();first_high[tangent] = center[tangent]
            second_low = low.copy();second_low[tangent] = center[tangent]
            yield dict(face=index, center=center, normal=normal, start=start, end=end, low=low, high=high,
                       first=(low, first_high), second=(second_low, high), axis=axis, tangent=tangent)

    def may_overlap_material(self, center, half):
        # Conservative box/BSP traversal: a false result proves the complete
        # box contains no point-solid. Crossing planes can only overestimate.
        stack = [self.models[0][9]]
        while stack:
            node = stack.pop()
            if node < 0:
                if self.leaves[-1-node][0] == -2:
                    return True
                continue
            branch = self.nodes[node]
            plane = self.planes[branch[0]]
            distance = sum(a*b for a,b in zip(center,plane[:3]))-plane[3]
            radius = sum(abs(a)*b for a,b in zip(plane[:3],half))
            if distance+radius >= 0:
                stack.append(branch[1])
            if distance-radius < 0:
                stack.append(branch[2])
        return False

    def clip_probe(self):
        rng = random.Random(4517)
        for _ in range(50000):
            end = [rng.uniform(self.models[0][a],self.models[0][a+3]) for a in range(3)]
            if self.contents(1,end)!=-2 or self.may_overlap_material(end,[16.01,16.01,36.01]):
                continue
            for axis in (0,1):
                for direction in (-1,1):
                    start = end.copy(); start[axis] += direction*96
                    center = [(a+b)/2 for a,b in zip(start,end)]
                    half = [16.01,16.01,36.01]; half[axis] += 48
                    if self.contents(1,start)==-1 and not self.may_overlap_material(center,half):
                        normal = [0,0,0]; normal[axis] = direction
                        return dict(start=start,end=end,center=center,normal=normal)
        raise RuntimeError('No provable clip-only native obstacle')


class Run(rounds.Run):
    def __init__(self):
        super().__init__()
        self.report["scope"] = __doc__

    def trace(self, start, end, hull=0, slot=-1):
        args = " ".join(f"{v:.6f}" for v in (*start,*end))
        answer = self.command(f"gc_carve_trace {slot} {hull} {args}")
        match = re.search(r"fraction=([-\d.]+) start=(\d+) all=(\d+) open=(\d+) water=(\d+) hit=(-?\d+) end=([^ ]+) normal=([^ ]+) contents=(-?\d+)", answer)
        if not match:
            raise RuntimeError("Native carve trace missing: " + answer)
        result = dict(fraction=float(match[1]), start=bool(int(match[2])), all=bool(int(match[3])),
                      open=bool(int(match[4])), water=bool(int(match[5])), hit=int(match[6]),
                      end=[float(v) for v in match[7].split(',')], normal=[float(v) for v in match[8].split(',')], contents=int(match[9]))
        self.report.setdefault("traces", []).append(dict(start=start,end=end,hull=hull,slot=slot,result=result))
        return result

    def cut(self, box, slot=0):
        text = " ".join(f"{v:.6f}" for row in box for v in row)
        answer = self.command(f"gc_carve_fixture {slot} {text}")
        if "committed=" not in answer and "unchanged=" not in answer:
            raise RuntimeError("Native carve commit failed: " + answer)
        return answer

    def physics(self):
        answer = self.command('gc_carve_status')
        match = re.search(r'revision=(\d+) targets=(\d+) traces=(\d+) points=(\d+) failures=(\d+) error=(.*)', answer)
        if not match:
            raise RuntimeError('Native physics status missing: ' + answer)
        result = dict(zip(('revision','targets','traces','points','failures'), map(int,match.groups()[:5])))
        result['error'] = match[6]
        self.report.setdefault('physics',[]).append(result)
        return result

    def move(self, start, normal, duck=False):
        yaw = math.degrees(math.atan2(-normal[1],-normal[0]))
        values = " ".join(f"{v:.6f}" for v in start)
        # Native round Spawn clears FL_FAKECLIENT. Reuse the fixture's existing
        # reset path, which restores that identity without adding Bot AI.
        reset = self.command(f'gc_headless_reset {values} 100 0')
        if 'alive=1' not in reset or f'slot={self.slot} ' not in reset:
            raise RuntimeError('Movement fixture player is not ready: ' + reset)
        answer = self.command(f"gc_carve_move {self.slot} {values} {yaw:.6f} 200 50 {int(duck)}")
        match = re.search(r"move=([^ ]+)",answer)
        if not match:
            raise RuntimeError("Native movement missing: " + answer)
        result = [float(v) for v in match[1].split(',')]
        self.report.setdefault('movements',[]).append(dict(start=start,result=result,duck=duck))
        return result

    def run(self):
        self.start()
        # Real PM_WALK with gravity disabled keeps the body at the test height.
        # MOVETYPE_FLY is normalized to WALK by native PM when no ladder is near.
        self.command('sv_gravity 0')
        self.check("physics API starts empty", self.physics()['targets']==0)
        candidates = list(Map().candidates())
        self.report['candidateCount'] = len(candidates)
        chosen = None
        for value in candidates[:8]:
            native = self.trace(value['start'],value['end'],1)
            if not native['start'] and not native['all'] and .1 < native['fraction'] < .9 and native['hit'] == 0:
                chosen = value
                break
        if chosen is None:
            raise RuntimeError("No real unobstructed wall candidate")
        self.report['wall'] = chosen
        start,end,normal,center = (chosen[key] for key in ('start','end','normal','center'))
        original = {h:self.trace(start,end,h) for h in (0,1,3)}
        self.check('real wall blocks native point/standing/crouching traces', all(.1<row['fraction']<.9 and not row['start'] for row in original.values()))
        edge_probes = []
        for offset in (-24,24):
            a,b = start.copy(),end.copy()
            a[chosen['tangent']] += offset; b[chosen['tangent']] += offset
            edge_probes.append((a,b,self.trace(a,b,1)))
        moved = self.move(start,normal)
        before_distance = sum((a-b)*n for a,b,n in zip(moved,center,normal))
        self.check('actual player PM movement reaches and stops at the unedited wall', math.dist(moved,original[1]['end'])<.1)
        self.cut(chosen['first'])
        self.check('single side does not fit the body across the seam',self.trace(start,end,1)['fraction']<1)
        self.cut(chosen['second'])
        revision = self.physics()['revision']
        duplicate = self.cut(chosen['second'])
        self.check('duplicate cut is idempotent', 'unchanged=' in duplicate and self.physics()['revision']==revision)
        opened = {h:self.trace(start,end,h) for h in (0,1,3)}
        self.check('adjacent cuts open real point/standing/crouching traces',all(row['fraction']==1 and not row['start'] and not row['all'] for row in opened.values()))
        edges = [(before,self.trace(a,b,1)) for a,b,before in edge_probes]
        self.check('remaining hole edges preserve original body contacts',all(abs(before['fraction']-after['fraction'])<1e-6 and before['normal']==after['normal'] for before,after in edges))
        # The optimized point path and direct TraceModel both need the overlay.
        self.check('direct world model trace agrees',self.trace(start,end,0,0)['fraction']==1)
        inside = [c-n*4 for c,n in zip(center,normal)]
        sample = self.trace(start,inside,0)
        self.check('actual engine point contents becomes empty in the hole',sample['contents']==-1 and sample['fraction']==1)
        moved = self.move(start,normal)
        after_distance = sum((a-b)*n for a,b,n in zip(moved,center,normal))
        self.check('actual RunPlayerMove crosses the carved wall',after_distance < -24)
        self.report['movementDistances'] = dict(before=before_distance,after=after_distance)
        before = self.state()['mapEditRevision']
        answer = self.command('gc_carve_fixture 0 0 0 0 0 0 0')
        self.check('failed candidate keeps committed collision and ledger intact','rejected=' in answer and self.state()['mapEditRevision']==before and self.trace(start,end,1)['fraction']==1)
        self.command('mc_map_mining_persist 1')
        self.end();self.restart()
        self.check('round retention preserves actual carved collision',self.trace(start,end,1)['fraction']==1)
        self.command('mc_map_mining_persist 0')
        self.end()
        restored = self.trace(start,end,1)
        self.check('round-end restoration returns original native collision',abs(restored['fraction']-original[1]['fraction'])<1e-6 and restored['hit']==original[1]['hit'])
        status = self.physics()
        self.check('round restore removes cache and preserves revision barrier',status['targets']==0 and status['revision']==self.state()['mapEditRevision'] and status['revision']>revision)
        self.restart()
        low,high = chosen['low'].copy(),chosen['high'].copy()
        low[2],high[2] = center[2]-22,center[2]+22
        self.cut((low,high))
        self.check('low opening passes crouching but blocks standing',self.trace(start,end,3)['fraction']==1 and self.trace(start,end,1)['fraction']<1)
        crouched = self.move(start,normal,duck=True)
        self.check('actual crouched RunPlayerMove fits low opening',sum((a-b)*n for a,b,n in zip(crouched,center,normal)) < -24)
        self.end(); self.restart()
        self.cut(chosen['first']);self.cut(chosen['second'])
        self.command('mc_map_mining_persist 1')
        self.peer.close();self.peer=None;self.reconnect()
        self.check('authority reconnect retains real collision edit',self.trace(start,end,1)['fraction']==1)
        status = self.physics()
        self.check('physics queries ran without error fallback',status['traces']>0 and status['points']>0 and status['failures']==0 and not status['error'])
        old_epoch=self.peer.world
        self.command(f'kick #{self.peer.actors[self.slot]["userid"]}')
        self.peer.close();self.peer=None
        self.command('changelevel cs_assault');self.reconnect(previous_epoch=old_epoch)
        self.command('sv_gravity 0')
        answer = self.command('gc_headless_create')
        self.slot = int(re.search(r'slot=(\d+)',answer)[1])
        restored = self.trace(start,end,1)
        self.check('map reload clears actual collision despite persist1',abs(restored['fraction']-original[1]['fraction'])<1e-6 and 'targets=0' in self.command('gc_carve_status'))
        clip = Map().clip_probe()
        self.report['clipOnlyProbe'] = clip
        start,end,normal,center = (clip[key] for key in ('start','end','normal','center'))
        clip_before = self.trace(start,end,1)
        self.check('verified material-free region has native clip-only blocking',self.trace(start,end,0)['fraction']==1 and 0<clip_before['fraction']<1 and not clip_before['start'])
        self.cut(([min(a,b)-48 for a,b in zip(start,end)], [max(a,b)+48 for a,b in zip(start,end)]))
        clip_after = self.trace(start,end,1)
        self.check('excavation retains clip-only collision inside its bounds',abs(clip_after['fraction']-clip_before['fraction'])<1e-6 and clip_after['normal']==clip_before['normal'])
        moved = self.move(start,normal)
        self.check('actual player reaches and stops at retained clip-only obstacle',math.dist(moved,clip_before['end'])<.1)
        status = self.physics()
        self.check('clip-only checks ran without physics fallback',status['traces']>0 and status['failures']==0 and not status['error'])
        self.check('production geometry permission still unavailable',self.peer.policy[3]==1)
        self.report['graphicalOrMinecraftAcceptance'] = False
        self.report['runtimeErrors'] = {p.name:p.read_bytes()[self.error_offsets.get(p,0):].decode('utf-8',errors='replace')
            for p in (rounds.mining.base.AMXX/'logs').glob('error_*.log') if p.stat().st_size>self.error_offsets.get(p,0)}
        self.check('no AMXX runtime errors',not self.report['runtimeErrors'])
        self.report['passed'] = True


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--locate-only',action='store_true')
    args=parser.parse_args()
    if args.locate_only:
        print(json.dumps(list(Map().candidates())[:8],indent=2))
        sys.exit(0)
    run=Run()
    try:
        run.run()
    except Exception as error:
        run.report['error']=str(error);run.report['passed']=False
        raise
    finally:
        run.close(output=ROOT/f'analysis/world-carving/hull-native-{rounds.mining.STAMP}.json')
