# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare one test TU against a completed, identity-checked whole SDK build."""
from pathlib import Path
import argparse, hashlib, json

p = argparse.ArgumentParser()
p.add_argument('--sdk', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--edges', type=Path, required=True)
args = p.parse_args()
root = Path(__file__).resolve().parents[2]
base, out = args.sdk.resolve(), args.output.resolve()
assert base.is_relative_to(root / 'build') and out.is_relative_to(root / 'build')
assert not out.exists()

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def row(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha(path))

def write(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')

jobs = json.loads((base / 'build-commands.json').read_text())
link = jobs[-1]['command']
objects = [Path(x) for x in link if x.lower().endswith(('.obj', '.lib')) and Path(x).is_absolute()]
assert objects and all(x.is_file() for x in objects), 'complete SDK objects are required'
out.mkdir(parents=True)
relative = ['tests/csol-scenario-monster-skills/incoming_sdk.cpp',
            'tests/csol-scenario-monster-skills/trap_fixture.hpp',
            'tests/csol-scenario-rules-session/sdk_engine.hpp',
            'tests/csol-scenario-rules-session/resources.hpp',
            'tests/csol-scenario-map-session/fixtures.hpp',
            'tests/csol-scenario-navigation-loader/support.hpp',
            'tests/csol-scenario-difficulty/support.hpp']
new = []
for r in relative:
    dst = out / 'source' / r
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes((root / r).read_bytes())
    assert dst.stat().st_nlink == 1
    new.append(dst)
for name in ['engine_edge_names.inc', 'engine_edge_defaults.inc']:
    dst = out / 'source/tests/csol-scenario-rules-session' / name
    dst.write_bytes((args.edges / name).read_bytes())
    new.append(dst)
overlay = json.loads((base / 'source-overlay.json').read_text())
manifest = json.loads((base / 'effective-source-manifest.json').read_text())['files']
for x in manifest:
    if Path(x['relative']).suffix.lower() in ('.h', '.hpp', '.inc'):
        overlay['roots'].append(dict(type='file', name=str(out / 'source' / x['relative']),
                                     **{'external-contents': x['frozen']['path']}))
for r in relative:
    virtual = str(root / r)
    overlay['roots'] = [x for x in overlay['roots'] if x['name'].lower() != virtual.lower()]
    overlay['roots'].append(dict(type='file', name=virtual, **{'external-contents': str(out / 'source' / r)}))
write(out / 'overlay.json', overlay)
compiler = jobs[0]['command'][0]
command = [compiler, '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
           '/utf-8', '/c', '/showIncludes', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2',
           '/fp:precise', '/Zc:strictStrings-', '/W3', '/Gy', '/Gw']
command += ['/D' + x for x in ['REGAMEDLL_ADD', 'REGAMEDLL_API', 'REGAMEDLL_FIXES', 'REGAMEDLL_SSE',
    'REGAMEDLL_SELF', 'REGAMEDLL_CHECKS', 'UNICODE_FIXES', 'BUILD_LATEST', 'BUILD_LATEST_FIXES',
    'CLIENT_WEAPONS', 'USE_QSTRING', '_CRT_SECURE_NO_WARNINGS', 'NDEBUG', 'NOMINMAX', '_WINDLL', '_MBCS']]
command += ['/I' + str(root / 'external/ReGameDLL_CS/regamedll' / x) for x in ['', 'hookers', 'common',
    'dlls', 'engine', 'public', 'public/regamedll', 'pm_shared', 'regamedll', 'testsuite']]
command += ['/I' + str(root / x) for x in ['native/include',
    'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include', 'external/ReGameDLL_CS/dep/cppunitlite/include']]
command += ['/clang:-ivfsoverlay', '/clang:' + str(out / 'overlay.json'),
            str(out / 'source' / relative[0]), '/Fo' + str(out / 'test.obj')]
remove = ('/out:', '/pdb:', '/implib:', '/def:', '/ltcgout:', '/manifest', '/natvis:')
link = [x for x in link if x.lower() != '/dll' and not x.lower().startswith(remove)]
link += ['/OUT:' + str(out / 'incoming.exe'), '/PDB:' + str(out / 'incoming.pdb'),
         '/SUBSYSTEM:CONSOLE', str(out / 'test.obj')]
inputs = json.loads((base / 'inputs.json').read_text())['files']
seen, stale = {}, []
for item in inputs + [x['frozen'] for x in manifest]:
    if not Path(item['path']).is_file() or sha(item['path']) != item['sha256']:
        stale.append(item)
    else:
        seen[item['path'].lower()] = item
for item in [row(x) for x in new + objects] + [row(out / 'overlay.json'), row(Path(compiler)), row(Path(link[0]))]:
    seen[item['path'].lower()] = item
write(out / 'prepared.json', dict(jobs=[dict(name='test', command=command), dict(name='link', command=link)],
    environment=json.loads((base / 'environment.json').read_text()), inputs=list(seen.values()),
    baseline=str(base), inheritedSourcesNoLongerCurrent=stale,
    boundary='All production objects unchanged from whole server build; one test TU. Actual incoming, equipment, authority and TakeDamage original. Selection hosts pool/map initialization independently; no full Combat/AI/live acceptance. Native Spawn tail is intentionally stopped at the first physics-key engine callback.'))
print(json.dumps(dict(out=str(out), inputs=len(seen), inheritedStale=len(stale), objects=len(objects), jobs=2)))
