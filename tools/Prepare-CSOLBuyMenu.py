"""Export original CSOL buy presentation from the consolidated common folder."""
import argparse
import csv
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
    for ident,icon in re.findall(r'\{(\d+),\d+,"[^"]*","[^"]*","([^"]*)",\d+(?:,WeaponSlot::\w+)?\}',catalog):
        if not icon:continue
        name='gfx/vgui/basket/'+icon
        if not (source/(name+'.tga')).is_file():name='gfx/vgui/'+icon
        if (source/(name+'.tga')).is_file():images.append(dict(id=int(ident),**texture(name,'w'+ident)))
    # K3's price is backed by original HW IGBuyMenuMgr record weapon197,+0x18.
    # Localization is only an export-time cross-check of that purchase record.
    # The original mode-15 override table has neither M249EX nor Balrog-XI;
    # their default purchase records therefore also apply in Scenario.
    price_text_differences=[]
    for name,price in [('cannon',4100),('tknifeex2',30),('k3',4750),('m249ex',9000),('balrog11',5750),('skull1',1200),('skull2',1550),('skull3',4000),('skull5',6000)]:
        token='cstrike_'+name+'price'
        # Original HW record312,+0x18 is1550; mode15 has no override.
        # Its static Chinese price token says2200. Preserve that discrepancy
        # in the export audit instead of using UI text as runtime pricing.
        expected_text=2200 if name=='skull2' else price
        localized_price=int(re.search(r'\d+',text[token])[0])
        if localized_price!=expected_text:
            raise ValueError(f'Unexpected original price text {token}: {localized_price}, expected {expected_text}')
        if localized_price!=price:
            price_text_differences.append(dict(weapon=name,token=token,localizedPrice=localized_price,runtimePrice=price))
    for ident in (29,96,112,130,142,144,197,203,220,312,725,726,1022,1023):
        if not any(image['id']==ident for image in images):
            raise ValueError(f'Missing original purchase image for weapon {ident}')
    for n in ['beamgun','beamgunle']:assert json.loads(read('weapons/weapon_'+n+'.json'))['price']==6100
    # Current original Item.csv identities are explicit: InGameID alone is not unique.
    # Cannon's other same-resource row is an excluded enhancement variant.
    item_data=read('resource/item.csv')
    if hashlib.sha256(item_data).hexdigest()!='35545cb7048218b4c6b01427f64f87a04f607fe98b39cf4b616099086f2a3cf1':
        raise ValueError('Unsupported original Item.csv revision; verify its native consumers before exporting')
    rows={int(r[0]):r for r in csv.reader(item_data.decode('cp949').splitlines()) if r and r[0].isdigit()}
    item_identity={
        220: (581, "tknifeex2", 220, 1),
        130: (407, "skull1", 130, 1),
        312: (828, "skull2", 312, 1),
        1: (2, "p228", 1, 1),
        10: (1, "Elites", 10, 1),
        11: (5, "fiveseven", 11, 1),
        16: (6, "usp45", 16, 1),
        17: (3, "Glock18", 17, 1),
        26: (4, "deserteagle", 26, 1),
        5: (8, "xm1014", 5, 2),
        21: (7, "m3", 21, 2),
        203: (554, "balrog11", 203, 2),
        7: (10, "MAC10", 7, 3),
        12: (13, "ump45", 12, 3),
        142: (427, "skull3", 142, 3),
        19: (12, "MP5", 19, 3),
        23: (9, "tmp", 23, 3),
        30: (11, "p90", 30, 3),
        3: (17, "Scout", 3, 4),
        8: (16, "aug", 8, 4),
        13: (18, "sg550", 13, 4),
        14: (23, "Galil", 14, 4),
        15: (20, "famas", 15, 4),
        18: (14, "awp", 18, 4),
        22: (15, "m4a1", 22, 4),
        24: (22, "G3SG1", 24, 4),
        27: (19, "sg552", 27, 4),
        28: (21, "AK47", 28, 4),
        112: (367, "skull5", 112, 4),
        20: (24, "m249", 20, 5),
        197: (525, "k3", 197, 5),
        96: (327, "m249ex", 96, 5),
        144: (428, "cannon", 144, 6),
        725: (9637, "beamgun", 725, 6),
        726: (9638, "beamgunle", 726, 6),
        1001: (27, "Kevlar", 2000, 6),
        1002: (28, "Kevlar_Helmet", 2001, 6),
        1003: (31, "HEGrenade", 2003, 6),
        1004: (30, "Flashbang", 2002, 6),
        1005: (26, "SmokeGrenade", 2004, 6),
        29: (161, "defaultKnife", 4000, 11),
        1022: (483, "dragonsword", 4016, 11),
        1023: (363, "skullaxe", 4010, 11),
    }
    # Native weapon-provider record maxClip; Item.csv ammo is not consumed here.
    max_clip={1: 13, 3: 10, 5: 7, 7: 30, 8: 30, 10: 30, 11: 20, 12: 25, 13: 30, 14: 35, 15: 25, 16: 12, 17: 20, 18: 10, 19: 30, 20: 100, 21: 8, 22: 30, 23: 30, 24: 20, 26: 7, 27: 30, 28: 30, 30: 50, 96: 120, 112: 24, 130: 7, 142: 35, 144: 20, 197: 150, 203: 7, 220: 30, 312: 7}
    for ident,name in [(725,'beamgun'),(726,'beamgunle')]:
        weapon=json.loads(read('weapons/weapon_'+name+'.json'))
        if (weapon['ingameID'],weapon['maxClip'],weapon['slot'])!=(ident,80,1) or 'WPN_EQUIPMENT' not in weapon['properties']:
            raise ValueError(f'Unexpected original weapon provider record {name}')
        max_clip[ident]=weapon['maxClip']
    attributes=['Damage','Zombie_Damage','Scen_Damage','AccuracyRate','Rebound','Fusillade','Weight','Knockback','Zombie_Delay','Load']
    columns=[21,49,50,22,23,24,25,51,52]
    def localized(token):return text[token.lower()]
    def atoi(value):
        match=re.match(r'\s*([+-]?\d+)',value)
        return int(match[1]) if match else 0
    def vector(value):return [atoi(v) for v in value.split(',') if v] or [0]
    info_items=[]
    for ident,(item_id,resource,in_game,category) in item_identity.items():
        row=rows[item_id]
        if (row[2],atoi(row[5]),atoi(row[4]))!=(resource,in_game,category):
            raise ValueError(f'Unexpected original item identity for host weapon {ident}')
        values=[vector(row[column]) for column in columns]
        mode_count=max(map(len,values))
        display=[]
        for index,value in enumerate(values):
            suffix='' if index<3 else ' %'
            first=value[0]
            second=value[1] if len(value)>1 else 0
            display.append((str(first)+suffix+((' / '+str(second)+suffix) if mode_count==2 and second else '')) if mode_count in (1,2) else '')
        display.append(str(max_clip[in_game]) if in_game in max_clip else '')
        grades=vector(row[48])
        info_items.append(dict(id=ident,itemId=item_id,name=localized('CSO_Item_Name_'+resource),
            description=localized('CSO_Item_Desc_'+resource),
            table=1<=category<=5 or in_game in (144,725,726),values=display,
            grade=[grades[i] if i<len(grades) else -1 for i in range(3)]))
    # Current client consumers: RichText18170A410, ScrollBar1817127F0,
    # ScrollBarSlider18175CFE0 and arrow181712840 inherit ClientScheme.
    # Its missing ScrollBar/BgColor returns the original GetSchemeColor
    # white default; no host-scheme color or substitute image is allowed.
    def scheme_color(key,default=None):
        value=scheme['colors'].get(key.lower())
        if value is None:
            value=theme
            for part in key.lower().split('/'):
                value=value.get(part) if isinstance(value,dict) else None
                if value is None:break
        if value is None:
            if default is None:raise ValueError(f'Missing original scheme color {key}')
            return list(default)
        seen=set()
        while not re.fullmatch(r'\s*\d+\s+\d+\s+\d+\s+\d+\s*',value):
            alias=value.lower()
            if alias in seen:raise ValueError(f'Cyclic original scheme color {key}')
            seen.add(alias);value=scheme['colors'][alias]
        rgba=[int(channel) for channel in value.split()]
        if any(channel>255 for channel in rgba):raise ValueError(f'Invalid original scheme color {key}')
        return rgba
    arrow_fg=scheme_color('BaseText');arrow_bg=scheme_color('LabelBgColor')
    scroll_style=dict(colors=dict(
        # Update181300CF3 overwrites WindowFgColor; DescText::GetBgColor
        #1812FE960 returns transparent regardless of WindowBgColor.
        richTextFg=[168,168,168,255],richTextBg=[0,0,0,0],
        barBg=scheme_color('ScrollBar/BgColor',[255,255,255,255]),
        sliderFg=scheme_color('ScrollBarSlider/ScrollBarSliderFgColor'),
        sliderBg=scheme_color('ScrollBarSlider/ScrollBarSliderBgColor'),
        arrowFg=arrow_fg,arrowBg=arrow_bg,
        arrowArmedFg=arrow_fg,arrowArmedBg=arrow_bg,
        arrowDepressedFg=arrow_fg,arrowDepressedBg=arrow_bg),
        slider=[texture(theme['scrollbarslider'][key],'scroll_'+part)
            for key,part in [('verticaltopimg','top'),('verticalmiddleimg','center'),('verticalbottomimg','bottom')]])
    weapon_info=dict(priceLabel=localized('CSO_Price'),
        attributeLabels=[localized('CSO_'+attribute+'_Label') for attribute in attributes],
        gradeLabels=[localized('CSO_Tooltip_Desc_ModeType_'+mode) for mode in ['Normal','Zombie','ZombieScen']],
        background=texture('resource/Control/Basket/basket_blank_slot','info_background'),
        divider=texture('resource/tooltip_bar','info_divider'),
        grades={str(grade):texture('resource/tooltip_'+name,'grade'+str(grade)) for grade,name in [(0,'d'),(1,'c'),(2,'b'),(3,'a'),(4,'s'),(5,'ss'),(6,'sss'),(8,'legend')] if any(grade in item['grade'] for item in info_items)},
        scroll=scroll_style,items=info_items)
    # Runtime consumers, not screenshot measurements: 02942780 chooses the
    # normal/Scenario outer root; 02934D80 creates the left list; 02938920
    # creates favorite children relative to their own panel. Keep that parent
    # relationship so each level retains the original integer rounding.
    generated['sound/events/tab_1.wav']=read('sound/events/tab_1.wav')
    generated['resource/font/df_gb_y9.ttf']=read('resource/Font/DF_GB_Y9.TTF')
    menu=dict(version=3,width=606,height=444,root=[17,22,606,444],
              listPanel=[0,0,217,352],favoritePanel=[372,30,215,162],
              title=text['cso_weaponselectmenu'],listTitle=text['cso_buysubmenutitle'],
              favoriteDescription=text['cso_buyeditdesc'],
              teamNames=[text['cso_buyshowter'],text['cso_buyshowct']],font=scheme['fonts']['default']['1']['name'],
              info=[216,56,152,220],weaponInfo=weapon_info,slots=[[12,50+i*25,194,25] for i in range(10)],
              previous=[28,309,66,17],next=[126,309,66,17],quit=[12,275,194,25],account=rect('account_num'),
              rebuy=rect('rebuybutton'),
              favorites=[[(i%2)*109,26+(i//2)*35,106,31] for i in range(8)],
              slotBackground=[texture(theme['buybutton'][key],'slot'+str(i)) for i,key in enumerate(['up','on'])],
              keyboard=texture('resource/control/button_ingame/keyboard','keyboard'),
              favoriteBackground=texture('resource/control/button_ingame/favoritebg','favorite'),
              frame=[texture(theme['frame'][edge],'f'+str(i)) for i,edge in enumerate(edges)],
              buttons=[[texture(theme['button'][edge+state],'b'+str(i)+str(j)) for j,edge in enumerate(['left','center','right'])] for i,state in enumerate(['n','o','c'])],
              tabs=[[texture('resource/control/TabButton/tab_small_'+enabled+'_'+edge+'_'+suffix,'tab'+str(i)+str(j)) for j,(edge,suffix) in enumerate(zip(['left','center','right'],suffixes))] for i,(enabled,suffixes) in enumerate([(enabled,suffixes) for enabled in ['abled','disabled'] for suffixes in ['nnn','ooo','occ']])],
              close=[texture('resource/control/frame/btn_default_close@'+s,'x'+str(i)) for i,s in enumerate(['n','o','c'])],
              colors={k:[int(x) for x in scheme['colors'][k].split()] for k in ['basetext','brightcontroltext','labeldimtext','windowbg','borderbright']},images=images)
    # Share the exact revision with both native consumers. Reusing the old
    # filename leaves already connected clients with an incompatible cached file.
    contract=(ROOT/'native/csol/buy_menu.hpp').read_text(encoding='utf8')
    resource=re.search(r'PresentationResource\[\]="(models/csol/buymenu_v\d+\.json)";',contract)
    if not resource:raise ValueError('Missing versioned purchase presentation contract')
    generated[resource.group(1)]=json.dumps(menu,ensure_ascii=False,separators=(',',':')).encode('utf8')
    output.mkdir(parents=True);files=[]
    for name,data in generated.items():
        path=output/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);files.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    output.with_suffix('.manifest.json').write_text(json.dumps(dict(inputs=inputs,files=files,
        priceTextDifferences=price_text_differences),indent=2),encoding='utf8')
    print(json.dumps(dict(files=len(files),images=len(images),bytes=sum(x['bytes'] for x in files))))
if __name__=='__main__':main()
