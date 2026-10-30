"""Prepare original CSOL upgrade movies as lossless RGBA atlases.

Input is the user's extracted common directory. Outputs are runtime assets,
not source publication inputs. No installation or existing resource is changed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save_tga(path, width, height, rgba):
    assert len(rgba) == width * height * 4
    header = bytearray(18)
    header[2] = 2
    struct.pack_into('<HHBB', header, 12, width, height, 32, 0x28)
    bgra = bytearray(rgba)
    bgra[0::4], bgra[2::4] = rgba[2::4], rgba[0::4]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + bgra)


def movie(source, destination, ffmpeg):
    original = source.read_bytes()
    if original[:4] != b'BIKi':
        raise ValueError(f'Expected original Bink i resource: {source}')
    frames, width, height, rate, divisor = (struct.unpack_from('<I', original, n)[0]
                                         for n in (8,20,24,28,32))
    if (frames,width,height,rate,divisor) != (53,382,9,30,1):
        raise ValueError(f'Original movie contract changed: {source}')
    result = subprocess.run([ffmpeg, '-v','error','-threads','1','-i',str(source),
        '-threads','1','-f','rawvideo','-pix_fmt','rgba','-'],check=True,capture_output=True)
    rgba = result.stdout
    if len(rgba) != frames * width * height * 4:
        raise ValueError('Decoded frame count/dimensions changed')
    # One replicated pixel surrounds every source frame so bilinear filtering
    # cannot mix adjacent frames. Original alpha bytes are retained.
    atlas_width, stride = width+2, height+2
    atlas = bytearray(atlas_width * stride * frames * 4)
    for frame in range(frames):
        for y in range(stride):
            source_y = max(0,min(height-1,y-1))
            row = rgba[(frame*height+source_y)*width*4:(frame*height+source_y+1)*width*4]
            target = (frame*stride+y)*atlas_width*4
            atlas[target:target+atlas_width*4] = row[:4]+row+row[-4:]
    save_tga(destination,atlas_width,stride*frames,atlas)
    return dict(source=str(source),sourceSha256=sha(original),frames=frames,
        frameSize=[width,height],rate=[rate,divisor],decodedRGBA=sha(rgba),
        destination=str(destination),sha256=sha(destination.read_bytes()),
        alphaRange=[min(rgba[3::4]),max(rgba[3::4])])


def digits(source, destination):
    data=source.read_bytes()
    ident,version,kind,format_=struct.unpack_from('<4i',data)
    width,height,frames=struct.unpack_from('<3i',data,20)
    if (ident,version,format_,width,height,frames)!=(0x50534449,2,1,256,256,1):
        raise ValueError('Original additive 640hud8 sprite contract changed')
    count=struct.unpack_from('<H',data,40)[0]
    if count!=256:raise ValueError('Unexpected HUD palette')
    palette=data[42:42+count*3];at=42+count*3
    frame_kind,x,y,w,h=struct.unpack_from('<5i',data,at)
    if (frame_kind,w,h)!=(0,256,256):raise ValueError('Unexpected HUD frame')
    pixels=data[at+20:at+20+w*h]
    if len(pixels)!=w*h:raise ValueError('Truncated HUD frame')
    rgba=bytearray()
    # sprites/hud.txt dollarNum_0_new..9_new: x80+13*n,y71,w13,h18.
    for row in range(71,89):
        for col in range(80,210):
            index=pixels[row*w+col]
            rgba.extend(palette[index*3:index*3+3]);rgba.append(255)
    save_tga(destination,130,18,rgba)
    return dict(source=str(source),sourceSha256=sha(data),destination=str(destination),
        sha256=sha(destination.read_bytes()),blend='additive',crop=[80,71,130,18])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--ffmpeg',default='ffmpeg')
    args=parser.parse_args()
    common=args.common.resolve(strict=True)
    output=args.output.resolve()
    workspace=Path(__file__).resolve().parents[1]
    if not output.is_relative_to(workspace/'build'):
        raise ValueError('Output must be an isolated directory under workspace/build')
    if output.exists():raise FileExistsError(f'Output already exists: {output}')
    output.mkdir(parents=True)
    generated=[]
    for part in ('top','bottom'):
        generated.append(movie(common/f'resource/ZBSVER2/levelup_line_{part}.bik',
            output/f'resource/csol/scenario/levelup_line_{part}.tga',args.ffmpeg))
    generated.append(digits(common/'sprites/640hud8.spr',output/'resource/csol/scenario/level_digits.tga'))
    resources=['resource/zombiez/zmode_level_bg.tga','resource/zombiez/zmode_level_num.tga',
        'resource/zombiez/zmode_level_bar.tga','resource/zombiez/zmode_level_bar_bg.tga',
        'resource/hud/zbs/TextBg_Left.tga','resource/hud/zbs/TextBg_Center.tga',
        'resource/hud/zbs/TextBg_Right.tga','resource/font/df_gb_y9.ttf','sound/events/Lvup.wav',
        'sprites/640hud8.spr']
    # Read the named, limited server download set rather than copying all 55
    # icon families and spending precache entries for unimplemented abilities.
    header=(workspace/'native/csol/scenario_abilities.hpp').read_text(encoding='utf-8')
    names=dict(re.findall(r'char (\w+)\[\]="([^"]+)"',header))
    number_block=header.split('NumberImages{{',1)[1].split('}};',1)[0]
    numbers=re.findall(r'"([^"]+\.tga)"',number_block)
    download_block=header.split('DownloadResources{{',1)[1].split('}};',1)[0]
    for token in download_block.split(','):
        token=token.strip()
        if token.startswith('"'):path=token.strip('"')
        elif token.startswith('NumberImages['):path=numbers[int(token[13:-1])]
        else:path=names[token]
        data=(common/path).read_bytes()
        if struct.unpack_from('<HH',data,12)!=(77,75):
            raise ValueError(f'Original ability image dimensions changed: {path}')
        resources.append(path)
    copied=[]
    for relative in resources:
        src=common/relative;dst=output/relative
        dst.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(src,dst)
        digest=sha(src.read_bytes())
        if sha(dst.read_bytes())!=digest:raise ValueError(f'Copy mismatch: {relative}')
        copied.append(dict(path=relative,sha256=digest))
    manifest=dict(generated=generated,copied=copied)
    (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(generated=len(generated),copied=len(copied),output=str(output)),ensure_ascii=False))


if __name__=='__main__':main()
