#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare the lantern and flare models and selection art for a GoldSrc sandbox."""
import argparse
import importlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import tempfile

from cryoffear_studio import Studio, inverse, multiply, transform
from csol_hud_resources import contract, dependencies

models = importlib.import_module('Generate-CryOfFearPlayerModel')
cards = importlib.import_module('Prepare-CryOfFearPresentation')
ROOT = Path(__file__).resolve().parents[1]

# The V model supplies the grip, the W model supplies third-person dimensions.
# The inventory definition names the same original picture for quick selection.
PINS = {
    'lantern': {
        'view': '57a6eb66da685953788e89826a9047512715afe43e388c10fbe6a6ef1ad639c7',
        'world': '60096eb47adcfa57c6da9792457cd45bab9e8b48d9a418e5ddc5d59eca24b572',
        'inventory': '82efe074e4a7d6434d58cf51985a2b0d1ff767a43ae6a55023560846ba436e4b',
        'picture': 'bfb0941254566b5dab28d9dc8d5f1923af541ad217e20471a49f753f18babfbf',
    },
    'flare': {
        'view': '480f2568b057a684b720e549033964ec08535b5d540263a8a43709e099dc9cc2',
        'world': '9a897d6587a19a7405202dfc09c616cdeb9efab31520ec52591e1dde62747639',
        'inventory': '757674181fcfe49b866f967039fbf8a2e1f70288c44cff3ab4f63afcfe1a3ba3',
        'picture': '3d425e4a1c738692bda655da4fb8b7904c4dea92d1eb65ce11298dc5cdacbec8',
    },
}
PLAYER = '939d4c34cb8b7339afc286feddd45c5d88f103e3a9ae69066ab372da276f3b0e'
TEMPLATE = 'dfb621c82fe160fdff5df1a1194807f22b6567ce416005c7842f1d60d8580668'
FLARE_AMMO = 'cf30b4cefbaa68bd2b7bb2cafe394907329ab633271bbe44d1ad2bccb4f3268e'


SOUNDS = {
    'item_get': 'e50b2bf740d0e9f673024995fe8885a1b3301cac8bd48f77b8f69a4ae06af663',
    'mobile/punch_swing': '76f82ffb2a7dcf1e2bcb6bb1430a7d3d4f429126d37dce9c05e8da4a51988551',
    'flare/flare_burn': '8949b464d21c5f7fed081eddd28c46efcb6e7cddc76bcd4e7499ed1850563da1',
    'flare/flare_ignite': 'cc91bae2ef3b90663e8ae3cac99506e27c097aefb803d8c8672c0f5cd8cf1f57',
    'flare/flare_movement1': '2e6afe603195a7673eea01959228d707639133ac82e7a40843d618725cd83a06',
    'flare/flare_movement2': 'c828d882a14060eddfbec4acf72d60f66da307f52ff85849484bf1ab43e634b6',
    'flare/flare_movement3': '7af3ad20b9b44968f11af7ac49afb9dbf89be8341ce9fe3a28e9db670e870b94',
    'flare/flare_strike1': '75694e8d375b4977e4d36c09fbef5ef07b94abc1d79baf15273e23189a4df2a9',
    'flare/flare_strike2': '80171ad98612a6f73d77e7b0044b0bc7ab3741077006f5489912291253d18ca7',
}
PARTICLES = {
    'signal': 'e94c8a0325c47485fecd3450e65d468fa7a1900de5c02b96017dc18dcecd8ecc',
    'blacksmoke': 'f6152e5d03fbe95c21aa86acaeab5d04ce8b347848870d0c0356e8b25d7e6abf',
    'flare_sparks': '9535f593ce24149f4c03c8a356f508ace2ca73c25938320c109a1d9e7b0654b2',
    'flare_glow': 'dce09caf37611075990bb39963ae185efec32c8685d28ac8423ae5a7f78c1b0e',
    'flare_extinguish': '1fa45394d1ac26d9b95e58cae8e833dbf2ca2395d64b71da793ff71ed5442dcc',
    'flare_sparks_short': '49fa9957f3d39f109012c12a4672c06e03e2e93c0b791051bdca5ba231d31ad4',
}
SPRITES = {
    'fire': 'b8964a44a414128faed87e4e8944879e0f7fc3eb30cfffdd7e283bdc1af6f110',
    'blacksmoke': '5c402ddcf2fde903e334441302b5dc1d99b7134bec874c5d0d5c2fa173c0a44d',
    'yelflare2': '4890f0e4eaf83f064892a05532fec0c298be94b69327f131cf0f0e98cb44a21a',
    'muzzleflash': '382fe00d87c7bf65549c1c6995e095f9389461dd7608eda4ba8864b35f93956d',
}


