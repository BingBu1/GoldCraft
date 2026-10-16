"""Check Steam helper/catalog delivery using a tiny non-playable package fixture.

The official CLI and GUI identities are read from an existing verified plan.
No installer/game/helper is launched, and no game runtime is modified.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('client_package',ROOT/'tools/client-installer/package.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference-plan',type=Path,required=True)
p.add_argument('--catalog',type=Path,required=True)
p.add_argument('--helper',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=package.safe(a.output);assert out.is_relative_to(ROOT) and not out.exists()
reference=json.loads(a.reference_plan.read_text(encoding='utf-8-sig'))
rows={r['path']:r for r in reference['files']}
upstream=Path(rows['MetahookInstallerCLI.exe']['source']).parent
license_file=Path(rows['licenses/MetahookInstaller-LICENSE.txt']['source'])
gui=Path(rows['gui-build.json']['source']).parent
game=out/'non-playable';game.mkdir(parents=True)
def write(relative,data=b'fixture only, not a playable game\n'):
    target=game/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
for name in ('MetaHook.exe','SDL2.dll','SDL3.dll','hw.dll','cstrike/cl_dlls/client.dll'):
    write(name)
for name in package.PLUGINS:write('cstrike/metahook/plugins/'+name)
for tree in package.TREES:(game/'cstrike'/tree).mkdir(parents=True,exist_ok=True)
for name in package.RESOURCE_FILES:write('cstrike/'+name)
for name in ('metahook/gamedata/index.json','renderer/shader/goldcraft.vert.glsl',
             'renderer/shader/goldcraft.frag.glsl','renderer/shader/goldcraft.geom.glsl',
             'captionmod/gameui_english.txt','bulletphysics/gameui_english.txt'):write('cstrike/'+name)
helper=game/'cstrike/metahook/plugins/GoldCraftSteamWorker.exe';shutil.copyfile(a.helper,helper)
catalog=game/'cstrike/metahook/gamedata/goldcraft-steam';catalog.mkdir()
for name in ('index.json','goldcraft-steam-hl-10210.json'):shutil.copyfile(a.catalog/name,catalog/name)
plan=package.make_plan(game,upstream,license_file,gui)
target='install/output/svencoop/metahook/plugins/GoldCraftSteamWorker.exe'
matches=[row for row in plan['files'] if row['path']==target]
assert len(matches)==1 and matches[0]['sha256']==package.digest(a.helper)
assert all('SteamWorker' not in name for name in plan['plugins'])
expected=('install/output/svencoop/metahook/gamedata/goldcraft-steam/index.json',
          'install/output/svencoop/metahook/gamedata/goldcraft-steam/goldcraft-steam-hl-10210.json')
assert all(any(row['path']==name for row in plan['files']) for name in expected)
rejected=[]
for missing in (helper,catalog/'goldcraft-steam-hl-10210.json'):
    parked=out/(missing.name+'.parked')
    # Single known fixture file, neither recursive nor a computed external path.
    assert missing.resolve().is_relative_to(game.resolve()) and parked.resolve().is_relative_to(out.resolve()) and not parked.exists()
    missing.rename(parked)
    try:
        try:package.make_plan(game,upstream,license_file,gui)
        except (FileNotFoundError,ValueError):rejected.append(missing.name)
        else:raise AssertionError('Missing runtime component was accepted')
    finally:parked.rename(missing)
assert len(rejected)==2
report=dict(passed=True,helperHash=matches[0]['sha256'],helperNotPlugin=True,catalogMembers=list(expected),
            missingRejected=rejected,boundary='Real package planner; tiny non-playable fixture, no installer/game or runtime writes.')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report))
