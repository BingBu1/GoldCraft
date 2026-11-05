#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
 p=Path(p).resolve();b=p.read_bytes();return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--oracle',type=Path,required=True);ap.add_argument('--resources',type=Path,required=True);ap.add_argument('--reuse',type=Path);args=ap.parse_args()
 out=args.output.resolve();assert out.is_relative_to(ROOT/'build')and not out.exists();out.mkdir();(out/'temp').mkdir()
 baseline=read(ROOT/'build/csol-skull5-integration-20261103/sdk-01/proposal.json');env=os.environ|baseline['environment'];env['TEMP']=env['TMP']=str(out/'temp')
 template=baseline['commands'][0]['argv'];sources={};todo=['tests/csol-sniper-scope/gpu.cpp','native/csol/client/assets.cpp','native/csol/image_codec.cpp']
 while todo:
  rel=todo.pop()
  if rel in sources:continue
  src=ROOT/rel;target=out/'source'/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(src.read_bytes());sources[rel]=dict(original=identity(src),frozen=identity(target))
  for token in re.findall(r'^\s*#include\s+"([^"]+)"',target.read_text('utf-8-sig'),re.M):
   child=(src.parent/token).resolve()
   if child.is_file()and child.is_relative_to(ROOT)and not child.is_relative_to(ROOT/'external'):todo.append(child.relative_to(ROOT).as_posix())
 steps=[];reused=[];prior=read(args.reuse/'steps.json')if args.reuse else []
 if args.reuse and(args.reuse/'result.json').exists():prior+=[row['compile']for row in read(args.reuse/'result.json').get('reused',[])]
 def run(name,argv,expected=0):
  start=time.monotonic();r=subprocess.run(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180);log=out/(name+'.log');log.write_bytes(r.stdout)
  includes=sorted(set(re.findall(r'Note: including file:\s*(.+)',r.stdout.decode('utf8','replace'))))
  steps.append(dict(name=name,argv=argv,exit=r.returncode,expected=expected,seconds=time.monotonic()-start,log=identity(log),includes=[identity(x.strip())for x in includes]));(out/'steps.json').write_text(json.dumps(steps,ensure_ascii=False,indent=2),'utf8');print(name,r.returncode,flush=True)
  if r.returncode!=expected:print(r.stdout.decode('utf8','replace')[-6000:]);raise RuntimeError(name)
 text=Path(sources['native/csol/client/assets.cpp']['frozen']['path']).read_text('utf8');start=text.index('GLuint UploadImage(');end=text.index('\nAsset::~Asset',start)
 upload=out/'source/upload.cpp';upload.write_text('#include "native/csol/client/assets.hpp"\nnamespace csol::client {\n'+text[start:end]+'\n}\n','utf8')
 sources['upload.cpp']=dict(extractedFrom=sources['native/csol/client/assets.cpp']['frozen'],frozen=identity(upload))
 objects=[]
 for label,rel in [('gpu','tests/csol-sniper-scope/gpu.cpp'),('upload','upload.cpp'),('codec','native/csol/image_codec.cpp'),('glew','external/MetaHookSv-20261007/thirdparty/glew_fork/src/glew.c')]:
  obj=out/(label+'.obj');argv=[]
  match=next((s for s in prior if s['name']=='compile-'+label and s['exit']==0),None)
  if match:
   src=out/'source'/rel if rel in sources else ROOT/rel;valid=identity(match['argv'][-1])['sha256']==identity(src)['sha256']
   for inc in match['includes']:
    path=Path(inc['path']);logical=next((r for r in sources if path.as_posix().casefold().endswith('/'+r.casefold())),None);selected=out/'source'/logical if logical else path
    valid=valid and selected.is_file()and identity(selected)['sha256']==inc['sha256']
   if valid:
    oldobj=Path(next(a[3:]for a in match['argv']if a.startswith('/Fo')));reused.append(dict(source=rel,object=identity(oldobj),compile=match));objects.append(str(oldobj));print('reuse',rel,flush=True);continue
  for a in template:
   if (a.startswith('/I')or a.startswith('-I'))and'generated'in a:continue
   if a.startswith('/Fo'):a='/Fo'+str(obj)
   elif a.startswith('/Fd'):a='/Fd'+str(out/(label+'.pdb'))
   elif a==template[-1]:a=str(out/'source'/rel)if rel in sources else str(ROOT/rel)
   elif a=='-TP'and label=='glew':a='-TC'
   argv.append(a)
  run('compile-'+label,argv);objects.append(str(obj))
 exe=out/'gpu.exe';run('link',[template[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objects,'/Fe'+str(exe),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1','opengl32.lib','gdi32.lib','user32.lib'])
 failures=[]
 for profile in ['compat','core']:
  try:run('run-'+profile,[str(exe),str(args.resources.resolve()),str(args.oracle.resolve()),profile])
  except RuntimeError:failures.append(profile)
 changed=[rel for rel,x in sources.items()if identity(x['frozen']['path'])['sha256']!=x['frozen']['sha256']]
 result=dict(success=not failures and not changed,sources=sources,steps=steps,reused=reused,failures=failures,changed=changed,oracle=identity(args.oracle),resource=identity(args.resources/'sprites/scope_arc.tga'),runner=identity(__file__),liveGameplay=False,boundary='Complete sniper scope and image decode TUs, exact production UploadImage extracted body and hidden real GL driver. Original instruction draw oracle; no game input or final in-game pixel acceptance.')
 (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');return bool(failures or changed)
if __name__=='__main__':raise SystemExit(main())
