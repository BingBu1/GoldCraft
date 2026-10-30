"""Extract the original CSOL class menu presentation into isolated HTTP resources.

Only presentation data is exported; selection remains authoritative in ReGameDLL.
The supplied installation is read-only and copyrighted resources are not source assets.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct


def keyvalues(path):
    raw = path.read_bytes()
    text = raw.decode('utf-16' if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig')
    tokens = re.finditer(r'//[^\n]*|"((?:\\.|[^"\\])*)"|([{}])', text)
    values = []
    for token in tokens:
        quoted, brace = token.groups()
        if quoted is not None or brace:
            values.append(brace or re.sub(r'\\([nrt"\\])', lambda m: {'n': '\n', 'r': '\r', 't': '\t'}.get(m[1], m[1]), quoted))
    index = 0

    def block():
        nonlocal index
        result = {}
        while index < len(values) and values[index] != '}':
            key = values[index].lower()
            index += 1
            value = values[index]
            index += 1
            if value == '{':
                value = block()
            result[key] = value
        if index < len(values):
            index += 1
        return result
    return block()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output == source or output.is_relative_to(source) or output.exists():
        raise ValueError('Require a new, isolated output directory')
    root = Path(__file__).resolve().parent.parent
    catalog = json.loads((root/'native/csol/config/characters.json').read_text(encoding='utf8'))['characters']
    inputs, generated = {}, {}

    def read(name):
        path = source/name
        data = path.read_bytes()
        inputs[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        return data

    def kv(name):
        read(name)
        return next(iter(keyvalues(source/name).values()))

    layout = kv('resource/UI/cso_classmenu.res')
    paged = kv('resource/UI/cso_classmenu_ver2.res')
    scheme = kv('resource/clientscheme.res')
    translations = {}
    for name in ['resource/cstrike_chn.txt', 'resource/cso_chn.txt']:
        translations.update(kv(name)['tokens'])

    def localize(value):
        return translations.get(value[1:].lower(), '') if value.startswith('#') else value

    def rect(value):
        return [int(value.get(k, 0)) for k in ['xpos', 'ypos', 'wide', 'tall']]

    def texture(name, alias):
        data = read(name+'.tga')
        path = 'gfx/csol/ui/'+alias+'.tga'
        generated[path] = data
        w, h = struct.unpack_from('<HH', data, 12)
        return dict(path=path, width=w, height=h)

    corners = ['topleft', 'topcenter', 'topright', 'middleleft', 'middlecenter', 'middleright', 'bottomleft', 'bottomcenter', 'bottomright']
    theme = scheme['basesettings']
    frame = [texture(theme['frame'][key], 'f'+str(i)) for i, key in enumerate(corners)]
    buttons = [[texture(theme['button'][edge+state], 'b'+str(s)+str(e)) for e, edge in enumerate(['left','center','right'])] for s,state in enumerate(['n','o','c'])]
    close = [texture('resource/control/frame/btn_default_close@'+s, 'x'+str(i)) for i,s in enumerate(['n','o','c'])]
    # The dedicated server seeds untouched profiles from ClassModPreset and
    # ItemTable columns 21..25. These are caps, unlike ZBS_class increase counts.
    # Use latin1 only as a byte-preserving CSV transport; no Korean text is used.
    items = list(csv.reader(read('resource/item.csv').decode('latin1').splitlines()))
    presets = list(csv.reader(read('resource/buff/ClassModPreset.csv').decode('latin1').splitlines()))
    item_by_id = {int(row[0]): row for row in items[2:] if row and row[0].strip()}
    preset_ids = {int(row[0]) for row in presets[1:] if row and row[0].strip()}

    def cap(value):
        token = next((part for part in value.split(',') if part), '')
        match = re.match(r'\s*([+-]?\d+)', token)
        result = int(match[1]) if match else 0
        if not 0 <= result <= 255:
            raise ValueError('ClassMod default cap exceeds the menu level range')
        return result

    descriptions, missing = {}, []
    for c in catalog:
        name = Path(c['sourcePortrait']).stem
        path = 'classes2/'+name+'.res'
        if not (source/path).is_file():
            path = 'classes/'+name+'.res'
        if not (source/path).is_file():
            missing.append(c['id'])
            continue
        panel = kv(path)
        info = panel.get('infolabel', {})
        image = next((v for v in panel.values() if isinstance(v,dict) and v.get('image','')),None)
        if image is None:
            missing.append(c['id'])
            continue
        # Modern runtime layout measured against the supplied 1116x780 CSO
        # screenshot. The legacy RES only describes the old 606x420 panel.
        detail = dict(text=localize(info.get('labeltext','')),
            image=[0, 0, 155, 119], description=[0, 135, 310, 112])
        class_id = c.get('scenarioClass')
        if class_id in preset_ids:
            item = item_by_id.get(class_id)
            if item is not None and len(item) < 26:
                raise ValueError('Referenced ClassMod item has no five cap columns')
            detail['defaultCaps'] = [cap(v) for v in item[21:26]] if item else [0]*5
        descriptions[str(c['id'])] = detail
    # Runtime font is registered privately from memory, never installed in Windows.
    read('resource/Font/DF_GB_Y9.TTF')
    colors = {key: [int(v) for v in scheme['colors'][key].split()] for key in ['basetext','brightcontroltext','labeldimtext','windowbg','borderbright']}
    colors.update(statheading=[125,205,255,255],statbar=[237,237,220,255],statvalue=[237,237,220,255])
    menu = dict(version=4, width=640, height=448,
        info=[249,60,375,275], slots=[[22,60+round(i*26.5),211,26] for i in range(10)],
        teams=[[22,42,20,17],[43,42,23,17]], previous=[22,332,59,17], next=[153,332,59,17],
        labels=dict(teams=['T','CT'],
            previous=localize(paged['prevbtn']['labeltext']), next=localize(paged['nextbtn']['labeltext'])),
        frame=frame, buttons=buttons, close=close, colors=colors, descriptions=descriptions,
        slotBackground=[texture(theme['buybutton'][key], 'slot'+str(i)) for i,key in enumerate(['up','on'])],
        keyboard=texture('resource/control/button_ingame/keyboard','keyboard'),
        font=dict(name=scheme['fonts']['default']['1']['name'],size=9,weight=0,titleSize=11,titleWeight=0),
        pin=texture('resource/zombi/pin','pin'),
        currentLabel=localize('#CSO_ZI_Current_Selected'),
        layout=dict(fullScreenFrame=True,title=[22,16,596,16],pageLabel=[85,332,64,17],closeButton=[616,16,15,15]),
        stats=dict(heading=[417,67,142,12],headingText='Max Level',maximum=30,
            labels=[[417,86+i*21,51,13] for i in range(5)],bars=[[470,90+i*21,73,5] for i in range(5)],
            values=[[548,86+i*21,22,13] for i in range(5)],names=['生命值','攻击力','移动速度','防弹衣','备弹']))
    generated['models/csol/classmenu2.json'] = json.dumps(menu,ensure_ascii=False,separators=(',',':')).encode('utf8')
    output.mkdir(parents=True)
    files=[]
    for name,data in generated.items():
        path=output/name
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data)
        files.append(dict(path=name,bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
    manifest=dict(inputs=inputs,files=files,descriptions=len(descriptions),missing=missing,
        layoutSource='Supplied CSO screenshot 1116x780; normalized 640x448, aspect-preserving content and full-screen frame',
        capSource='ClassModPreset class keys and item.csv columns 21..25, matching server HostDefault profiles; not account modifications',
        defaultCaps=sum('defaultCaps' in d for d in descriptions.values()))
    output.with_suffix('.manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(dict(files=len(files),bytes=sum(r['bytes'] for r in files),descriptions=len(descriptions),missing=missing)))


if __name__ == '__main__':
    main()
