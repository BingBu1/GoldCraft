"""Check extracted mount priority and actual original HUD palette contributions."""
from pathlib import Path
import argparse
import importlib.util
import json
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('hud_export', ROOT / 'tools/Prepare-CSOLHud.py')
hud = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hud)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    checks = 0

    def check(condition, reason):
        nonlocal checks
        checks += 1
        if not condition:
            raise AssertionError(reason)

    with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='hud-mount-test-') as temporary:
        base = Path(temporary)
        for mount in ('common', 'locale_chn', 'cstrike_chn', 'valve'):
            path = base / mount / 'sprites/a.spr'
            path.parent.mkdir(parents=True)
            path.write_bytes(mount.encode('ascii'))
        check(hud.common_resource(base / 'common', 'sprites/a.spr').read_bytes() == b'locale_chn', 'locale must override common and valve')
        (base / 'locale_chn/sprites/a.spr').unlink()
        check(hud.common_resource(base / 'common', 'sprites/a.spr').read_bytes() == b'cstrike_chn', 'Chinese mount must override valve fallback')
        (base / 'cstrike_chn/sprites/a.spr').unlink()
        check(hud.common_resource(base / 'common', 'sprites/a.spr').read_bytes() == b'common', 'merged common precedes valve')
        try:
            hud.validate_weapon_pixels(bytes([255, 255, 255, 73]) * 576, 'old gray placeholder')
        except ValueError:
            check(True, 'old placeholder rejected')
        else:
            check(False, 'old placeholder was accepted')
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    output = Path(manifest['output'])
    catalog = json.loads((output / hud.CATALOG).read_text(encoding='utf-8'))
    rows = {row['name'][7:]: row for row in catalog['weapons']}
    comparisons = []
    for conversion in manifest['spriteConversions']:
        label = conversion['label']
        if '/' not in label:
            continue
        weapon, kind = label.split('/')
        if weapon not in rows or kind not in ('weapon', 'ammo', 'ammo2'):
            continue
        original = Path(conversion['resolvedSource']).read_bytes()
        palette_count = struct.unpack_from('<H', original, 40)[0]
        frame = 42 + palette_count * 3
        width, height = struct.unpack_from('<ii', original, frame + 12)
        check(struct.unpack_from('<i', original, frame)[0] == 0, 'single source frame')
        exported = (output / rows[weapon][kind]).read_bytes()
        x, y, w, h = conversion['cropXYWH']
        check(struct.unpack_from('<HHBB', exported, 12) == (w, h, 32, 0x28), 'TGA dimensions and origin')
        check(conversion['renderMode'] == 1, 'known additive source')
        max_error = 0
        source_colors = set()
        for yy in range(h):
            for xx in range(w):
                index = original[frame + 20 + (y + yy) * width + x + xx]
                rgb = original[42 + index * 3:45 + index * 3]
                b, g, r, a = exported[18 + (yy*w+xx)*4:22 + (yy*w+xx)*4]
                source_colors.add(bytes(rgb))
                max_error = max(max_error, *(abs(c * a / 255 - expected) for c, expected in zip((r,g,b), rgb)))
        check(len(source_colors) > 1, 'source icon must contain a shape: ' + label)
        check(max_error <= .5, 'additive RGB contribution: ' + label)
        comparisons.append(dict(label=label, colors=len(source_colors), maxError=max_error))
    check('balrog11_ammo_v2.tga' in rows['balrog11']['ammo'], 'changed icon has fresh download key')
    check('beamgun_weapon_v2.tga' in rows['beamgun']['weapon'], 'localized card has fresh download key')
    print(json.dumps(dict(checks=checks, failures=0, icons=comparisons), indent=2))


if __name__ == '__main__':
    main()
