# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify original TMP artwork, menu closure, and byte-identical older firearm output."""
import argparse,hashlib,importlib.util,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('original','baseline','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();out=a.output.absolute()
 spec=importlib.util.spec_from_file_location('author',ROOT/'tools/Prepare-CryOfFearPresentation.py')
 tool=importlib.util.module_from_spec(spec);spec.loader.exec_module(tool)
 tool.hud.no_reparse(out)
 if out.exists() or not out.resolve().is_relative_to(ROOT/'build'):raise ValueError('Fresh build output required')
 out.mkdir(parents=True);r=tool.prepare(a.original,a.baseline,out/'tmp','tmp');checks=0
 for row in r['files']:
  path=out/'tmp'/row['path'];assert sha(path)==row['sha256'] and path.stat().st_size==row['bytes'];checks+=1
 assert (r['originalId'],r['hostId'],r['clip'],r['reserve'],r['price'])==(14,900,30,300,2000);checks+=1
 menu_path=next(x['path'] for x in r['files'] if '/buymenu_' in x['path'])
 before_hud=json.loads((a.baseline/tool.hud.CATALOG).read_bytes());after_hud=json.loads((out/'tmp'/tool.hud.CATALOG).read_bytes())
 assert after_hud['weapons'][:-1]==before_hud['weapons'];checks+=1
 row=after_hud['weapons'][-1];assert row['name']=='weapon_cof_tmp' and row['title']=='客串武器·MP9' and 'ammoFrames' not in row;checks+=1
 before_menu=json.loads((a.baseline/menu_path).read_bytes());after_menu=json.loads((out/'tmp'/menu_path).read_bytes())
 assert after_menu['images'][:-1]==before_menu['images'] and after_menu['weaponInfo']['items'][:-1]==before_menu['weaponInfo']['items'];checks+=1
 image=after_menu['images'][-1];info=after_menu['weaponInfo']['items'][-1]
 assert image['id']==info['id']==900 and image['width']==170 and image['height']==80 and info['name']=='客串武器·MP9' and '冲锋枪' in info['description'];checks+=1
 actual=(out/'tmp/sprites/guest/cof/tmp_ammo.spr').read_bytes();expected=(a.original/'sprites/cof_tmp_mag_icon.spr').read_bytes()
 assert actual==expected;checks+=1
 w,h,ammo,_=tool.hud.decode_sprite(expected);aw,ah,modern=tool.tga32((out/'tmp'/row['ammo']).read_bytes())
 assert (w,h)==(aw,ah)==(32,32) and ammo==modern;checks+=1
 card=(out/'tmp/gfx/csol/buy/cof_tmp.tga').read_bytes();w,h,pixels=tool.tga32(card)
 sw,sh,fallback,_=tool.hud.decode_sprite((out/'tmp/sprites/guest/cof/ctmp.spr').read_bytes())
 assert (w,h)==(sw,sh)==(170,80);checks+=1
 for i in range(w*h):
  assert (fallback[4*i+3]==0)==(pixels[4*i+3]<128);checks+=1
  if fallback[4*i+3]:assert max(abs(x-y) for x,y in zip(fallback[4*i:4*i+3],pixels[4*i:4*i+3]))<=25;checks+=1
 # A data-only baseline removes four prior guest rows so both old and new
 # authors operate on exactly the same reviewed input. No source tree copies.
 base=out/'old-family-baseline'
 for n,v in ((tool.hud.CATALOG,before_hud),(menu_path,before_menu)):
  if n==tool.hud.CATALOG:v['weapons']=[x for x in v['weapons'] if not x['name'].startswith('weapon_cof_')]
  else:
   v['images']=[x for x in v['images'] if x['id'] not in (896,897,898,899)]
   v['weaponInfo']['items']=[x for x in v['weaponInfo']['items'] if x['id'] not in (896,897,898,899)]
  path=base/n;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(v,ensure_ascii=False),encoding='utf8')
 old_blob=subprocess.check_output(['git','show','df0c438:tools/Prepare-CryOfFearPresentation.py'],cwd=ROOT)
 old={'__file__':str(ROOT/'tools/Prepare-CryOfFearPresentation.py'),'__name__':'old_author'}
 exec(compile(old_blob,str(ROOT/'tools/Prepare-CryOfFearPresentation.py'),'exec'),old)
 for name in ('glock','p345','revolver','vp70'):
  previous=old['prepare'](a.original,base,out/(name+'-old'),name)
  current=tool.prepare(a.original,base,out/(name+'-new'),name)
  assert previous['files']==current['files'];checks+=1
  for x in current['files']:assert (out/(name+'-old')/x['path']).read_bytes()==(out/(name+'-new')/x['path']).read_bytes();checks+=1
 # A small complete native sprite closure for the actual SDK cold test;
 # this does not claim the full optional CSO HUD download closure is present.
 closure=out/'native-closure'
 for name,selection in (('glock','cglock'),('p345','cp345'),('revolver','crever'),('vp70','cvp70'),('tmp','ctmp')):
  for rel in ('sprites/weapon_cof_'+name+'.txt','sprites/guest/cof/'+selection+'.spr','sprites/guest/cof/'+name+'_ammo.spr'):
   src=(out/'tmp'/rel) if name=='tmp' else a.baseline/rel
   tool.hud.no_reparse(src);dest=closure/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dest)
 result=dict(success=True,checks=checks,originalTMPPins=tool.TMP_PINS,originalInputs=r['inputs'],
  oldAuthorSha256=hashlib.sha256(old_blob).hexdigest(),toolSha256=sha(Path(tool.__file__)),
  testSha256=sha(Path(__file__)),files=r['files'],olderFourOutputsUnchanged=True,
  deployed=False,gameplay=False,fullHudClosure=False,performance=False,sourceCopies=0)
 (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(success=True,checks=checks,files=len(r['files']),olderFourOutputsUnchanged=True)))
if __name__=='__main__':main()
