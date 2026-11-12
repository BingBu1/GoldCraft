#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add authored CoF Glock cards to an isolated, existing CSO presentation.

Only the new build directory is written. Original game files, the baseline,
runtime clients and servers are never modified. This is host UI adaptation,
not a reconstruction of CoF's inventory or its HUD layout.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('csol_hud_export', ROOT/'tools/Prepare-CSOLHud.py')
hud = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hud)
PINS = {
    'sprites/weapon_glock.txt': '5115c52533be24e12dc1de4776baea91e6dab0e41e5b25b4d5cd6c798ea37cf4',
    'sprites/cglock.spr': '175cff5be62c7340ede984439f6ffb100d14e73c54f781a449d9974a85fb60c6',
    'sprites/cof_glock_mag_icon.spr': '2638312b229c7a755a0ed6aa43c8b136fbc7988b749098ffa25e6cf8e3fafa35',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fit(width, height, rgba, target_width, target_height):
    """Authored aspect-preserving, centered nearest sampling, done offline."""
    scale = min(target_width / width, target_height / height)
    out_width, out_height = max(1, round(width*scale)), max(1, round(height*scale))
    left, top = (target_width-out_width)//2, (target_height-out_height)//2
    result = bytearray(target_width*target_height*4)
    columns = [min(width-1, (2*x+1)*width//(2*out_width))*4 for x in range(out_width)]
    for y in range(out_height):
        source = min(height-1, (2*y+1)*height//(2*out_height))*width*4
        destination = ((top+y)*target_width+left)*4
        for x, offset in enumerate(columns):
            result[destination+x*4:destination+x*4+4] = rgba[source+offset:source+offset+4]
    return bytes(result)


def prepare(original_root, baseline_root, output):
    original_root, baseline_root, output = map(Path, (original_root, baseline_root, output))
    for path in (original_root, baseline_root, output):
        hud.no_reparse(path.absolute())
    original_root, baseline_root, output = (p.resolve() for p in (original_root, baseline_root, output))
    if not original_root.is_dir() or not baseline_root.is_dir():
        raise ValueError('Original and baseline resource roots must exist')
    if not baseline_root.is_relative_to(ROOT/'sandbox') and not baseline_root.is_relative_to(ROOT/'build'):
        raise ValueError('Baseline must be an isolated workspace sandbox or build resource directory')
    if output == ROOT/'build' or not output.is_relative_to(ROOT/'build') or output.exists():
        raise ValueError('Use a fresh output strictly beneath workspace build')
    if output.is_relative_to(baseline_root) or output.is_relative_to(original_root):
        raise ValueError('Output and inputs must be isolated')
    inputs = {}

    def read(path, pin=None):
        hud.no_reparse(path)
        data = path.read_bytes()
        digest = sha(data)
        previous = inputs.setdefault(str(path), digest)
        if previous != digest or pin is not None and digest != pin:
            raise ValueError(f'Input identity differs: {path}')
        return data

    definition = read(ROOT/'native/guest/cof/catalog.hpp').decode('utf8')
    match = re.search(r'PistolDefinition\{Pistol::Glock,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"', definition)
    if not match:
        raise ValueError('Glock author definition is missing or unsupported')
    ident, original_id, clip, reserve, price, ammo_price, ammo_count = map(int, match.groups()[:7])
    classname, ammunition, title = match.groups()[7:]
    if (ident, original_id, classname, ammunition, clip) != (896, 3, 'weapon_cof_glock', 'cof_glock', 15):
        raise ValueError('Unreviewed host/original Glock identity')
    shop = read(ROOT/'native/csol/buy_menu.cpp').decode('utf8')
    if not re.search(r'\{896,1,"cof_glock","'+re.escape(title)+r'","cof_glock",'+str(price)+r'\}', shop):
        raise ValueError('Host shop and guest author definition disagree')
    contract = read(ROOT/'native/csol/buy_menu.hpp').decode('utf8')
    menu_path = re.search(r'PresentationResource\[\]="(models/csol/buymenu_v\d+\.json)";', contract)
    if not menu_path:
        raise ValueError('Missing versioned host purchase resource')
    menu_path = menu_path[1]
    baseline_hud = json.loads(read(baseline_root/hud.CATALOG))
    baseline_menu = json.loads(read(baseline_root/menu_path))
    if baseline_hud.get('version') != 1 or baseline_menu.get('version') != 3:
        raise ValueError('Unsupported host presentation schema')
    result_hud, result_menu = copy.deepcopy(baseline_hud), copy.deepcopy(baseline_menu)
    rows = result_hud['weapons']
    images = result_menu['images']
    info = result_menu['weaponInfo']['items']
    if any(r['name'] == classname for r in rows) or any(r['id'] == ident for r in images+info):
        raise ValueError('Guest presentation already exists; supply the unmodified baseline')
    assets = {name: read(original_root/name, pin) for name, pin in PINS.items()}
    original_rows = hud.sprite_rows(assets['sprites/weapon_glock.txt'], {'weapon', 'weapon_s'})
    normal = original_rows.get(('weapon', 640))
    if normal != dict(atlas='sprites/cglock.spr', x=0, y=0, width=170, height=80) or original_rows.get(('weapon_s', 640)) != normal:
        raise ValueError('Original Glock selection rectangle differs')
    width, height, rgba, _ = hud.decode_sprite(assets['sprites/cglock.spr'])
    pixels = b''.join(rgba[y*width*4:(y*width+170)*4] for y in range(80))
    aw, ah, ammo, _ = hud.decode_sprite(assets['sprites/cof_glock_mag_icon.spr'])
    if (aw, ah) != (32, 32):
        raise ValueError('Original Glock magazine icon dimensions differ')
    hud.validate_weapon_pixels(pixels, 'guest/cof/glock weapon')
    hud.validate_weapon_pixels(ammo, 'guest/cof/glock ammo')
    weapon_path = hud.RESOURCE+'cof_glock_weapon.tga'
    ammo_path = hud.RESOURCE+'cof_glock_ammo.tga'
    purchase_path = 'gfx/csol/buy/cof_glock.tga'
    # ItemId is host UI metadata only. It never creates a CSO gameplay item.
    rows.append(dict(name=classname, title=title, weapon=weapon_path, selected=weapon_path,
                     ammo=ammo_path, ammo2=''))
    images.append(dict(id=ident, path=purchase_path, width=170, height=80))
    info.append(dict(id=ident, itemId=ident, name=title,
        description=f'Cry of Fear 客串手枪。{clip} 发弹匣，支持机瞄与枪托推击。',
        table=False, values=['']*10, grade=[-1]*3))
    native_list = ('6\n'
        'weapon 320 guest/cof/cglock 0 0 170 80\nweapon_s 320 guest/cof/cglock 0 0 170 80\n'
        'weapon 640 guest/cof/cglock 0 0 170 80\nweapon_s 640 guest/cof/cglock 0 0 170 80\n'
        'ammo 320 guest/cof/glock_ammo 0 0 32 32\nammo 640 guest/cof/glock_ammo 0 0 32 32\n').encode('ascii')
    generated = {
        hud.CATALOG: json.dumps(result_hud, ensure_ascii=False, separators=(',', ':')).encode('utf8'),
        menu_path: json.dumps(result_menu, ensure_ascii=False, separators=(',', ':')).encode('utf8'),
        weapon_path: hud.tga(170, 45, fit(170, 80, pixels, 170, 45)),
        ammo_path: hud.tga(aw, ah, ammo), purchase_path: hud.tga(170, 80, pixels),
        'sprites/guest/cof/cglock.spr': assets['sprites/cglock.spr'],
        'sprites/guest/cof/glock_ammo.spr': assets['sprites/cof_glock_mag_icon.spr'],
        'sprites/weapon_cof_glock.txt': native_list,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=output.name+'-preparing-', dir=output.parent))
    try:
        files = []
        for name, data in generated.items():
            hud.relative(name)
            path = stage/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            files.append(dict(path=name, bytes=len(data), sha256=sha(data)))
        for name, digest in inputs.items():
            if sha(Path(name).read_bytes()) != digest:
                raise ValueError(f'Input changed during preparation: {name}')
        receipt = dict(success=True, inputs=inputs, files=files, originalId=original_id, hostId=ident,
            hostAdaptation=True, cardConversion='aspect-preserving centered nearest170x80->170x45 offline',
            clip=clip, reserve=reserve, price=price, ammunitionPrice=ammo_price, ammunitionCount=ammo_count,
            baselineOtherRowsUnchanged=True, deployed=False, gameplay=False, performance=False)
        (stage/'presentation-receipt.json').write_text(json.dumps(receipt, indent=2), 'utf8')
        os.replace(stage, output)
        return receipt
    except BaseException:
        shutil.rmtree(stage)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original-game-root', 'baseline', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    receipt = prepare(args.original_game_root, args.baseline, args.output)
    print(json.dumps(dict(success=True, files=len(receipt['files']), bytes=sum(r['bytes'] for r in receipt['files']))))


if __name__ == '__main__':
    main()
