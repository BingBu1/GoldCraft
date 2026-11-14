#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add reviewed CoF pistol cards to an isolated, existing CSO presentation.

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
P345_PINS={
    'inventoryitems/weapons/weapon_p345.txt':'376a05dad38387981be4a7f88a1a6444582f5781afd209df9a3d5a7ea18bb2fe',
    'gfx/vgui/weapons/p345/640_p345_slot.tga':'9410f7c93e0ff02ca9cf1e0bb7f504471041f4e8809d549fac2ab2b220ffc5fc',
    'sprites/cof_p345_mag_icon.spr':'57549f1601a0ec4fc0d80f9485bdc39f32ca262ad376ae3fb68afe27c517cfc3',
}
REVOLVER_PINS={
    'inventoryitems/weapons/weapon_revolver.txt':'584ecbd431f86ee5d1d385cc63c4116109d028c4814b7c7436d85c539396ae4b',
    'gfx/vgui/weapons/revolver/640_revolver_slot.tga':'93e840af2e0cba5732118eecc3b42961a8fc447c8aa3b10a6a69a7396915b0b7',
    'sprites/cof_revolver_cylinder.spr':'1abc649e0684796805236ebd0442db4e518cfa3c400a710db27a6e09906693a0',
}
VP70_PINS={
    "inventoryitems/weapons/weapon_vp70.txt": "96b54688cd5733995a31fff1d2d49569f21611272a56ed8cfb1933f743165846",
    "gfx/vgui/weapons/vp70/640_vp70_slot.tga": "527ae58a905d13bee62e6b778dc573bddbab35417d71ec0c1774be60d0a1d99f",
    "sprites/cof_vp70_mag_icon.spr": "2638312b229c7a755a0ed6aa43c8b136fbc7988b749098ffa25e6cf8e3fafa35"
}
REVOLVER_AMMO_SHA = '1abc649e0684796805236ebd0442db4e518cfa3c400a710db27a6e09906693a0'