def effect_files(original, read, files):
    for sound, expected in SOUNDS.items():
        files[f'sound/guest/cof/{sound}.wav'] = read(original/f'sound/weapons/{sound}.wav', expected)
    referenced = set()
    for name, expected in PARTICLES.items():
        script = read(original/f'aurora/{name}.aur', expected)

        def relocate(match):
            sprite = match[2].decode('ascii')
            referenced.add(sprite)
            if sprite not in SPRITES:
                raise ValueError('Unknown Aurora sprite ' + sprite)
            return match[1] + b'sprites/guest/cof/' + match[2] + b'.spr"'

        data, count = re.subn(rb'("sprite"\s+")sprites/([a-z0-9_]+)\.spr"', relocate, script)
        if not count:
            raise ValueError('No sprite in ' + name)
        files[f'aurora/guest/cof/{name}.aur'] = data
    if referenced != SPRITES.keys():
        raise ValueError('Flare particle sprite closure changed')
    for sprite, expected in SPRITES.items():
        files[f'sprites/guest/cof/{sprite}.spr'] = read(original/f'sprites/{sprite}.spr', expected)


def mean(points):
    return tuple(sum(p[i] for p in points) / len(points) for i in range(3))


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def unit(v):
    length = math.hypot(*v)
    if not math.isfinite(length) or length < 1e-6:
        raise ValueError('Degenerate equipment landmark')
    return tuple(x / length for x in v)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def frame(origin, forward, up):
    x = unit(forward)
    y = unit(cross(up, x))
    z = cross(x, y)
    return [[x[i], y[i], z[i], origin[i]] for i in range(3)]


def pos(matrix):
    return tuple(row[3] for row in matrix)


def hand_frame(model, sequence, names):
    hand, index, thumb, finger_tip, thumb_tip = indices = [model.bone_index(n) for n in names]
    pose = model.sample(sequence, 0, 0, bone_scope=indices)
    grip = mean([pos(pose[finger_tip]), pos(pose[thumb_tip])])
    return pose[hand], frame(grip, subtract(pos(pose[index]), pos(pose[hand])),
                             subtract(pos(pose[thumb]), pos(pose[hand])))


def vertices(model, pose, part, bone=None):
    mesh = next(m for group in model.bodyparts for m in group['models'] if m['name'] == part)
    allowed = model.bone_index(bone) if bone else None
    return [transform(pose[b], v) for b, v in zip(mesh['verticesBones'], mesh['vertices'])
            if allowed is None or b == allowed]


