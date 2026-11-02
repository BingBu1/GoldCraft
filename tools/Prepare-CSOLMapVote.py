"""Export original CSOL map cards for GoldCraft's authoritative end-map vote.

The menu uses the supplied modern GameUI's measured card layout and original
images/font and continuous scrolling. Multiplayer ballots, counts and countdown
are GoldCraft policy. The animated ModeBg.bik background is not exported or emulated.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent.parent
GAMEUI = 'bd31fdf1c4ee795bf24de64ab28a6fcf0671c86db485ff3d563cbd505aa48770'
MAPS = [(74, 'zs_lostcity'), (75, 'zs_2gate'), (76, 'zs_trap'),
        (77, 'zs_lastclue'), (85, 'zs_chaos'), (98, 'zs_deadend'), (101, 'zs_nightmare')]
spec = importlib.util.spec_from_file_location('character_menu_exporter',
    Path(__file__).with_name('Prepare-CSOLCharacterMenu.py'))
kv_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kv_module)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--panel-evidence', type=Path,
                        help='Optional original GameUI round-panels.json evidence cross-check')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Require a new workspace build output')
    evidence_bytes = args.panel_evidence.read_bytes() if args.panel_evidence else b''
    evidence = json.loads(evidence_bytes) if evidence_bytes else None
    if evidence and evidence['binarySha256'] != GAMEUI:
        raise ValueError('This layout requires the analyzed modern GameUI identity')
    generated, inputs = {}, {}

    def read(name):
        path = (source/name).resolve()
        if not path.is_relative_to(source):
            raise ValueError('Input path escapes common')
        data = path.read_bytes()
        inputs[name] = dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        return data

    def kv(name):
        read(name)
        return next(iter(kv_module.keyvalues(source/name).values()))

    def image(name, alias, expected=None):
        data = read(name+'.tga')
        if expected and hashlib.sha256(data).hexdigest() != expected:
            raise ValueError('Original map-panel image changed: '+name)
        if len(data) < 18:
            raise ValueError('Truncated TGA: '+name)
        width, height = struct.unpack_from('<HH', data, 12)
        if not 0 < width <= 4096 or not 0 < height <= 4096:
            raise ValueError('Invalid image bounds')
        path = 'gfx/csol/mapvote/v1/'+alias+'.tga'
        if path in generated and generated[path] != data:
            raise ValueError('Conflicting output identity')
        generated[path] = data
        return dict(path=path, width=width, height=height)

    theme = kv('resource/clientscheme.res')
    font = theme['fonts']['default16']['1']
    if font != dict(name='DFYuanW9-GB', tall='16', weight='0', antialias='1', dropshadow='0'):
        raise ValueError('Default16 differs from the analyzed original scheme')
    translations = {}
    for name in ['resource/cstrike_chn.txt', 'resource/cso_chn.txt']:
        translations.update(kv(name)['tokens'])
    rows = list(csv.reader(read('resource/maplist.csv').decode('latin1').splitlines()))
    catalog = {}
    for row in rows[2:]:
        if row and row[0].strip().isdigit():
            catalog.setdefault(int(row[0]), row)
    maps = []
    for identity, name in MAPS:
        row = catalog[identity]
        if row[2] != name:
            raise ValueError('First-season map catalog identity changed')
        label = translations.get(row[1].lstrip('#').lower())
        description = translations.get('cso_mapinfo_'+name)
        if not label or not description:
            raise ValueError('Original Chinese map localization missing')
        thumbnail = image('gfx/maps/'+name+'_cso', 'map-'+name)
        # The alternate %s2_cso branch is not a general detail-image rule.
        # Keep the selected map's same original preview until its branch is needed.
        maps.append(dict(id=identity, map=name, name=label, description=description,
                         thumbnail=thumbnail, detail=thumbnail))
    groups = {panel['kind']: panel for panel in evidence['panels']} if evidence else {}
    cards = []
    for kind in ['normal', 'selected', 'hover']:
        if evidence:
            group = groups[kind]
            if [part['index'] for part in group['parts']] != list(range(9)):
                raise ValueError('Missing original nine-slice part')
            cards.append([image(part['name'], kind+str(part['index']), part['files'][0]['sha256'])
                          for part in group['parts']])
        else:
            # Original CRoundPanel type0/type1/type10 tables. Paths and layout
            # are source metadata; no copyrighted game image is published.
            parts = [vertical+'_'+horizontal for vertical in ['top', 'center', 'bottom']
                     for horizontal in ['left', 'center', 'right']]
            base = {'normal': 'resource/control/button_list/9/Button_List_',
                    'selected': 'resource/control/selected/Selected_',
                    'hover': 'resource/Control/Selected/Over_'}[kind]
            cards.append([image(base+part+('@n' if kind == 'normal' else ''), kind+str(i))
                          for i, part in enumerate(parts)])
    # Root frame is an explicit static adaptation; original GameUI creates a
    # BIK EffectPanel and chooses its height at runtime. Card/side-panel skins
    # retain the actual CRoundPanel type0/1/10 assets and measured positions.
    corners = ['topleft', 'topcenter', 'topright', 'middleleft', 'middlecenter',
               'middleright', 'bottomleft', 'bottomcenter', 'bottomright']
    frame = [image(theme['basesettings']['frame'][key], 'frame'+str(i))
             for i, key in enumerate(corners)]
    buttons = [[image('resource/control/button_glow/btn_glow_'+edge+'@'+state,
                      'button'+str(s)+str(e)) for e, edge in enumerate(['left', 'center', 'right'])]
               for s, state in enumerate(['n', 'o', 'c'])]
    close = [image('resource/control/frame/btn_default_close@'+state, 'close'+str(i))
             for i, state in enumerate(['n', 'o', 'c'])]
    scroll = [image(theme['basesettings']['scrollbarslider'][name], 'scroll'+str(i))
              for i, name in enumerate(['verticaltopimg', 'verticalmiddleimg', 'verticalbottomimg'])]
    font_path = 'resource/csol/mapvote/v1/df_gb_y9.ttf'
    generated[font_path] = read('resource/font/df_gb_y9.ttf')
    menu = dict(version=1, width=932, height=510, font=font['name'], fontResource=font_path,
                fontSize=16, fontWeight=0, columns=3, rows=3, strideX=210, strideY=147,
                list=[53, 19, 638, 432], info=[698, 19, 206, 432], preview=[701, 65, 200, 97],
                scrollbar=[676, 19, 14, 431],
                card=[0, 0, 200, 134], cardImage=[0, 0, 200, 97], cardLabel=[10, 105, 170, 21],
                confirm=[715, 352, 172, 41], close=[908, 1, 20, 20],
                cards=cards, frame=frame, infoBackground=cards[0], buttons=buttons,
                closeImages=close, scrollThumb=scroll, maps=maps)
    generated['models/csol/mapvote_v1.json'] = json.dumps(menu, ensure_ascii=False,
                                                       separators=(',', ':')).encode('utf8')
    output.mkdir(parents=True)
    files = []
    for name, data in generated.items():
        path = output/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        files.append(dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
    manifest = dict(gameuiSha256=GAMEUI, panelEvidenceSha256=hashlib.sha256(evidence_bytes).hexdigest() if evidence else None,
                    inputs=inputs, files=files,
                    originalConsumers=['CMapSelectDialogV2', 'Default16', 'CRoundPanel::SetType'],
                    adaptations=['authoritative multiplayer vote', 'static root frame (no ModeBg.bik)',
                                 'close control and countdown'],
                    doesNotEnableMaps=True)
    output.with_suffix('.manifest.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding='utf8')
    print(json.dumps(dict(files=len(files), bytes=sum(item['bytes'] for item in files), maps=len(maps))))


if __name__ == '__main__':
    main()
