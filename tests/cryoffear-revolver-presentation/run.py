#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate real six-frame cylinder export against the established SPR decoder."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
PINS = {
    'sprites/cof_revolver_cylinder.spr': '1abc649e0684796805236ebd0442db4e518cfa3c400a710db27a6e09906693a0',
    'sprites/cof_glock_mag_icon.spr': '2638312b229c7a755a0ed6aa43c8b136fbc7988b749098ffa25e6cf8e3fafa35',
    'sprites/cof_p345_mag_icon.spr': '57549f1601a0ec4fc0d80f9485bdc39f32ca262ad376ae3fb68afe27c517cfc3',
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    path = ROOT / 'tools/Prepare-CSOLHud.py'
    spec = importlib.util.spec_from_file_location('hud_export', path)
    hud = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hud)
    output = args.output.absolute()
    hud.no_reparse(output)
    if output.exists() or not output.resolve().is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build output')
    inputs = {path, Path(__file__).resolve()}
    for relative, expected in PINS.items():
        source = args.original / relative
        hud.no_reparse(source)
        if sha(source) != expected:
            raise ValueError('Original resource identity mismatch: ' + relative)
        inputs.add(source)
    before = {str(p): sha(p) for p in sorted(inputs)}
    old_revision = '2f8b102fac1889ae376b2bca9838d8efdb35e90b'
    old_source = subprocess.check_output(['git', 'show', old_revision + ':tools/Prepare-CSOLHud.py'], cwd=ROOT)
    old = {'__file__': str(path), '__name__': 'previous_hud_export'}
    exec(compile(old_source, str(path), 'exec'), old)
    data = (args.original / 'sprites/cof_revolver_cylinder.spr').read_bytes()
    result = hud.decode_sprite_frames(data)
    assert len(result) == 6 and all(frame[:2] == (64, 64) for frame in result)
    checks = 1
    palette_end = 42 + struct.unpack_from('<H', data, 40)[0] * 3
    header = bytearray(data[:palette_end])
    struct.pack_into('<i', header, 28, 1)
    at = palette_end
    frames = []
    output.mkdir(parents=True)
    for index, frame in enumerate(result):
        width, height = struct.unpack_from('<ii', data, at + 12)
        end = at + 20 + width * height
        # The former strict decoder independently consumes each original frame.
        original_frame = bytes(header) + data[at:end]
        assert old['decode_sprite'](original_frame) == frame
        checks += 1
        pixels = frame[2]
        alpha = pixels[3::4]
        assert any(alpha) and len(pixels) == 64 * 64 * 4
        checks += 1
        image = hud.tga(width, height, pixels)
        target = output / ('cylinder-' + str(index) + '.tga')
        target.write_bytes(image)
        frames.append(dict(frame=index, rgbaSha256=hashlib.sha256(pixels).hexdigest(),
                           imageSha256=sha(target), coveredPixels=sum(a > 0 for a in alpha)))
        at = end
    assert at == len(data) and len({r['rgbaSha256'] for r in frames}) == 6
    checks += 1
    for relative in list(PINS)[1:]:
        ordinary = (args.original / relative).read_bytes()
        expected = old['decode_sprite'](ordinary)
        assert hud.decode_sprite(ordinary) == expected
        assert hud.decode_sprite_frames(ordinary) == (expected,)
        checks += 2
    malformed = [data[:cut] for cut in (0, 31, 41, palette_end - 1, palette_end,
                                      palette_end + 19, len(data) - 1)]
    malformed.append(data + b'x')
    for offset, value in ((28, 0), (28, 257), (palette_end, 1), (palette_end + 12, -1)):
        bad = bytearray(data)
        struct.pack_into('<i', bad, offset, value)
        malformed.append(bytes(bad))
    for bad in malformed:
        try:
            hud.decode_sprite_frames(bad)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('Invalid or grouped frames accepted')
    for decoder in (hud.decode_sprite, old['decode_sprite']):
        try:
            decoder(data)
        except ValueError:
            checks += 1
        else:
            raise AssertionError('Single-frame caller silently accepted cylinder animation')
    after = {str(p): sha(p) for p in sorted(inputs)}
    assert before == after
    receipt = dict(success=True, checks=checks, before=before, inputsUnchanged=True,
                   previousDecoderRevision=old_revision, frames=frames, singleFrameOutputUnchanged=True,
                   sourceCopies=0, boundary='Cold original resource conversion only; no runtime HUD, deployment, gameplay or live performance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf-8')
    print(json.dumps(dict(success=True, checks=checks, originalFrames=len(frames))))


if __name__ == '__main__':
    main()
