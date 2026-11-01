"""Draw the original BingSang ice-cube mark as a multi-resolution Windows icon."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw


def render(size):
    scale = size * 4 / 256
    image = Image.new('RGBA', (size * 4, size * 4))
    draw = ImageDraw.Draw(image)

    def polygon(points, color):
        draw.polygon([(round(x * scale), round(y * scale)) for x, y in points], fill=color)

    # Wide, uncluttered silhouette survives 16px Explorer and taskbar rendering.
    polygon([(128, 12), (234, 69), (234, 183), (128, 244), (22, 183), (22, 69)], '#103B50')
    polygon([(128, 22), (224, 74), (128, 128), (32, 74)], '#B4F0F4')
    polygon([(32, 83), (124, 135), (124, 231), (32, 178)], '#50B6D0')
    polygon([(133, 135), (224, 83), (224, 178), (133, 231)], '#267791')
    polygon([(54, 98), (82, 114), (82, 149), (54, 133)], '#DCFFFF')
    polygon([(155, 151), (200, 126), (200, 191), (155, 217)], '#0C3447')
    if size >= 32:
        polygon([(128, 31), (207, 74), (191, 82), (112, 39)], '#E2FFFF')
        polygon([(38, 159), (118, 204), (118, 219), (38, 174)], '#3D9DB9')
    return image.resize((size, size), Image.Resampling.LANCZOS)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    frames = [render(size) for size in sizes]
    frames[-1].save(args.output, format='ICO', sizes=[(s, s) for s in sizes], append_images=frames[:-1])
    frames[-1].save(args.output.with_suffix('.png'))
    with Image.open(args.output) as icon:
        assert icon.ico.sizes() == {(s, s) for s in sizes}
    print('Created BingSang icon: 9 sizes, transparent RGBA.')
