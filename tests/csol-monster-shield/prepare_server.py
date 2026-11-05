# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze narrow trace integration fixture; reuse verified whole-server objects."""
from pathlib import Path
import argparse,copy,hashlib,json,re,subprocess

p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--old-sdk',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--engine',type=Path,required=True);p.add_argument('--models',type=Path,required=True)
a=p.parse_args();ROOT=Path(__file__).resolve().parents[2];OUT=a.output.resolve();BASE=a.sdk.resolve();OLD=a.old_sdk.resolve()
assert OUT.is_relative_to(ROOT/'build') and not OUT.exists();OUT.mkdir(parents=True)
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def ident(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def save(name,value):(OUT/name).write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n','utf8')
def freeze(relative):
 dst=OUT/'source'/relative;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes((ROOT/relative).read_bytes());assert dst.stat().st_nlink==1;return dst
manifest=read(BASE/'effective-source-manifest.json')['files']
abiRow=next(row for row in read(BASE/'inputs.json')['files']if row['path'].replace('\\','/').casefold().endswith('/regamedll_cs/regamedll/engine/eiface.h'))
abi=Path(abiRow['path']);assert ident(abi)['sha256']==abiRow['sha256'],'pinned engine ABI changed'
body=abi.read_text('utf-8-sig').split('typedef struct enginefuncs_s',1)[1].split('} enginefuncs_t;',1)[0]
body=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
edges=re.findall(r'\(\s*\*\s*(pfn\w+)\s*\)',body)
assert len(edges)==len(set(edges)) and edges[0]=='pfnPrecacheModel' and edges[-1]=='pfnEngCheckParm'
generated={
 'engine_edge_names.inc':'inline constexpr const char* engineEdgeNames[] = {\n'+''.join('    "'+n+'",\n'for n in edges)+'};\n',
 'engine_edge_defaults.inc':''.join(f'g_engfuncs.{n}=Unsupported<{i},decltype(g_engfuncs.{n})>::Call;\n'for i,n in enumerate(edges)),
}
fixture={};todo=['tests/csol-monster-shield/server.cpp','tests/csol-monster-shield/bridge.h']
while todo:
 rel=todo.pop()
 if rel in fixture:continue
 path=freeze(rel);fixture[rel]=path
 for inc in re.findall(r'^\s*#include\s*"([^"]+)"',path.read_text('utf8'),re.M):
  original=(ROOT/rel).parent/inc
  if original.resolve().is_relative_to(ROOT/'tests'):
   if original.is_file():todo.append(original.resolve().relative_to(ROOT).as_posix())
   elif inc in ['engine_edge_names.inc','engine_edge_defaults.inc']:
    target=OUT/'source/tests/csol-scenario-rules-session'/inc;target.write_text(generated[inc],'utf8');fixture['tests/csol-scenario-rules-session/'+inc]=target
   elif '/' in inc or '\\' in inc:raise FileNotFoundError(original)
overlay=read(BASE/'source-overlay.json')
names={str(x['name']).casefold():x for x in overlay['roots']}
for row in manifest:
 rel=row['relative'];path=row['frozen']['path'];names[str(OUT/'source'/rel).casefold()]=dict(type='file',name=str(OUT/'source'/rel),**{'external-contents':path})
for rel,path in fixture.items():
 for virtual in [ROOT/rel,OUT/'source'/rel]:names[str(virtual).casefold()]=dict(type='file',name=str(virtual),**{'external-contents':str(path)})
overlay['roots']=list(names.values());save('overlay.json',overlay)
oldOverlay=copy.deepcopy(overlay)
oldHeaders={x['relative'].casefold():x['frozen']['path']for x in read(OLD/'effective-source-manifest.json')['files']if Path(x['relative']).suffix in ['.h','.hpp','.inc']}
for row in oldOverlay['roots']:
 normalized=str(row['name']).replace('\\','/').casefold()
 for rel,path in oldHeaders.items():
  if normalized.endswith('/'+rel):row['external-contents']=path;break
save('old-overlay.json',oldOverlay)
data=OUT/'data';data.mkdir();modelrows=[]
for name in ['zombi_origin','zombi_host','speed_zombi_host','heavy_zombi_origin','deimos_zombi_host','stonedeimos2_host','y19s4psycho_origin','zbs_elite_01','oberon']:
 src=a.models/('monster/zbs_bossL_big02.mdl' if name=='oberon' else 'monster/zbs_elite_01.mdl' if name=='zbs_elite_01' else f'player/{name}/{name}.mdl')
 dst=data/(name+'.mdl');dst.write_bytes(src.read_bytes());modelrows.append(dict(original=ident(src),copy=ident(dst)))
for name in ['trace.dll','trace.pdb']:(data/name).write_bytes((a.engine/name).read_bytes())
save('models.json',dict(models=modelrows,engine=[ident(a.engine/'plan.json'),ident(a.engine/'result.json'),ident(data/'trace.dll')]))
compiler=next(x['command'][0]for x in read(BASE/'build-commands.json')if x['kind']=='compile')
baseflags=[compiler,'--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/c','/showIncludes',
 '/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/Zc:strictStrings-','/W3','/Gy','/Gw']
baseflags += ['/D'+x for x in ['REGAMEDLL_ADD','REGAMEDLL_API','REGAMEDLL_FIXES','REGAMEDLL_SSE','REGAMEDLL_SELF',
 'REGAMEDLL_CHECKS','UNICODE_FIXES','BUILD_LATEST','BUILD_LATEST_FIXES','CLIENT_WEAPONS','USE_QSTRING',
 '_CRT_SECURE_NO_WARNINGS','NDEBUG','NOMINMAX','_WINDLL','_MBCS']]
baseflags += ['/I'+str(ROOT/'external/ReGameDLL_CS/regamedll'/x)for x in
 ['', 'hookers','common','dlls','engine','public','public/regamedll','pm_shared','regamedll','testsuite']]
baseflags += ['/I'+str(ROOT/x)for x in ['native/include','external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include',
 'external/ReGameDLL_CS/dep/cppunitlite/include']]
jobs=[]
for which in ['new','old']:
 argv=baseflags+['/clang:-ivfsoverlay','/clang:'+str(OUT/('old-overlay.json'if which=='old'else'overlay.json'))]
 if which=='old':argv+=['/DSHIELD_BASELINE']
 argv += [str(fixture['tests/csol-monster-shield/server.cpp']),'/Fo'+str(OUT/(which+'.obj'))]
 jobs.append(dict(name='compile-'+which,command=argv))
 link=list(next(x['command']for x in read((BASE if which=='new' else OLD)/'build-commands.json') if x['kind']=='link'))
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','--intdir=')
 link=[x for x in link if x.lower()!='/dll' and not x.lower().startswith(remove)]
 link+=['/OUT:'+str(OUT/(which+'.exe')),'/PDB:'+str(OUT/(which+'.pdb')),'/SUBSYSTEM:CONSOLE',str(OUT/(which+'.obj'))]
 jobs.append(dict(name='link-'+which,command=link))
known={}
for directory in [BASE,OLD]:
 for row in read(directory/'inputs.json')['files']+ [x['frozen']for x in read(directory/'effective-source-manifest.json')['files']]:
  path=Path(row['path'])
  if path.is_file() and ident(path)['sha256']==row['sha256']:known[str(path.resolve()).casefold()]=row
for path in [*fixture.values(),OUT/'overlay.json',OUT/'old-overlay.json',OUT/'models.json',Path(jobs[0]['command'][0]),Path(jobs[1]['command'][0]),*data.iterdir()]:known[str(path.resolve()).casefold()]=ident(path)
environment=read(BASE/'environment.json')
for path in environment['INCLUDE'].split(';'):
 inc=Path(path)
 if inc.is_dir() and (inc/'filesystem').is_file():
  # Microsoft standard-library headers are immutable build dependencies.
  for h in inc.rglob('*'):
   if h.is_file():known[str(h.resolve()).casefold()]=ident(h)
save('prepared.json',dict(jobs=jobs,inputs=list(known.values()),environment=environment,baseline=str(BASE),oldBaseline=str(OLD),
 scope='Actual complete server objects plus actual ReHLDS hull and clip bodies in separate DLL; authentic model hitboxes, controlled bone matrices and animation sequences. No live gameplay.'))
print(json.dumps(dict(output=str(OUT),fixtures=len(fixture),inputs=len(known))))