def model_for_hand(kind, view, world, player, template, player_path):
    lantern = kind == 'lantern'
    side = 'R' if lantern else 'L'
    hand = 'Bip01 ' + side + ' Hand'
    action = 'ref_aim_knife' if lantern else 'ref_aim_grenade'
    seq = next(i for i in range(player.i(164)) if player.string(player.i(168)+176*i, 32) == action)
    host_hand, host_palm = hand_frame(player, seq, ['Bip01 '+side+' '+suffix for suffix in
        ('Hand', 'Finger1', 'Finger0', 'Finger11', 'Finger01')])
    _, source_palm = hand_frame(view, 2, [side.lower()+'_'+suffix for suffix in
        ('wr', 'in1', 'th1', 'in3', 'th3')])
    grip_rotation = multiply(host_palm, inverse(source_palm))
    posed = world.sample(0, 0, 0)
    held = view.sample(2, 0, 0)
    points = vertices(world, posed, 'ref')
    if lantern:
        # Opposing edges of the W carry handle, across its upper bar.
        grip = mean([points[i] for i in (23, 32, 35, 45)])
        source_frame = frame(grip, subtract(points[34], points[118]), (0, 0, 1))
        bar = vertices(view, held, 'ref_lantern', 'handle')
        bar_direction = subtract(max(bar, key=lambda p: p[1]), min(bar, key=lambda p: p[1]))
        dx, dy, _ = transform(grip_rotation, bar_direction, normal=True)
        # At standing idle the hanging lamp is vertical under the hand.
        placed = frame(pos(host_palm), (dx, dy, 0), (0, 0, 1))
    else:
        rear = mean(vertices(world, posed, 'ref', 'joint1'))
        tip = transform(posed[world.attachments[0]['bone']], world.attachments[0]['origin'])
        vpoints = vertices(view, held, 'ref_flare')
        vrear = mean(vertices(view, held, 'ref_flare', 'SMDImport'))
        vtip = transform(held[view.attachments[0]['bone']], view.attachments[0]['origin'])
        vaxis = subtract(vtip, vrear)
        fraction = sum(a*b for a, b in zip(subtract(pos(source_palm), vrear), vaxis)) / sum(x*x for x in vaxis)
        if not 0 < fraction < 1:
            raise ValueError('View-model hand is outside the flare shaft')
        waxis = subtract(tip, rear)
        grip = tuple(a + fraction*b for a, b in zip(rear, waxis))
        source_frame = frame(grip, waxis, subtract(points[37], rear))
        placed = frame(pos(host_palm), transform(grip_rotation, vaxis, normal=True),
                       transform(grip_rotation, subtract(vpoints[1], vrear), normal=True))
    anchor = world.bones[0]['name']
    matrix = multiply(inverse(host_hand), multiply(placed, multiply(inverse(source_frame), posed[0])))
    idle = dict(index=0, frame=0, blend=0, label='idle')
    calibration = dict(schema=1, status='verified', attachments='all',
        evidence=dict(path=str(player_path), sha256=player.sha256),
        source=dict(sha256=world.sha256, bone=anchor, sequence=idle),
        target=dict(sha256=template.sha256, bone=hand, handBone=hand, binding='hand', sequence=idle),
        sourceVertexBones=[bone['name'] for bone in world.bones],
        textures=[dict(name=t['name'], sha256=t['sha256']) for t in world.textures],
        sourceAnchorToTargetBone=matrix)
    data, receipt = models.generate(world, template, calibration)
    if lantern:
        # W has no attachments. Map the original V light and trace endpoint
        # through the same standing grip used to author the player model.
        if world.attachments or len(view.attachments) != 2:
            raise ValueError('Unexpected lantern light attachments')
        built = Studio(data)
        start = built.i(188)
        payload = bytearray(data[:start])
        hand_bone = built.bone_index(hand)
        for attachment in view.attachments:
            basis = multiply(inverse(host_hand), multiply(grip_rotation, held[attachment['bone']]))
            point = transform(basis, attachment['origin'])
            axes = [value for vector in attachment['vectors'] for value in transform(basis, vector, normal=True)]
            payload.extend(struct.pack('<32sii12f', attachment['name'].encode('ascii'),
                attachment['type'], hand_bone, *point, *axes))
        # The GoldSrc loader drops texture pixels after upload. Keep the new
        # attachment table in the retained prefix and relocate the pixel offsets.
        shift = len(payload) - start
        struct.pack_into('<i', payload, 188, len(payload))
        for i in range(built.i(180)):
            field = built.i(184) + 80*i + 76
            struct.pack_into('<i', payload, field, built.i(field) + shift)
        payload.extend(data[start:])
        struct.pack_into('<2i', payload, 212, 2, start)
        struct.pack_into('<i', payload, 72, len(payload))
        data = bytes(payload)
    decoded = Studio(data)
    if lantern:
        if len(decoded.attachments) != 2 or decoded.i(216)+2*88 > decoded.i(188):
            raise ValueError('Lantern light points must survive GoldSrc texture upload')
        if [t['sha256'] for t in built.textures] != [t['sha256'] for t in decoded.textures]:
            raise ValueError('Lantern P texture relocation changed pixel data')
        for src, dst in zip(view.attachments, decoded.attachments, strict=True):
            expected = transform(grip_rotation, transform(held[src['bone']], src['origin']))
            actual = transform(host_hand, dst['origin'])
            if decoded.bones[dst['bone']]['name'] != hand or math.dist(actual, expected) > 1e-4:
                raise ValueError('Lantern light attachment differs from its grip calibration')
    # Read the serialized vertex bone IDs and triangle stream back from the P model.
    # Compare multiplicities after applying the independent standing player pose.
    def corners(model, pose_transform):
        for body in model.bodyparts:
            for part in body['models']:
                for mesh in part['meshes']:
                    for tri in mesh['triangles']:
                        for vertex, _, u, v in tri:
                            point = pose_transform(part['verticesBones'][vertex], part['vertices'][vertex])
                            yield point, (u, v)
    def in_hand(bone, point):
        if decoded.bones[bone]['name'] != hand:
            raise ValueError('Serialized equipment is bound to the wrong bone')
        return transform(host_hand, point)
    transform_world = multiply(placed, inverse(source_frame))
    expected = corners(world, lambda bone, point: transform(transform_world, transform(posed[bone], point)))
    maximum_error = 0.0
    for (point, uv), (stored, stored_uv) in zip(expected, corners(decoded, in_hand), strict=True):
        maximum_error = max(maximum_error, math.dist(point, stored))
        if uv != stored_uv or maximum_error > .0001:
            raise ValueError('Serialized equipment mesh differs from its standing calibration')
    merged = multiply(host_hand, multiply(matrix, inverse(posed[0])))
    error = math.dist(transform(merged, grip), pos(host_palm))
    if error > 1e-5 or receipt['excludedTriangles'] or not receipt['selectedTriangles']:
        raise ValueError('Incomplete held mesh or misplaced grip')
    return data, dict(hand=hand, pose=action, viewSha256=view.sha256, gripError=error, serializedVertexError=maximum_error,
                      triangles=receipt['selectedTriangles'], attachments=len(decoded.attachments),
                      worldToPlayerHand=multiply(matrix, inverse(posed[0])))


