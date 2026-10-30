"""Export original CSOL buy presentation from the consolidated common folder."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('class_menu',Path(__file__).with_name('Prepare-CSOLCharacterMenu.py'))
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    source,output=a.source.resolve(),a.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace build output')
    generated={};inputs={}
    def read(name):
        data=(source/name).read_bytes();inputs[name]=hashlib.sha256(data).hexdigest();return data
    def kv(name):read(name);return next(iter(module.keyvalues(source/name).values()))
    scheme=kv('resource/clientscheme.res');frame=kv('resource/UI/CSO_BuyMenu.res')['buymenu']
    layout=kv('resource/UI/CSO_BuySubMenu_ver7.res')
    text={}
    for n in ['resource/cstrike_chn.txt','resource/cso_chn.txt']:text.update(kv(n)['tokens'])
    def texture(name,alias):
        data=read(name+'.tga');path='gfx/csol/buy/'+alias+'.tga';generated[path]=data;w,h=struct.unpack_from('<HH',data,12);return dict(path=path,width=w,height=h)
    def rect(name):return [int(layout[name].get(k,0)) for k in ['xpos','ypos','wide','tall']]
    theme=scheme['basesettings'];edges=['topleft','topcenter','topright','middleleft','middlecenter','middleright','bottomleft','bottomcenter','bottomright']
    images=[]
    catalog=(ROOT/'native/csol/buy_menu.cpp').read_text(encoding='utf-8')
    for ident,icon in re.findall(r'\{(\d+),\d+,"[^"]*","[^"]*","([^"]*)",\d+\}',catalog):
        if not icon:continue
        name='gfx/vgui/basket/'+icon
        if not (source/(name+'.tga')).is_file():name='gfx/vgui/'+icon
        if (source/(name+'.tga')).is_file():images.append(dict(id=int(ident),**texture(name,'w'+ident)))
    for i,price in [(144,4100),(220,30)]:
        token='cstrike_'+('cannon' if i==144 else 'tknifeex2')+'price';assert int(re.search(r'\d+',text[token])[0])==price
    for n in ['beamgun','beamgunle']:assert json.loads(read('weapons/weapon_'+n+'.json'))['price']==6100
    # ver7 declares the detail/rebuy/account controls. Its runtime-created list
    # and eight favorites are measured from the supplied modern CSOL screenshot,
    # in the same 606-wide logical space; do not merge unrelated ver3 controls.
    menu=dict(version=2,width=int(frame['wide']),height=440,title=text[frame['title'][1:].lower()],font=scheme['fonts']['default']['1']['name'],
              info=rect('iteminfo'),slots=[[14,52+i*25,200,25] for i in range(10)],previous=[14,306,72,18],next=[142,306,72,18],quit=[14,277,200,25],account=rect('account_num'),
              search=[56,22,90,12],teams=[[14,37,68,15],[82,37,70,15]],rebuy=rect('rebuybutton'),
              favorites=[[374+(i%2)*110,52+(i//2)*35,107,31] for i in range(8)],
              slotBackground=[texture(theme['buybutton'][key],'slot'+str(i)) for i,key in enumerate(['up','on'])],
              keyboard=texture('resource/control/button_ingame/keyboard','keyboard'),
              favoriteBackground=texture('resource/control/button_ingame/favoritebg','favorite'),
              frame=[texture(theme['frame'][edge],'f'+str(i)) for i,edge in enumerate(edges)],
              buttons=[[texture(theme['button'][edge+state],'b'+str(i)+str(j)) for j,edge in enumerate(['left','center','right'])] for i,state in enumerate(['n','o','c'])],
              close=[texture('resource/control/frame/btn_default_close@'+s,'x'+str(i)) for i,s in enumerate(['n','o','c'])],
              colors={k:[int(x) for x in scheme['colors'][k].split()] for k in ['basetext','brightcontroltext','labeldimtext','windowbg','borderbright']},images=images)
    generated['models/csol/buymenu.json']=json.dumps(menu,ensure_ascii=False,separators=(',',':')).encode('utf8')
    output.mkdir(parents=True);files=[]
    for name,data in generated.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);files.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    output.with_suffix('.manifest.json').write_text(json.dumps(dict(inputs=inputs,files=files),indent=2),encoding='utf8')
    print(json.dumps(dict(files=len(files),images=len(images),bytes=sum(x['bytes'] for x in files))))
if __name__=='__main__':main()
