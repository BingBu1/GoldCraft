#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export original CSOL HUD resources into a fresh workspace build directory.

Run with --source <extracted-common> --output <fresh-build-directory>.
This prepares resources and an input/output audit only; it never installs,
downloads, compiles, edits the source assets, or adds game assets to Git.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import re
import stat
import struct


ROOT = Path(__file__).resolve().parents[1]
RESOURCE = 'resource/csol/hud/v1/'
CATALOG = 'models/csol/hud_v1.json'
BACKGROUNDS = (
    'hud_character_bg_new_top', 'hud_character_bg_new_bottom',
    'hud_weapon_bg_top', 'hud_weapon_bg_bottom', 'hud_weapon_off_bg',
)
WEAPON_NAMES = {
    'elites': 'elite', 'fn57': 'fiveseven', 'glock': 'glock18', 'mp5': 'mp5navy',
    'hegren': 'hegrenade', 'flash': 'flashbang', 'sgren': 'smokegrenade',
}
TITLE_KEYS = {
    'usp': 'usp45', 'deagle': 'deserteagle', 'elite': 'elites',
    'mp5navy': 'mp5', 'hegrenade': 'he_grenade', 'smokegrenade': 'smoke_grenade',
}
VERIFIED_COMMON_WEAPONS = frozenset(('tknifeex2', 'cannon', 'k3', 'beamgun', 'beamgunle'))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def no_reparse(path):
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            info = item.lstat()
            if item.is_symlink() or getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError(f'Reparse points are not supported: {item}')


def relative(name):
    path = PurePosixPath(name)
    if not name or path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError(f'Invalid relative resource path: {name}')
    return path