def add_presentations(baseline, files, inputs):
    constants, images = contract(ROOT)
    hud_path = constants['CatalogPath']
    header = (ROOT/'native/csol/buy_menu.hpp').read_text('utf8')
    def menu_resource(symbol):
        return re.search(r'\b'+symbol+r'\[\]="(models/csol/buymenu_v\d+\.json)";', header)[1]

    menu_path = menu_resource('EquipmentPresentationResource')
    def load(name):
        path = models.ordinary(baseline/name)
        data = path.read_bytes()
        inputs[path] = models.digest(data)
        return json.loads(data)

    hud = load(hud_path)
    # Always merge the current authoritative base so updates to other shop items survive.
    menu = load(menu_resource('PresentationResource'))
    if menu.get('version') not in (3, 4):
        raise ValueError('Expected a host shop catalogue with weapon detail cards')
    definitions = (ROOT/'native/guest/cof/light_equipment.hpp').read_text('utf8')
    for kind, description in (('lantern', '按 5 取出提灯。'), ('flare', '在第 4 栏选择照明弹，点燃后再投出。')):
        match = re.search(r'Equipment '+kind.title()+r'\{\s*(\d+),\s*\d+,\s*\d+,\s*"(weapon_cof_'+kind+r')",\s*"([^"]+)"', definitions)
        if not match:
            raise ValueError('Missing host equipment definition for ' + kind)
        ident, classname, title = int(match[1]), match[2], match[3]
        weapon = cards.hud.RESOURCE+f'cof_{kind}_weapon.tga'
        ammo = cards.hud.RESOURCE+'cof_flare_ammo.tga' if kind == 'flare' else ''
        rows = [(hud['weapons'], 'name', dict(name=classname, title=title, weapon=weapon, selected=weapon, ammo=ammo, ammo2='')),
                (menu['images'], 'id', dict(id=ident, path=f'gfx/csol/buy/cof_{kind}.tga', width=170, height=80)),
                (menu['weaponInfo']['items'], 'id', dict(id=ident, itemId=ident, name=title, description=description,
                     table=False, values=['']*10, grade=[-1]*3))]
        for target, key, row in rows:
            existing = [i for i, item in enumerate(target) if item[key] == row[key]]
            if len(existing) > 1:
                raise ValueError('Duplicate equipment in baseline: '+kind)
            if existing:
                target[existing[0]] = row
            else:
                target.append(row)
    for name, value in ((hud_path, hud), (menu_path, menu)):
        files[name] = json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode('utf8')
    for name in dependencies(files[hud_path], constants, images):
        if name not in files and not models.ordinary(baseline/name).is_file():
            raise ValueError('Missing host HUD dependency ' + name)


