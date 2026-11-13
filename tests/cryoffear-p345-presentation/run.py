#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify actual P345 original-art conversion and unchanged older Glock output."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('original','baseline','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Fresh workspace output required')
    spec=importlib.util.spec_from_file_location('presentation',ROOT/'tools/Prepare-CryOfFearPresentation.py')
    tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
    tool.hud.no_reparse(out);out.mkdir(parents=True)
    inputs={Path(__file__).resolve(),Path(tool.__file__),ROOT/'tools/Prepare-CSOLHud.py',ROOT/'native/guest/cof/catalog.hpp',
        ROOT/'native/csol/buy_menu.cpp',ROOT/'native/csol/buy_menu.hpp'}
    inputs.update(a.original/p for p in (*tool.PINS,*tool.P345_PINS))
    before={str(p):sha(p) for p in sorted(inputs)}
    receipt=tool.prepare(a.original,a.baseline,out/'p345','p345');checks=0
    for row in receipt['files']:
        path=out/'p345'/row['path'];assert sha(path)==row['sha256'] and path.stat().st_size==row['bytes'];checks+=1
    card=(out/'p345/gfx/csol/buy/cof_p345.tga').read_bytes()
    w,h,pixels=tool.tga32(card);assert (w,h)==(170,80);checks+=1
    native=(out/'p345/sprites/guest/cof/cp345.spr').read_bytes()
    nw,nh,decoded,_=tool.hud.decode_sprite(native);assert (nw,nh)==(170,80);checks+=1
    for i in range(w*h):
        assert (decoded[i*4+3]==0)==(pixels[i*4+3]<128);checks+=1
        if decoded[i*4+3]:assert max(abs(a-b) for a,b in zip(pixels[i*4:i*4+3],decoded[i*4:i*4+3]))<=25;checks+=1
    assert (out/'p345/sprites/guest/cof/p345_ammo.spr').read_bytes()==(a.original/'sprites/cof_p345_mag_icon.spr').read_bytes();checks+=1
    # Two true-color pixels prove BGR conversion and top/bottom image origin.
    tiny=bytearray(18);tiny[2]=2;tiny[16]=32;struct.pack_into('<HH',tiny,12,1,2)
    tiny.extend(bytes((0,0,255,128,255,0,0,64)))
    assert tool.tga32(tiny)[2]==bytes((0,0,255,64,255,0,0,128));checks+=1
    tiny[17]=0x20;assert tool.tga32(tiny)[2]==bytes((255,0,0,128,0,0,255,64));checks+=1
    for bad in (bytes(tiny[:-1]),bytes(tiny)+b'x',bytes(tiny)+bytes(7)+b'1'+b'TRUEVISION-XFILE.\0'):
        try:tool.tga32(bad)
        except ValueError:checks+=1
        else:raise AssertionError('Malformed original-art image accepted')
    # Resource baseline only: two generated JSON inputs, never a source copy.
    baseline=out/'glock-baseline';menu_path=next(row['path'] for row in receipt['files'] if '/buymenu_' in row['path'])
    for name in (tool.hud.CATALOG,menu_path):
        value=json.loads((a.baseline/name).read_bytes())
        if name==tool.hud.CATALOG:value['weapons']=[r for r in value['weapons'] if r['name'] not in ('weapon_cof_glock','weapon_cof_p345')]
        else:
            value['images']=[r for r in value['images'] if r['id'] not in (896,897)]
            value['weaponInfo']['items']=[r for r in value['weaponInfo']['items'] if r['id'] not in (896,897)]
        path=baseline/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value,ensure_ascii=False),'utf8')
    old_blob=subprocess.check_output(['git','show','680f6d8:tools/Prepare-CryOfFearPresentation.py'],cwd=ROOT)
    old={'__file__':str(ROOT/'tools/Prepare-CryOfFearPresentation.py'),'__name__':'old_presentation'}
    exec(compile(old_blob,str(ROOT/'tools/Prepare-CryOfFearPresentation.py'),'exec'),old)
    old_receipt=old['prepare'](a.original,baseline,out/'glock-old')
    new_receipt=tool.prepare(a.original,baseline,out/'glock-new')
    assert old_receipt['files']==new_receipt['files'];checks+=1
    for row in old_receipt['files']:
        assert (out/'glock-old'/row['path']).read_bytes()==(out/'glock-new'/row['path']).read_bytes();checks+=1
    assert before=={str(p):sha(p) for p in sorted(inputs)}
    result=dict(success=True,checks=checks,before=before,inputsUnchanged=True,files=receipt['files'],glockOutputBytesUnchanged=True,
        authoring='Full RGBA original-art modern HUD/card; separate216-color/index255 stock fallback',deployed=False,gameplay=False,performance=False)
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n','utf8');print(json.dumps(dict(success=True,checks=checks,files=len(receipt['files']),glockOutputBytesUnchanged=True)))

if __name__=='__main__':main()
