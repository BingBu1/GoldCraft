#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a looping blood-moon GIF and matching HUD atlas from authored art."""
import argparse
import cProfile
import hashlib
import json
from pathlib import Path
import re
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent


def identity(path):
    return dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(), bytes=path.stat().st_size)


def prepare(source, output):
    header = ROOT / 'native/csol/scenario/custom/phase_art.hpp'
    text = header.read_text('utf8')
    get = lambda key: int(re.search(r'\b' + key + r'=(\d+);', text)[1])
    size, count, columns, rows, duration = (get(key) for key in
        ('PhaseIconSize', 'NightAnimationFrames', 'NightAnimationColumns', 'NightAnimationRows', 'NightAnimationFrameMs'))
    paths = [re.search(r'\b' + key + r'\[\]="([^"]+)";', text)[1] for key in ('NightAnimationGif', 'NightAnimationAtlas')]
    assert count == columns * rows and count <= 64 and size <= 256
    image = Image.open(source).convert('RGBA')
    alpha = image.getchannel('A')
    if alpha.getextrema() != (0, 255):
        raise ValueError('Source artwork must contain actual transparency')
    bounds = alpha.getbbox()
    moon = image.crop(bounds)
    moon.thumbnail((size - 12, size - 10), Image.Resampling.LANCZOS)
    base = Image.new('RGBA', (size, size))
    base.alpha_composite(moon, ((size - moon.width) // 2, 1))
    pixels=base.load()
    for y in range(size):
        for x in range(size):
            red,green,blue,a=pixels[x,y]
            if a and red>70 and red>green*1.3 and red>blue*1.2:
                pixels[x,y]=(min(255,max(195,int(red*1.2))),min(green,35),min(blue,40),a)
    frames = []
    for frame in range(count):
        # Keep the moon stationary while thick, high-contrast droplets fall.
        # Source sizes remain legible after the HUD scales to ~45px at720p.
        work = base.resize((size * 4, size * 4), Image.Resampling.LANCZOS)
        draw = ImageDraw.Draw(work)
        for x, y, phase in ((22, 41, 0.0), (32, 44, .31), (42, 41, .63)):
            t = (frame / count + phase) % 1.0
            if t < .20:
                length = t / .20 * 6
                draw.rounded_rectangle((x * 4 - 7, y * 4, x * 4 + 7, (y + length) * 4), radius=6, fill=(232, 16, 25, 255))
            else:
                falling = (t - .20) / .80
                dy = y + 5 + 20 * falling * falling
                radius = 2.8 - .2 * falling
                opacity = int(255 * min(1, (1 - falling) * 8))
                draw.ellipse(((x-radius)*4,(dy-radius*1.5)*4,(x+radius)*4,(dy+radius*1.5)*4), fill=(238, 17, 29, opacity))
                draw.ellipse(((x-.8)*4,(dy-1.6)*4,(x+.2)*4,(dy+.2)*4),fill=(255,146,129,opacity))
        frames.append(work.resize((size, size), Image.Resampling.LANCZOS))
    atlas = Image.new('RGBA', (columns * size, rows * size))
    for i, frame in enumerate(frames):
        atlas.alpha_composite(frame, ((i % columns) * size, (i // columns) * size))
    atlas_path, gif_path = output / paths[1], output / paths[0]
    atlas_path.parent.mkdir(parents=True, exist_ok=True)
    # Build one shared palette for all frames; reserve index0 for alpha.
    palette = atlas.convert('RGB').quantize(colors=255)
    raw_palette = palette.getpalette()[:765]
    encoded = []
    for frame in frames:
        indexed = frame.convert('RGB').quantize(palette=palette, dither=Image.Dither.NONE)
        data = indexed.tobytes()
        a = frame.getchannel('A').tobytes()
        result = Image.frombytes('P', frame.size, bytes(value + 1 if alpha >= 100 else 0 for value, alpha in zip(data, a)))
        result.putpalette([0, 0, 0] + raw_palette)
        encoded.append(result)
    encoded[0].save(gif_path, save_all=True, append_images=encoded[1:], duration=duration,
                    loop=0, transparency=0, disposal=2, optimize=False)
    gif = Image.open(gif_path)
    assert gif.n_frames == count and gif.info['loop'] == 0
    durations = []
    decoded = []
    for i in range(count):
        gif.seek(i)
        durations.append(gif.info['duration'])
        decoded.append(gif.convert('RGBA').copy())
    assert durations == [duration] * count
    assert len({hashlib.sha256(frame.tobytes()).hexdigest() for frame in decoded}) == count
    atlas = Image.new('RGBA', (columns * size, rows * size))
    for i, frame in enumerate(decoded):
        atlas.alpha_composite(frame, ((i % columns) * size, (i // columns) * size))
    atlas.save(atlas_path, format='TGA', compression='tga_rle')
    decoded[0].resize((256, 256), Image.Resampling.LANCZOS).save(output / 'moon-preview.png')
    (output / 'receipt.json').write_text(json.dumps(dict(success=True, source=identity(source), contract=identity(header),
        outputs=[identity(gif_path), identity(atlas_path)], frames=count, frameMs=duration, loop=0,
        atlas=[atlas.width, atlas.height], alpha=True, boundary='Authored GIF and RGBA atlas preparation, no game rendering'), indent=2) + '\n', 'utf8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(strict=True), args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build output directory')
    output.mkdir(parents=True)
    cProfile.runctx('prepare(source, output)', globals(), locals(), str(output / 'prepare-cpu.pstats'))
