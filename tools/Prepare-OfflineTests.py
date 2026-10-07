"""Prepare isolated vanilla GameTests; no live bridge credentials or game inputs."""
from pathlib import Path
import argparse
import json
import struct
import zlib

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'sandbox/mc-offline-tests'


def bsp_fixture():
    # MC coordinates; all support is BSP geometry, never Minecraft blocks.
    models = [
        [(0, 63, 0, 64, 64.25, 32), (8, 64.25, 2, 9, 69, 7),
         (24, 64.25, 10, 26, 64.625, 16), (26, 64.25, 10, 34, 65, 16),
         (40, 64.25, 19, 56, 70, 20), (40, 64.25, 24, 56, 70, 25),
         (40, 64.25, 19, 41, 70, 25), (55, 64.25, 19, 56, 70, 25)],
        [(48, 64.25, 20, 49, 69, 24)],
    ]
    planes, nodes, roots, bounds = [], [], [], []
    for boxes in models:
        outside = -2  # leaf 1: empty
        transformed = [(x0*32, -z1*32, (y0-64)*32, x1*32, -z0*32, (y1-64)*32)
                       for x0,y0,z0,x1,y1,z1 in boxes]
        bounds.append(tuple(min(b[i] for b in transformed) for i in range(3)) +
                      tuple(max(b[i] for b in transformed) for i in range(3,6)))
        for box in reversed(transformed):
            inside = -1  # leaf 0: solid
            for axis, high in reversed([(axis, high) for axis in range(3) for high in (False, True)]):
                normal = [0.,0.,0.]
                normal[axis] = 1.
                plane = len(planes)
                planes.append(struct.pack('<4fi', *normal, box[axis + (3 if high else 0)], axis))
                front, back = (outside, inside) if high else (inside, outside)
                index = len(nodes)
                nodes.append(struct.pack('<ihh6h2H', plane, front, back, *([0]*8)))
                inside = index
            outside = inside
        roots.append(outside)
    lumps = [b''] * 15
    lumps[1] = b''.join(planes)
    lumps[5] = b''.join(nodes)
    lumps[9] = struct.pack('<ihh', 0, -1, -1)
    lumps[10] = struct.pack('<i24x', -2) + struct.pack('<i24x', -1)
    lumps[14] = b''.join(struct.pack('<9f7i', *bound, 0,0,0, root,-1,-1,-1,0,0,0)
                          for bound, root in zip(bounds, roots))
    offset = 124
    header = struct.pack('<i', 30)
    for lump in lumps:
        header += struct.pack('<ii', offset if lump else 0, len(lump))
        offset += len(lump)
    return header + b''.join(lumps)


def main():
    global OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--loader', choices=('fabric','neoforge'), default='neoforge')
    args=parser.parse_args()
    OUT=ROOT / ('sandbox/neoforge-offline-tests' if args.loader=='neoforge' else 'sandbox/mc-offline-tests')
    OUT.mkdir(parents=True, exist_ok=True)
    fixture = OUT / 'navigation-fixture.bsp'
    fixture.write_bytes(bsp_fixture())
    settings = {'biome':'minecraft:plains','features':False,'lakes':False,
                'layers':[{'height':1,'block':'minecraft:air'}], 'structure_overrides':[]}
    pack = OUT / 'world/datapacks/goldcraft_offline_maps'
    dimension_dir = pack / 'data/goldcraft/dimension'
    dimension_dir.mkdir(parents=True, exist_ok=True)
    (pack/'pack.mcmeta').write_text(json.dumps({'pack':{'pack_format':48,'description':'Isolated BSP navigation tests'}}),encoding='utf-8')
    maps = [('nav_fixture', fixture), ('cs_assault', ROOT/'sandbox/cs-client-a/Half-Life/cstrike/maps/cs_assault.bsp')]
    manifest = {}
    generation = {'type':'minecraft:overworld','generator':{'type':'minecraft:flat','settings':settings}}
    preset_dimensions = {'minecraft:overworld': generation}
    for name, path in maps:
        crc = zlib.crc32(path.read_bytes())
        identity = f'{name}_{crc:08x}'
        (dimension_dir/f'{identity}.json').write_text(json.dumps(generation),encoding='utf-8')
        preset_dimensions['goldcraft:'+identity] = generation
        manifest[name] = {'path':str(path), 'dimension':'goldcraft:'+identity, 'crc':crc}
    # 1.21 TestServer constructs dimensions from WorldPresets.FLAT, ignoring the
    # normal world's dimension registry. Override that preset in this test pack only.
    preset = pack/'data/minecraft/worldgen/world_preset/flat.json'
    preset.parent.mkdir(parents=True, exist_ok=True)
    preset.write_text(json.dumps({'dimensions':preset_dimensions}),encoding='utf-8')
    # TestServer's initialization mode loads builtin mod resources reliably.
    # Keep the preset in the offline test mod as well, never the shipping mod.
    test_preset = ROOT/args.loader/'src/offlineTest/resources/data/minecraft/worldgen/world_preset/flat.json'
    test_preset.parent.mkdir(parents=True, exist_ok=True)
    test_preset.write_text(json.dumps({'dimensions':preset_dimensions},indent=2),encoding='utf-8')
    (OUT/'fixtures.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    (OUT/'eula.txt').write_text('eula=true\n',encoding='ascii')
    (OUT/'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nenable-rcon=false\nlevel-name=world\nlevel-type=minecraft\\:flat\ngenerate-structures=false\ndifficulty=normal\ngenerator-settings='+json.dumps(settings)+'\n',encoding='utf-8')
    print('Prepared isolated test maps:', ', '.join(manifest))


if __name__ == '__main__':
    main()