def revolver_ammunition(data):
    """Author all original cylinder states into the established 32px CSO slot."""
    if sha(data) != REVOLVER_AMMO_SHA:
        raise ValueError('Original Revolver cylinder identity changed')
    frames = hud.decode_sprite_frames(data)
    if len(frames) != 6 or any(frame[:2] != (64, 64) for frame in frames):
        raise ValueError('Original Revolver requires six independent 64px frames')
    palette_end = 42 + struct.unpack_from('<H', data, 40)[0] * 3
    native = bytearray(data[:palette_end])
    struct.pack_into('<f', native, 16, struct.unpack_from('<f', data, 16)[0] * .5)
    struct.pack_into('<ii', native, 20, 32, 32)
    images = []
    at = palette_end
    for width, height, rgba, _ in frames:
        kind, x, y, fw, fh = struct.unpack_from('<iiiii', data, at)
        if (kind, x, y, fw, fh) != (0, -32, 32, width, height):
            raise ValueError('Original cylinder frame origin changed')
        indices = data[at + 20:at + 20 + width * height]
        # Same centered nearest sampling as fit; palette/mode remain original.
        native += struct.pack('<iiiii', 0, x // 2, y // 2, 32, 32)
        native += bytes(indices[(2 * row + 1) * 64 + 2 * column + 1]
                        for row in range(32) for column in range(32))
        images.append(hud.tga(32, 32, fit(width, height, rgba, 32, 32)))
        at += 20 + width * height
    if at != len(data):
        raise ValueError('Unexpected cylinder trailing bytes')
    return tuple(images), bytes(native)


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


def tga32(data):
    if len(data)<18 or data[1]!=0 or data[2]!=2 or data[16]!=32 or data[17]&0xC0:
        raise ValueError('Expected an uncompressed original RGBA inventory picture')
    width,height=struct.unpack_from('<HH',data,12)
    end=18+data[0]+width*height*4
    footer=data[end:]
    if not 1<=width<=4096 or not 1<=height<=4096 or len(data)<end or footer not in (b'',bytes(8)+b'TRUEVISION-XFILE.\0'):
        raise ValueError('Invalid inventory picture dimensions')
    raw=data[18+data[0]:end];rgba=bytearray(len(raw))
    for y in range(height):
        source_y=y if data[17]&0x20 else height-1-y
        for x in range(width):
            source_x=width-1-x if data[17]&0x10 else x
            at=(source_y*width+source_x)*4;to=(y*width+x)*4
            rgba[to:to+4]=bytes((raw[at+2],raw[at+1],raw[at],raw[at+3]))
    return width,height,bytes(rgba)

def native_sprite(width,height,rgba):
    # Explicit stock-CS fallback authoring. Modern HUD/card uses full RGBA;
    # the palette fallback has 216 RGB colors and index255 transparency.
    palette=bytearray(768)
    for r in range(6):
        for g in range(6):
            for b in range(6):palette[(r*36+g*6+b)*3:(r*36+g*6+b)*3+3]=bytes((r*51,g*51,b*51))
    pixels=bytearray(width*height)
    for i in range(width*height):
        r,g,b,a=rgba[i*4:i*4+4]
        pixels[i]=255 if a<128 else ((r+25)//51)*36+((g+25)//51)*6+(b+25)//51
    return struct.pack('<4siiifiiifi',b'IDSP',2,2,3,float(max(width,height)),width,height,1,0.,0)+\
        struct.pack('<H',256)+palette+struct.pack('<5i',0,-width//2,height//2,width,height)+pixels

def prepare(original_root, baseline_root, output, weapon='glock'):
    if weapon not in ('glock','p345','revolver','vp70'):raise ValueError('Unreviewed guest presentation weapon')
    p345=weapon=='p345';revolver=weapon=='revolver';vp70=weapon=='vp70'
    kind={'glock':'Glock','p345':'P345','revolver':'Revolver','vp70':'VP70'}[weapon];key='cof_'+weapon
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
    match = re.search(r'PistolDefinition\{Pistol::'+kind+r',\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"', definition)
    if not match:
        raise ValueError('Glock author definition is missing or unsupported')
    ident, original_id, clip, reserve, price, ammo_price, ammo_count = map(int, match.groups()[:7])
    classname, ammunition, title = match.groups()[7:]
    expected=(899,19,'weapon_cof_vp70','cof_vp70',18) if vp70 else (898,21,'weapon_cof_revolver','cof_revolver',5) if revolver else (897,7,'weapon_cof_p345','cof_p345',8) if p345 else (896,3,'weapon_cof_glock','cof_glock',15)
    if (ident, original_id, classname, ammunition, clip) != expected:
        raise ValueError('Unreviewed host/original Glock identity')
    shop = read(ROOT/'native/csol/buy_menu.cpp').decode('utf8')
    if not re.search(r'\{'+str(ident)+r',1,"'+key+r'","'+re.escape(title)+r'","'+key+r'",'+str(price)+r'\}', shop):
        raise ValueError('Host shop and guest author definition disagree')
    contract = read(ROOT/'native/csol/buy_menu.hpp').decode('utf8')
    menu_path = re.search(r'PresentationResource\[\]="(models/csol/buymenu_v\d+\.json)";', contract)
    if not menu_path:
        raise ValueError('Missing versioned host purchase resource')
    menu_path = menu_path[1]
    baseline_hud = json.loads(read(baseline_root/hud.CATALOG))
    baseline_menu = json.loads(read(baseline_root/menu_path))
    if baseline_hud.get('version') != 1 or baseline_menu.get('version') not in (3, 4):
        raise ValueError('Unsupported host presentation schema')
    result_hud, result_menu = copy.deepcopy(baseline_hud), copy.deepcopy(baseline_menu)
    rows = result_hud['weapons']
    images = result_menu['images']
    info = result_menu['weaponInfo']['items']
    if any(r['name'] == classname for r in rows) or any(r['id'] == ident for r in images+info):
        raise ValueError('Guest presentation already exists; supply the unmodified baseline')
    assets = {name: read(original_root/name, pin) for name, pin in (VP70_PINS if vp70 else REVOLVER_PINS if revolver else P345_PINS if p345 else PINS).items()}
    if p345 or revolver or vp70:
        text=assets['inventoryitems/weapons/weapon_'+weapon+'.txt'].decode('utf8')
        picture='gfx/vgui/weapons/'+weapon+'/640_'+weapon+'_slot.tga'
        if not re.search(r'"inventory_picture"\s*\{\s*data\s*"'+re.escape(picture)+r'"\s*\}',text):
            raise ValueError('Original inventory picture mapping differs')
        width,height,rgba=tga32(assets[picture])
        pixels=fit(width,height,rgba,170,80)
        selection_sprite=native_sprite(170,80,pixels)
    else:
        original_rows = hud.sprite_rows(assets['sprites/weapon_glock.txt'], {'weapon', 'weapon_s'})
        normal = original_rows.get(('weapon', 640))
        if normal != dict(atlas='sprites/cglock.spr', x=0, y=0, width=170, height=80) or original_rows.get(('weapon_s', 640)) != normal:
            raise ValueError('Original Glock selection rectangle differs')
        width, height, rgba, _ = hud.decode_sprite(assets['sprites/cglock.spr'])
        pixels = b''.join(rgba[y*width*4:(y*width+170)*4] for y in range(80))
        selection_sprite=assets['sprites/cglock.spr']
    ammo_frames=None
    if revolver:
        ammo_frames,ammo_asset=revolver_ammunition(assets['sprites/cof_revolver_cylinder.spr'])
        aw,ah,ammo=tga32(ammo_frames[0])
    else:
        ammo_asset=assets['sprites/cof_'+weapon+'_mag_icon.spr']
        aw, ah, ammo, _ = hud.decode_sprite(ammo_asset)
        if (aw, ah) != (32, 32):
            raise ValueError('Original magazine icon dimensions differ')
    hud.validate_weapon_pixels(pixels, 'guest/cof/glock weapon')
    hud.validate_weapon_pixels(ammo, 'guest/cof/glock ammo')
    weapon_path = hud.RESOURCE+key+'_weapon.tga'
    ammo_path = hud.RESOURCE+key+'_ammo.tga'
    purchase_path = 'gfx/csol/buy/'+key+'.tga'
    # ItemId is host UI metadata only. It never creates a CSO gameplay item.
    rows.append(dict(name=classname, title=title, weapon=weapon_path, selected=weapon_path,
                     ammo=ammo_path, ammo2=''))
    frame_paths=[]
    if revolver:
        frame_paths=[ammo_path]+[hud.RESOURCE+key+'_ammo'+str(i)+'.tga' for i in range(1,6)]
        rows[-1]['ammoFrames']=frame_paths
    images.append(dict(id=ident, path=purchase_path, width=170, height=80))
    info.append(dict(id=ident, itemId=ident, name=title,
        description=f'Cry of Fear 客串手枪。{clip} 发弹匣，支持机瞄与枪托推击。',
        table=False, values=['']*10, grade=[-1]*3))
    if vp70:info[-1]['description']=f'Cry of Fear 客串手枪。{clip} 发弹匣，三连发，支持机瞄与枪托推击。'
    native_list = ('6\n'
        'weapon 320 guest/cof/cglock 0 0 170 80\nweapon_s 320 guest/cof/cglock 0 0 170 80\n'
        'weapon 640 guest/cof/cglock 0 0 170 80\nweapon_s 640 guest/cof/cglock 0 0 170 80\n'
        'ammo 320 guest/cof/glock_ammo 0 0 32 32\nammo 640 guest/cof/glock_ammo 0 0 32 32\n').encode('ascii')
    if p345 or vp70:native_list=native_list.replace(b'cglock',('c'+weapon).encode()).replace(b'glock_ammo',(weapon+'_ammo').encode())
    if revolver:native_list=native_list.replace(b'cglock',b'crever').replace(b'glock_ammo',b'revolver_ammo')
    selection_name='crever' if revolver else 'c'+weapon
    generated = {
        hud.CATALOG: json.dumps(result_hud, ensure_ascii=False, separators=(',', ':')).encode('utf8'),
        menu_path: json.dumps(result_menu, ensure_ascii=False, separators=(',', ':')).encode('utf8'),
        weapon_path: hud.tga(170, 45, fit(170, 80, pixels, 170, 45)),
        ammo_path: hud.tga(aw, ah, ammo), purchase_path: hud.tga(170, 80, pixels),
        'sprites/guest/cof/'+selection_name+'.spr': selection_sprite,
        'sprites/guest/cof/'+weapon+'_ammo.spr': ammo_asset,
        'sprites/weapon_cof_'+weapon+'.txt': native_list,
    }
    if ammo_frames:
        generated.update(zip(frame_paths,ammo_frames))
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
    parser.add_argument('--weapon',choices=('glock','p345','revolver','vp70'),default='glock')
    args = parser.parse_args()
    receipt = prepare(args.original_game_root, args.baseline, args.output,args.weapon)
    print(json.dumps(dict(success=True, files=len(receipt['files']), bytes=sum(r['bytes'] for r in receipt['files']))))


if __name__ == '__main__':
    main()
