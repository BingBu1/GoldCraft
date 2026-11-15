# SPDX-License-Identifier: GPL-3.0-or-later
"""Consume pinned MP5 art through the real offline author, preserving prior rows."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original', 'baseline', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.absolute()
    spec = importlib.util.spec_from_file_location('author', ROOT/'tools/Prepare-CryOfFearPresentation.py')
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    tool.hud.no_reparse(out)
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):
        raise ValueError('Fresh build output required')
    out.mkdir(parents=True)
    # A temporary Windows sharing conflict retries the same atomic rename;
    # a persistent denial or newly appearing destination remains an error.
    transient = PermissionError('controlled sharing conflict')
    transient.winerror = 32
    source, destination = out/'publish-stage', out/'publish-result'
    source.mkdir()
    (source/'payload').write_bytes(b'atomic')
    rename = Path.rename
    attempts = []

    def publish(path, target):
        attempts.append(target)
        if len(attempts) < 3:
            raise transient
        return rename(path, target)

    with patch.object(Path, 'rename', publish), patch.object(tool.time, 'sleep') as sleep:
        tool.commit_output(source, destination)
        assert sleep.call_count >= 2
    assert (destination/'payload').read_bytes() == b'atomic' and not source.exists()
    checks = 2
    with patch.object(Path, 'rename', side_effect=transient) as retry, patch.object(tool.time, 'sleep') as sleep:
        try:
            tool.commit_output(out/'missing-stage', out/'missing-result')
        except PermissionError:
            assert retry.call_count == 6 and sleep.call_count == 5
            checks += 1
        else:
            raise AssertionError('Permanent publication denial was swallowed')
    with patch.object(Path, 'rename') as retry:
        try:
            tool.commit_output(out/'missing-stage', destination)
        except FileExistsError:
            assert retry.call_count == 0
            checks += 1
        else:
            raise AssertionError('Existing output was replaced')
    result = tool.prepare(args.original, args.baseline, out/'mp5', 'mp5')
    for row in result['files']:
        path = out/'mp5'/row['path']
        assert sha(path) == row['sha256'] and path.stat().st_size == row['bytes']
        checks += 1
    assert (result['originalId'], result['hostId'], result['clip'], result['reserve'], result['price']) == (27, 901, 30, 300, 2400)
    checks += 1
    menu_path = next(row['path'] for row in result['files'] if '/buymenu_' in row['path'])
    before_hud = json.loads((args.baseline/tool.hud.CATALOG).read_bytes())
    after_hud = json.loads((out/'mp5'/tool.hud.CATALOG).read_bytes())
    before_menu = json.loads((args.baseline/menu_path).read_bytes())
    after_menu = json.loads((out/'mp5'/menu_path).read_bytes())
    assert after_hud['weapons'][:-1] == before_hud['weapons']
    assert after_menu['images'][:-1] == before_menu['images']
    assert after_menu['weaponInfo']['items'][:-1] == before_menu['weaponInfo']['items']
    checks += 3
    row = after_hud['weapons'][-1]
    assert row['name'] == 'weapon_cof_mp5' and row['title'] == '客串武器·MP5A3' and 'ammoFrames' not in row
    assert after_menu['images'][-1]['id'] == after_menu['weaponInfo']['items'][-1]['id'] == 901
    checks += 2
    original_ammo = (args.original/'sprites/cof_mp5_mag_icon.spr').read_bytes()
    assert (out/'mp5/sprites/guest/cof/mp5_ammo.spr').read_bytes() == original_ammo
    width, height, pixels, _ = tool.hud.decode_sprite(original_ammo)
    assert tool.tga32((out/'mp5'/row['ammo']).read_bytes()) == (width, height, pixels)
    checks += 2
    picture = (args.original/'gfx/vgui/weapons/mp5/640_mp5_slot.tga').read_bytes()
    width, height, pixels = tool.tga32(picture)
    card = tool.tga32((out/'mp5/gfx/csol/buy/cof_mp5.tga').read_bytes())
    assert card == (170, 80, tool.fit(width, height, pixels, 170, 80))
    width, height, fallback, _ = tool.hud.decode_sprite((out/'mp5/sprites/guest/cof/cmp5.spr').read_bytes())
    assert (width, height) == (170, 80)
    checks += 2
    for at in range(0, len(fallback), 4):
        assert (fallback[at+3] == 0) == (card[2][at+3] < 128)
        checks += 1
        if fallback[at+3]:
            assert max(abs(a-b) for a, b in zip(fallback[at:at+3], card[2][at:at+3])) <= 25
            checks += 1
    # Only three native HUD resources per gun; full optional HUD is validated
    # separately before any runtime deployment.
    closure = out/'native-closure'
    closure_inputs = {}
    for weapon, selection in (('glock', 'cglock'), ('p345', 'cp345'), ('revolver', 'crever'),
                              ('vp70', 'cvp70'), ('tmp', 'ctmp'), ('mp5', 'cmp5')):
        for name in ('sprites/weapon_cof_'+weapon+'.txt', 'sprites/guest/cof/'+selection+'.spr',
                     'sprites/guest/cof/'+weapon+'_ammo.spr'):
            source = (out/'mp5'/name) if weapon == 'mp5' else args.baseline/name
            tool.hud.no_reparse(source)
            closure_inputs[str(source)] = sha(source)
            destination = closure/name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
    for name, digest in (result['inputs'] | closure_inputs).items():
        assert sha(Path(name)) == digest
    receipt = dict(success=True, checks=checks, files=result['files'], inputs=result['inputs'],
                   closureInputs=closure_inputs, toolSha256=sha(Path(tool.__file__)), testSha256=sha(Path(__file__)),
                   previousRowsUnchanged=True, deployed=False, gameplay=False, performance=False, sourceCopies=0)
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf8')
    print(json.dumps(dict(success=True, checks=checks, files=len(result['files']), previousRowsUnchanged=True)))


if __name__ == '__main__':
    main()
