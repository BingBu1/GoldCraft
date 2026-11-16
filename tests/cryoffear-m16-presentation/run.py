# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare MP5 then M16 from original art; verify six prior catalog rows survive."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'baseline', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('author', ROOT/'tools/Prepare-CryOfFearPresentation.py')
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    out = args.output.absolute()
    tool.hud.no_reparse(out)
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Fresh build output required')
    out.mkdir(parents=True)
    mp5 = tool.prepare(args.original, args.baseline, out/'mp5', 'mp5')
    result = tool.prepare(args.original, out/'mp5', out/'m16', 'm16')
    checks = 0
    for row in result['files']:
        path = out/'m16'/row['path']
        assert sha(path) == row['sha256'] and path.stat().st_size == row['bytes']
        checks += 1
    assert (result['originalId'], result['hostId'], result['clip'], result['reserve'],
            result['price'], result['ammunitionPrice'], result['ammunitionCount']) == (9, 902, 20, 200, 3100, 40, 20)
    checks += 1
    menu_path = next(row['path'] for row in result['files'] if '/buymenu_' in row['path'])
    before_hud = json.loads((out/'mp5'/tool.hud.CATALOG).read_bytes())
    after_hud = json.loads((out/'m16'/tool.hud.CATALOG).read_bytes())
    before_menu = json.loads((out/'mp5'/menu_path).read_bytes())
    after_menu = json.loads((out/'m16'/menu_path).read_bytes())
    assert after_hud['weapons'][:-1] == before_hud['weapons']
    assert after_menu['images'][:-1] == before_menu['images']
    assert after_menu['weaponInfo']['items'][:-1] == before_menu['weaponInfo']['items']
    assert all(any(row['name'] == 'weapon_cof_'+key for row in before_hud['weapons'])
               for key in ('glock', 'p345', 'revolver', 'vp70', 'tmp', 'mp5'))
    checks += 4
    row = after_hud['weapons'][-1]
    assert row['name'] == 'weapon_cof_m16' and row['title'] == '客串武器·M16' and len(row['ammoFrames']) == 2
    assert after_menu['images'][-1]['id'] == after_menu['weaponInfo']['items'][-1]['id'] == 902
    assert '突击步枪' in after_menu['weaponInfo']['items'][-1]['description']
    checks += 3
    original_ammo = (args.original/'sprites/cof_m16_mag_icon.spr').read_bytes()
    assert (out/'m16/sprites/guest/cof/m16_ammo.spr').read_bytes() == original_ammo
    original_frames = tool.hud.decode_sprite_frames(original_ammo)
    assert len(original_frames) == 2
    for path, (width, height, pixels, _) in zip(row['ammoFrames'], original_frames):
        assert tool.tga32((out/'m16'/path).read_bytes()) == (width, height, pixels)
    width, height, pixels = tool.tga32((args.original/'gfx/vgui/weapons/m16/640_m16_slot.tga').read_bytes())
    card = tool.tga32((out/'m16/gfx/csol/buy/cof_m16.tga').read_bytes())
    assert card == (170, 80, tool.fit(width, height, pixels, 170, 80))
    assert tool.tga32((out/'m16'/row['weapon']).read_bytes()) == (170, 45, tool.fit(170, 80, card[2], 170, 45))
    checks += 4
    native_list = (out/'m16/sprites/weapon_cof_m16.txt').read_bytes()
    assert b'guest/cof/cm16' in native_list and b'guest/cof/m16_ammo' in native_list and b'glock' not in native_list
    width, height, fallback, _ = tool.hud.decode_sprite((out/'m16/sprites/guest/cof/cm16.spr').read_bytes())
    assert (width, height) == (170, 80)
    checks += 2
    import csol_hud_resources
    constants, images = csol_hud_resources.contract()
    dependencies = csol_hud_resources.dependencies((out/'m16'/tool.hud.CATALOG).read_text('utf8'), constants, images)
    assert all(path in dependencies for path in row['ammoFrames'])
    checks += 1
    for at in range(0, len(fallback), 4):
        assert (fallback[at+3] == 0) == (card[2][at+3] < 128)
        if fallback[at+3]:
            assert max(abs(a-b) for a, b in zip(fallback[at:at+3], card[2][at:at+3])) <= 25
    checks += 1
    for name, digest in (mp5['inputs'] | result['inputs']).items():
        assert sha(Path(name)) == digest
    receipt = dict(success=True, checks=checks, palettePixels=170*80, files=result['files'],
                   mp5Inputs=mp5['inputs'], inputs=result['inputs'],
                   toolSha256=sha(Path(tool.__file__)), testSha256=sha(Path(__file__)),
                   previousSixRowsUnchanged=True, deployed=False, gameplay=False, performance=False, sourceCopies=0)
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(success=True, checks=checks, palettePixels=170*80, files=len(result['files']))))


if __name__ == '__main__':
    main()