def prepare(original, cstrike, output, baseline=None):
    original, cstrike, output = (models.ordinary(p) for p in (original, cstrike, output))
    if output.exists() or ROOT/'build' not in output.parents or any(
            parent == output or parent in output.parents for parent in (original, cstrike)):
        raise ValueError('Use a fresh build directory outside the input trees')
    inputs = {}

    def read(path, expected):
        path = models.ordinary(path)
        data = path.read_bytes()
        if models.digest(data) != expected:
            raise ValueError('Input identity changed: ' + path.name)
        inputs[path] = expected
        return data

    player_path = cstrike/'models/player/gign/gign.mdl'
    player = Studio(read(player_path, PLAYER), opaque_controllers=True)
    template = Studio(read(cstrike/'models/p_elite.mdl', TEMPLATE))
    files, grips = {}, {}
    for kind, pins in PINS.items():
        view = Studio(read(original/f'models/weapons/{kind}/v_{kind}.mdl', pins['view']))
        world = Studio(read(original/f'models/weapons/{kind}/w_{kind}.mdl', pins['world']))
        prefix = f'models/guest/cof/{kind}/'
        files[prefix+f'p_{kind}.mdl'], grips[kind] = model_for_hand(kind, view, world, player, template, player_path)
        files[prefix+f'v_{kind}.mdl'] = view.data
        files[prefix+f'w_{kind}.mdl'] = world.data
        if kind == 'flare':
            files[prefix+'p_flare_lit.mdl'] = files[prefix+'p_flare.mdl']
        picture = f'gfx/vgui/weapons/{kind}/640_{kind}_slot.tga'
        inventory = read(original/f'inventoryitems/weapons/weapon_{kind}.txt', pins['inventory']).decode()
        if not re.search(r'"inventory_picture"\s*\{\s*data\s*"'+re.escape(picture)+r'"\s*\}', inventory):
            raise ValueError('Inventory picture differs: ' + kind)
        width, height, pixels = cards.tga32(read(original/picture, pins['picture']))
        image = cards.fit(width, height, pixels, 170, 80)
        cards.hud.validate_weapon_pixels(image, kind)
        files[f'gfx/csol/buy/cof_{kind}.tga'] = cards.hud.tga(170, 80, image)
        files[f'{cards.hud.RESOURCE}cof_{kind}_weapon.tga'] = cards.hud.tga(170, 45, cards.fit(width, height, pixels, 170, 45))
        files[f'sprites/guest/cof/c{kind}.spr'] = cards.native_sprite(170, 80, image)
        rows = [f'{slot} {res} guest/cof/c{kind} 0 0 170 80' for res in (320,640) for slot in ('weapon','weapon_s')]
        if kind == 'flare':
            ammo = read(original/'sprites/cof_flare_icon.spr', FLARE_AMMO)
            w,h,rgba,_ = cards.hud.decode_sprite(ammo)
            if (w,h) != (32,32):
                raise ValueError('Invalid original flare ammunition icon')
            files[cards.hud.RESOURCE+'cof_flare_ammo.tga'] = cards.hud.tga(w, h, rgba)
            files['sprites/guest/cof/flare_ammo.spr'] = ammo
            rows.extend(f'ammo {res} guest/cof/flare_ammo 0 0 32 32' for res in (320,640))
        files[f'sprites/weapon_cof_{kind}.txt'] = ('\n'.join([str(len(rows)), *rows])+'\n').encode('ascii')
    effect_files(original, read, files)
    if baseline is not None:
        add_presentations(models.ordinary(baseline), files, inputs)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=output.name+'-preparing-', dir=output.parent))
    try:
        rows = []
        for name, data in files.items():
            path = stage/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            rows.append(dict(source=str(output/name), destination=name, bytes=len(data), sha256=models.digest(data)))
        if any(models.digest(path.read_bytes()) != expected for path, expected in inputs.items()):
            raise ValueError('Equipment inputs changed during preparation')
        (stage/'resource-plan.json').write_text(json.dumps(dict(files=rows), indent=2), encoding='utf8')
        (stage/'grips.json').write_text(json.dumps(grips, indent=2), encoding='utf8')
        cards.commit_output(stage, output)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return rows, grips


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'cstrike', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--baseline', type=Path, help='Current authoritative cstrike HUD and purchase catalogues')
    args = parser.parse_args()
    rows, grips = prepare(args.original, args.cstrike, args.output, args.baseline)
    print(json.dumps(dict(files=len(rows), grips=grips)))
