"""Run actual original TMP Spawn and the shared world-entity lifecycle."""
import importlib.util
from pathlib import Path

path=Path(__file__).parents[1]/'cryoffear-p345-weapon/world_oracle.py'
spec=importlib.util.spec_from_file_location('tmp_original_world_shared',path)
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)


class World(shared.World):
    def call(self,address):
        super().call(0x1007FC10 if address==0x1005F510 else address)


def generate(sample,cases,expected):
    original=World(sample);count=0
    for now in (.1,13.37,8192.3):
        for health in (-2,-1,0,.5,30,42.9):
            for touch in (0,1):
                for ground,hidden,retire in ((0,0,0),(1,0,0),(1,1,0),(1,1,1),(0,1,2)):
                    row=[shared.bits(now),shared.bits(health),touch,ground,hidden,retire]
                    cases.write(' '.join(f'{word:08x}'for word in row)+'\n')
                    expected.write(' '.join(f'{word:08x}'for word in original.run(row))+'\n');count+=1
    return dict(sampleSha256=shared.shared.SHA,cases=count,
        functions=['1007FC10','1011D150','1011D220','1011E1C0','1011CF10','1011DFD0','1011F680'],
        boundary='Original complete Spawn/shared world instructions; controlled Precache/model/origin/size; no pickup acceptance')
