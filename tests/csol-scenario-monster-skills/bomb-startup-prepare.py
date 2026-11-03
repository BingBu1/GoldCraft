# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import argparse,hashlib,json
args=argparse.ArgumentParser(description='Prepare the actual-SDK zombie-bomb startup suite; no compilation.')
args.add_argument('--sdk',type=Path,required=True)
args.add_argument('--output',type=Path,required=True)
args.add_argument('--compiler',type=Path,required=True)
args.add_argument('--edges',type=Path,required=True)
args=args.parse_args()
root=Path(__file__).resolve().parents[2]
base=args.sdk.resolve();out=args.output.resolve()
assert base.is_relative_to(root/'build') and out.is_relative_to(root/'build')
assert not out.exists()
out.mkdir()
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def row(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=sha(p))
def write(p,x):p.write_text(json.dumps(x,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
def freeze(relative):
 p=out/'source'/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((root/relative).read_bytes());assert p.stat().st_nlink==1;return p
rels=['native/csol/server/scenario_monster_bomb.cpp','native/csol/server/scenario_monster_bomb.h','native/csol/server/scenario_monster_skills.h','tests/csol-scenario-monster-skills/bomb_startup.cpp','tests/csol-scenario-rules-session/sdk_engine.hpp','tests/csol-scenario-rules-session/resources.hpp','tests/csol-scenario-map-session/fixtures.hpp']
new=[freeze(x) for x in rels]
edges=args.edges.resolve()
assert edges.is_dir()
for n in ['engine_edge_names.inc','engine_edge_defaults.inc']:
 p=out/'source/tests/csol-scenario-rules-session'/n;p.write_bytes((edges/n).read_bytes());new.append(p)
overlay=json.loads((base/'source-overlay.json').read_text())
# Preserve baseline mappings and map new source siblings to exactly the same
# effective header set. No previous SDK layout is mixed with a new one.
files=json.loads((base/'effective-source-manifest.json').read_text())['files']
frozen={x['relative']:x['frozen'] for x in files}
for r in ['native/csol/server/scenario_monster_factory.h','native/csol/server/scenario_monster_pool.h','native/csol/server/scenario_monster_ordinary.h','native/csol/server/scenario_monster_animation.h']:
 assert sha(root/r)==frozen[r]['sha256'],r
for x in files:
 if Path(x['relative']).suffix.lower() in ('.h','.hpp','.inc'):
  overlay['roots'].append({'type':'file','name':str(out/'source'/x['relative']),'external-contents':x['frozen']['path']})
for r in rels:
 dst=str(out/'source'/r)
 overlay['roots']=[x for x in overlay['roots'] if x['name'].lower() not in [str(root/r).lower(),dst.lower()]]
 overlay['roots'].append({'type':'file','name':str(root/r),'external-contents':dst})
write(out/'overlay.json',overlay)
cmd=[str(args.compiler.resolve()),'--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/c','/showIncludes','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/Zc:strictStrings-','/W3','/Gy','/Gw']
cmd+=['/D'+x for x in ['REGAMEDLL_ADD','REGAMEDLL_API','REGAMEDLL_FIXES','REGAMEDLL_SSE','REGAMEDLL_SELF','REGAMEDLL_CHECKS','UNICODE_FIXES','BUILD_LATEST','BUILD_LATEST_FIXES','CLIENT_WEAPONS','USE_QSTRING','_CRT_SECURE_NO_WARNINGS','NDEBUG','NOMINMAX','_WINDLL','_MBCS']]
cmd+=['/I'+str(root/'external/ReGameDLL_CS/regamedll'/x) for x in ['','hookers','common','dlls','engine','public','public/regamedll','pm_shared','regamedll','testsuite']]
cmd+=['/I'+str(root/x) for x in ['native/include','external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include','external/ReGameDLL_CS/dep/cppunitlite/include']]
cmd+=['/clang:-ivfsoverlay','/clang:'+str(out/'overlay.json')]
jobs=[]
for name,r in [('bomb','native/csol/server/scenario_monster_bomb.cpp'),('test','tests/csol-scenario-monster-skills/bomb_startup.cpp')]:
 jobs.append({'name':name,'command':cmd+[str(out/'source'/r),'/Fo'+str(out/(name+'.obj'))]})
link=json.loads((base/'build-commands.json').read_text())[-1]['command']
remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:')
link=[x for x in link if x.lower()!='/dll' and not x.lower().startswith(remove)]
link+=['/OUT:'+str(out/'startup.exe'),'/PDB:'+str(out/'startup.pdb'),'/SUBSYSTEM:CONSOLE',str(out/'bomb.obj'),str(out/'test.obj')]
jobs.append({'name':'link','command':link})
env=json.loads((base/'environment.json').read_text())
inputs=json.loads((base/'inputs.json').read_text())['files']
# Only inherited recorded identities; no fresh whole-workspace copy.
seen={}
for x in inputs+[x['frozen'] for x in files]+[row(x) for x in new]+[row(out/'overlay.json')]:
 key=x['path'].lower()
 if key in seen:assert seen[key]['sha256']==x['sha256'],key
 else:seen[key]=x
for p in link:
 if Path(p).is_absolute() and p.lower().endswith(('.obj','.lib','.exe')) and Path(p).exists():seen[p.lower()]=row(p)
unread_changed=[]
for key,x in list(seen.items()):
 if sha(x['path'])!=x['sha256']:
  # A previously compiled SDK source is not a new compiler input. Preserve
  # the prior receipt, and require every newly observed include below to be
  # in the unchanged set. All new translation units are independent copies.
  unread_changed.append(x);del seen[key]
write(out/'prepared.json',dict(jobs=jobs,environment=env,inputs=list(seen.values()),inheritedFilesNoLongerCurrent=unread_changed,baseline=str(base),boundary='Real SDK startup helper only; Create is explicit observer; no entity/service/DLL/gameplay acceptance.'))
print(json.dumps({'out':str(out),'jobs':len(jobs),'inputs':len(seen)}))