def text_bytes(data):
    return data.decode('utf-16' if data[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig')


def tokens(data):
    """Read Valve quoted keys; comments/bare separator lines are not keys."""
    values = []
    for token in re.finditer(r'//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])', text_bytes(data)):
        quoted, brace = token.groups()
        if quoted is not None:
            values.append(re.sub(r'\\([nrt"\\])', lambda m: {'n': '\n', 'r': '\r', 't': '\t'}.get(m[1], m[1]), quoted))
        elif brace:
            values.append(brace)
    index = 0

    def block(nested=False):
        nonlocal index
        result = {}
        while index < len(values):
            key = values[index]
            index += 1
            if key == '}':
                if nested:
                    return result
                raise ValueError('Unexpected localization closing brace')
            if key == '{' or index >= len(values):
                raise ValueError('Incomplete localization key/value')
            value = values[index]
            index += 1
            if value == '}':
                raise ValueError('Missing localization value')
            # CSO resolves token names without case sensitivity. Later entries
            # replace earlier ones, as in the shipped localization loader.
            result[key.lower()] = block(True) if value == '{' else value
        if nested:
            raise ValueError('Unterminated localization block')
        return result

    root = block()
    if len(root) != 1:
        raise ValueError('Expected one localization root')
    result = next(iter(root.values())).get('tokens')
    if not isinstance(result, dict):
        raise ValueError('Missing localization Tokens block')
    return result


def sprite_rows(data, wanted):
    rows = {}
    # hud.txt includes Korean comments in a legacy code page. The actual
    # grammar and filenames are ASCII; discard comments before decoding.
    for raw in data.splitlines():
        line = raw.split(b'//', 1)[0].decode('ascii')
        fields = line.split()
        if not fields or len(fields) == 1:
            continue
        if fields[0].lower() not in wanted:
            continue
        if len(fields) != 7:
            raise ValueError(f'Invalid sprite list row: {line}')
        kind, resolution, atlas, x, y, width, height = fields
        row = dict(atlas=f'sprites/{atlas}.spr', x=int(x), y=int(y), width=int(width), height=int(height))
        relative(row['atlas'])
        key = (kind.lower(), int(resolution))
        if key in rows and rows[key] != row:
            raise ValueError(f'Ambiguous sprite list row: {key}')
        rows[key] = row
    return rows


def decode_sprite(data):
    """Decode a version-2, one-frame HUD SPR without guessing grouped frames."""
    if len(data) < 42 or data[:4] != b'IDSP' or struct.unpack_from('<i', data, 4)[0] != 2:
        raise ValueError('Expected an IDSP version 2 sprite')
    mode = struct.unpack_from('<i', data, 12)[0]
    frames = struct.unpack_from('<i', data, 28)[0]
    count = struct.unpack_from('<H', data, 40)[0]
    if mode not in (0, 1, 2, 3) or frames != 1 or not 1 <= count <= 256:
        raise ValueError(f'Unsupported HUD sprite: mode={mode}, frames={frames}, palette={count}')
    at = 42 + count * 3
    if len(data) < at + 20 or struct.unpack_from('<i', data, at)[0] != 0:
        raise ValueError('Truncated or grouped HUD sprite')
    width, height = struct.unpack_from('<ii', data, at + 12)
    if not 1 <= width <= 4096 or not 1 <= height <= 4096 or len(data) != at + 20 + width * height:
        raise ValueError('Invalid HUD sprite dimensions/pixel count')
    palette = [tuple(data[42 + i * 3:45 + i * 3]) for i in range(count)]
    indices = data[at + 20:]
    if max(indices) >= count or mode == 2 and count != 256:
        raise ValueError('HUD sprite palette index is unavailable')
    colors = []
    for i, color in enumerate(palette):
        if mode == 1:
            # Additive sprites contain black background and intensity in RGB.
            # Export straight RGBA coverage: black is transparent, gray edges
            # become white+alpha, colored edges retain normalized chroma.
            # alpha*RGB recreates the original additive source contribution
            # (within byte rounding). Source-over still attenuates background;
            # it is not mathematically identical to additive on every backdrop.
            alpha = max(color)
            rgb = tuple((value * 255 + alpha // 2) // alpha for value in color) if alpha else (0, 0, 0)
        elif mode == 2:
            rgb, alpha = palette[255], i
        elif mode == 3:
            rgb, alpha = color, 0 if i == 255 else 255
        else:
            rgb, alpha = color, 255
        colors.append(bytes((*rgb, alpha)))
    rgba = b''.join(colors[index] for index in indices)
    return width, height, rgba, dict(renderMode=mode, paletteEntries=count,
        dimensions=[width, height], grayscale=all(r == g == b for r, g, b in palette),
        conversion={0: 'opaque palette RGB', 1: 'straight RGBA: alpha=max(R,G,B), RGB=round(255*sourceRGB/alpha); black alpha=0',
                    2: 'palette[255] RGB, index as alpha', 3: 'palette RGB, index255 transparent'}[mode])


def tga(width, height, rgba):
    if len(rgba) != width * height * 4:
        raise ValueError('RGBA image length mismatch')
    header = bytearray(18)
    header[2] = 2
    struct.pack_into('<HHBB', header, 12, width, height, 32, 0x28)
    bgra = bytearray(rgba)
    bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
    return bytes(header + bgra)


def catalog_weapons(data):
    match = re.search(r'constexpr Item items\[\]\s*\{(.*?)\n\};', data.decode('utf-8-sig'), re.S)
    if not match:
        raise ValueError('Missing buy::Catalog items declaration')
    rows = re.findall(r'\{\s*(\d+),\s*(\d+),\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*(\d+)\s*\}', match[1])
    if len(rows) != match[1].count('{'):
        raise ValueError('Unsupported buy::Catalog row syntax')
    names = []
    for ident, category, alias, title, icon, price in rows:
        if int(category) in (6, 7) or alias in ('vest', 'vesthelm'):
            continue
        name = WEAPON_NAMES.get(alias, alias)
        if not re.fullmatch('[a-z0-9_]+', name):
            raise ValueError(f'Invalid weapon classname: {alias}')
        # Purchase IDs (e.g. 1003 for HE) are not gameplay weapon IDs.
        names.append(name)
    return sorted(set(names + ['knife', 'hegrenade', 'flashbang', 'smokegrenade', 'c4']))


def prepare(source, output, stock_source=None):
    source, output = source.absolute(), output.absolute()
    no_reparse(source)
    no_reparse(output)
    source, output = source.resolve(), output.resolve()
    build = (ROOT / 'build').resolve()
    audit_path = output.with_name(output.name + '-manifest.json')
    if not source.is_dir() or not output.is_relative_to(build) or output == build or output.exists() or audit_path.exists():
        raise ValueError('Require an existing common source and a fresh directory below workspace build')
    if output.is_relative_to(source) or source.is_relative_to(output):
        raise ValueError('Source and output must be isolated')
    if stock_source is not None:
        stock_source = stock_source.absolute()
        no_reparse(stock_source)
        stock_source = stock_source.resolve()
        if not stock_source.is_dir() or not stock_source.is_relative_to((ROOT/'sandbox').resolve()):
            raise ValueError('Stock resources must come from an isolated workspace sandbox')
    no_reparse(audit_path)
    generated, inputs, conversions, missing, missing_weapons = {}, {}, [], [], []
    sprites = {}

    def read(path):
        path = Path(path)
        no_reparse(path)
        data = path.read_bytes()
        item = dict(path=str(path.resolve()), bytes=len(data), sha256=sha(data))
        key = str(path.resolve()).casefold()
        if key in inputs and inputs[key] != item:
            raise ValueError(f'Input changed while preparing: {path}')
        inputs[key] = item
        return data

    def common(name):
        return read(source / relative(name))

    def add(name, data):
        relative(name)
        if name in generated and generated[name] != data:
            raise ValueError(f'Output conflict: {name}')
        generated[name] = data
        return name

    def crop(row, label, atlas_root=source):
        name = row['atlas']
        path = atlas_root / relative(name)
        if path not in sprites:
            sprites[path] = decode_sprite(read(path))
        sw, sh, rgba, metadata = sprites[path]
        x, y, width, height = (row[key] for key in ('x', 'y', 'width', 'height'))
        if min(x, y) < 0 or min(width, height) <= 0 or x + width > sw or y + height > sh:
            raise ValueError(f'Sprite crop outside atlas: {label}')
        pixels = b''.join(rgba[((y + j) * sw + x) * 4:((y + j) * sw + x + width) * 4] for j in range(height))
        conversions.append(dict(label=label, source=name, sourceRoot=str(atlas_root), cropXYWH=[x,y,width,height], **metadata,
            alphaMin=min(pixels[3::4]), alphaMax=max(pixels[3::4]), transparentPixels=pixels[3::4].count(0)))
        return pixels

    translations = {}
    for name in ('resource/cstrike_chn.txt', 'resource/cso_chn.txt'):
        translations.update(tokens(common(name)))
    names = catalog_weapons(read(ROOT / 'native/csol/buy_menu.cpp'))
    weapons = []
    title_sources = {}
    for name in names:
        title_key = 'cso_' + TITLE_KEYS.get(name, name)
        title = translations.get(title_key)
        if not isinstance(title, str) or not title or '\ufffd' in title:
            raise ValueError(f'Original weapon title missing or invalid: {name}, {title_key}')
        title_sources[name] = title_key
        # The merged common directory contains old stock weapon tables paired
        # with newer CSO atlases (e.g. stock C4 points to a knife). Never export
        # those mismatched pairs. Stock tables and atlases must come together
        # from the isolated CS client; common remains the title authority.
        atlas_root = source if name in VERIFIED_COMMON_WEAPONS else stock_source
        if atlas_root is None:
            missing_weapons.append(dict(name='weapon_'+name,
                reason='No isolated stock table/atlas source supplied; mismatched common stock art is not exported'))
            continue
        rows = sprite_rows(read(atlas_root/f'sprites/weapon_{name}.txt'), {'weapon','ammo','ammo2'})
        item = dict(name='weapon_' + name, title=title)
        for kind, dimensions in (('weapon', (170,45)), ('ammo', (24,24)), ('ammo2', (24,24))):
            row = rows.get((kind, 640))
            if row is None:
                if kind == 'weapon':
                    raise ValueError(f'Original weapon silhouette missing: {name}')
                item[kind] = ''
                continue
            if (row['width'], row['height']) != dimensions:
                raise ValueError(f'Original sprite dimensions changed: {name}/{kind}')
            pixels = crop(row, name + '/' + kind, atlas_root)
            item[kind] = add(RESOURCE + name + '_' + kind + '.tga', tga(*dimensions, pixels))
        weapons.append(item)

    rows = sprite_rows(common('sprites/hud.txt'),
        {*(f'number_{index}_new' for index in range(10)), 'cross_new', 'suit_full_new', 'infinite'})
    digits = []
    for index in range(10):
        row = rows.get((f'number_{index}_new', 640))
        expected = dict(atlas='sprites/640hud8.spr', x=36+17*index, y=89, width=17, height=22)
        if row != expected:
            raise ValueError(f'Original new-HUD digit contract changed: {index}')
        digits.append(crop(row, f'number_{index}_new'))
    digit_pixels = b''.join(digit[j*17*4:(j+1)*17*4] for j in range(22) for digit in digits)
    add(RESOURCE + 'digits.tga', tga(170,22,digit_pixels))
    for key, filename, x, y in (('cross_new','health',204,0), ('suit_full_new','armor',187,144)):
        row = rows.get((key,640))
        if row != dict(atlas='sprites/640hud8.spr', x=x, y=y, width=16, height=16):
            raise ValueError(f'Original new-HUD icon contract changed: {key}')
        add(RESOURCE + filename + '.tga', tga(16,16,crop(row,key)))
    row = rows.get(('infinite',640))
    if row != dict(atlas='sprites/640hud12.spr', x=172, y=231, width=36, height=24):
        raise ValueError('Original infinite-ammo icon contract changed')
    add(RESOURCE + 'infinite.tga', tga(36,24,crop(row,'infinite')))

    originals = []
    for name in BACKGROUNDS:
        origin = 'resource/Hud/' + name + '.tga'
        destination = RESOURCE + name + '.tga'
        add(destination, common(origin))
        originals.append(dict(source=origin, output=destination, conversion='verbatim'))
    font = 'resource/font/df_gb_y9.ttf'
    add(font, common(font))
    originals.append(dict(source=font, output=font, conversion='verbatim'))

    characters = json.loads(read(ROOT / 'native/csol/config/characters.json'))['characters']
    portraits, seen = [], set()
    for character in characters:
        ident = character['id']
        if not isinstance(ident,int) or ident <= 0 or ident in seen:
            raise ValueError('Character IDs must be unique positive integers')
        seen.add(ident)
        stem = PurePosixPath(character['sourceModel']).stem
        if not re.fullmatch('[A-Za-z0-9_]+', stem):
            raise ValueError(f'Invalid source model stem: {stem}')
        origin = f'resource/Hud/Portrait/hud_{stem}.tga'
        if not (source / origin).is_file():
            missing.append(dict(id=ident, sourceModel=character['sourceModel'], expected=origin,
                reason='Original HUD portrait is absent; no menu portrait substituted'))
            continue
        destination = RESOURCE + f'portrait_{ident}.tga'
        add(destination, common(origin))
        originals.append(dict(source=origin, output=destination, conversion='verbatim'))
        portraits.append(dict(id=ident,path=destination))
    catalog = dict(version=1,weapons=weapons,portraits=portraits)
    add(CATALOG, (json.dumps(catalog,ensure_ascii=False,separators=(',',':'))+'\n').encode('utf-8'))
    read(Path(__file__).resolve())
    # Finish all decoding and input checks before creating any output.
    for item in inputs.values():
        current = Path(item['path']).read_bytes()
        if len(current) != item['bytes'] or sha(current) != item['sha256']:
            raise ValueError(f'Input changed before export: {item["path"]}')
    output.mkdir(parents=True,exist_ok=False)
    files = []
    for name,data in sorted(generated.items()):
        path = output / name
        path.parent.mkdir(parents=True,exist_ok=True)
        with path.open('xb') as stream:
            stream.write(data)
        if path.stat().st_nlink != 1 or sha(path.read_bytes()) != sha(data):
            raise ValueError(f'Output verification failed: {name}')
        files.append(dict(path=name,bytes=len(data),sha256=sha(data)))
    audit = dict(version=1,source=str(source),stockSource=str(stock_source) if stock_source else None,
        output=str(output),inputs=list(inputs.values()),files=files,
        originalCopies=originals,spriteConversions=conversions,titleKeys=title_sources,missingPortraits=missing,
        missingWeapons=missing_weapons,
        summary=dict(weapons=len(weapons),portraits=len(portraits),missingPortraits=len(missing),files=len(files),
                     bytes=sum(item['bytes'] for item in files)),
        transparency='SPR additive exported as straight coverage RGBA. Drawing with source-alpha additive blend preserves '
                     'the original source RGB contribution within 0.5 byte/channel before framebuffer quantization '
                     '(at most 1 byte after rounding). Non-additive source-over fallback attenuates the background '
                     'and is not pixel-equivalent to the original additive path. Original TGA bytes/alpha are unchanged.',
        deployed=False,compiled=False)
    with audit_path.open('x',encoding='utf-8',newline='\n') as stream:
        json.dump(audit,stream,ensure_ascii=False,indent=2)
        stream.write('\n')
    print(json.dumps(dict(**audit['summary'],manifest=str(audit_path),catalog=str(output/CATALOG))))
    return audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True,help='Previously extracted CSOL common directory')
    parser.add_argument('--output',type=Path,required=True,help='Fresh directory under this project build/')
    parser.add_argument('--stock-source',type=Path,help='Isolated CS cstrike directory containing matching stock sprite tables and atlases')
    args = parser.parse_args()
    prepare(args.source,args.output,args.stock_source)


if __name__ == '__main__':
    main()
