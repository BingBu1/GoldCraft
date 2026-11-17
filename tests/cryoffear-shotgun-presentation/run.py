# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify original 870 reserve art and preserve the existing eight-gun presentation."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('original', 'baseline', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('cof_author', ROOT / 'tools/Prepare-CryOfFearPresentation.py')
    author = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(author)
    out = args.output.absolute()
    result = author.prepare(args.original, args.baseline, out, 'shotgun')
    checks = 0
    for row in result['files']:
        path = out / row['path']
        assert path.stat().st_size == row['bytes'] and sha(path) == row['sha256']
        checks += 1
    assert (result['hostId'], result['originalId'], result['clip'], result['reserve'], result['price'],
            result['ammunitionPrice'], result['ammunitionCount']) == (904, 4, 5, 50, 3000, 65, 5)
    checks += 1
    menu_path = next(row['path'] for row in result['files'] if '/buymenu_' in row['path'])
    before_hud = json.loads((args.baseline / author.hud.CATALOG).read_bytes())
    after_hud = json.loads((out / author.hud.CATALOG).read_bytes())
    before_menu = json.loads((args.baseline / menu_path).read_bytes())
    after_menu = json.loads((out / menu_path).read_bytes())
    assert after_hud['weapons'][:-1] == before_hud['weapons']
    assert after_menu['images'][:-1] == before_menu['images']
    assert after_menu['weaponInfo']['items'][:-1] == before_menu['weaponInfo']['items']
    checks += 3
    assert after_menu['images'][-1]['id'] == after_menu['weaponInfo']['items'][-1]['id'] == 904
    checks += 1
    before_hud['weapons'] = after_hud['weapons']
    before_menu['images'] = after_menu['images']
    before_menu['weaponInfo']['items'] = after_menu['weaponInfo']['items']
    assert before_hud == after_hud and before_menu == after_menu
    checks += 1
    row = after_hud['weapons'][-1]
    assert row['name'] == 'weapon_cof_shotgun' and row['title'] == '客串武器·Remington 870'
    checks += 1
    original_icon = (args.original / 'sprites/cof_shotgun_box_icon.spr').read_bytes()
    assert (out / 'sprites/guest/cof/shotgun_ammo.spr').read_bytes() == original_icon
    checks += 1
    width, height, pixels, _ = author.hud.decode_sprite(original_icon)
    assert (width, height) == (32, 32)
    assert author.tga32((out / row['ammo']).read_bytes()) == (width, height, pixels)
    checks += 1
    box = (args.original / 'sprites/cof_shotgun_box.spr').read_bytes()
    frames = author.hud.decode_sprite_frames(box)
    assert len(frames) == 6 and all((frame[0], frame[1]) == (32, 128) for frame in frames)
    assert box != original_icon
    checks += 2
    width, height, pixels = author.tga32((args.original / 'gfx/vgui/weapons/shotgun/640_shotgun_slot.tga').read_bytes())
    card = author.tga32((out / 'gfx/csol/buy/cof_shotgun.tga').read_bytes())
    assert card == (170, 80, author.fit(width, height, pixels, 170, 80))
    checks += 1
    for key, digest in result['inputs'].items():
        assert sha(key) == digest
    receipt = dict(success=True, checks=checks, inputs=result['inputs'], files=result['files'],
                   toolSha256=sha(author.__file__), testSha256=sha(__file__),
                   originalClipBoxSha256=sha(args.original / 'sprites/cof_shotgun_box.spr'),
                   originalReserveIconSha256=sha(args.original / 'sprites/cof_shotgun_box_icon.spr'),
                   existingRowsPreserved=True, clipBoxNotSquashed=True, sourceCopies=0, live=False, performance=False)
    (out / 'test-receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(success=True, checks=checks, existingRowsPreserved=True, clipBoxNotSquashed=True)))


if __name__ == '__main__':
    main()
