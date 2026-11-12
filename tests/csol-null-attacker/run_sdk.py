#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Targeted current SDK replacement: two actual changed TUs plus real fixture.

Reuse the approved full-build object seal; do not build a DLL or instantiate
Scenario Session/Models whose current layout differs from the sealed objects.
"""
import sys
sys.dont_write_bytecode=True
import json,hashlib,subprocess,os,re,time,argparse
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];CWD=ROOT/'external/ReGameDLL_CS/regamedll/msvc'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def ident(p):p=Path(p).resolve();return dict(path=str(p),sha256=sha(p),bytes=p.stat().st_size)
def main():
 p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);p.add_argument('--reuse',type=Path);args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False);(out/'temp').mkdir()
 sealpath=ROOT/'build/boss-room-cache-full-20261112-01/server/cache-link-inputs.json';seal=json.loads(sealpath.read_text(encoding='utf8'));environment=json.loads((ROOT/'build/csol-season2-trace-20261109/compiler-env.json').read_text(encoding='utf8'))
 for row in seal['linkInputs']:assert sha(row['path'])==row['sha256'],('sealed object changed',row['path'])
 env=dict(os.environ)
 for k,v in environment.items():
  for old in list(env):
   if old.casefold()==k.casefold():del env[old]
  env[k]=v
 env['TEMP']=env['TMP']=str(out/'temp')
 basecmd=[x for x in seal['compileCommand'] if not(x.lower().endswith(('.cpp','.c')) or x.lower().startswith(('/yu','/yc','/fo','/fd')) or (x.lower().startswith('/fp') and not x.lower().startswith('/fp:')))]
 (out/'sdk_util.h').write_text('#pragma once\n#include \"'+str(ROOT/'external/ReGameDLL_CS/regamedll/dlls/util.h').replace('\\','/')+'\"\n')
 basecmd+=['/I'+str(out),'/Y-','/showIncludes','/I'+str(ROOT),'/I'+str(ROOT/'build/scenario-sdk-current-20261108-05')]
 sources=[ROOT/'external/ReGameDLL_CS/regamedll/dlls/player.cpp',ROOT/'external/ReGameDLL_CS/regamedll/dlls/multiplay_gamerules.cpp',ROOT/'tests/csol-null-attacker/main.cpp']
 tracked={str(p.resolve()):ident(p) for p in sources+[ROOT/'native/server/native_armor_commit.inc',Path(__file__),sealpath]};steps=[]
 for row in seal['linkInputs']:tracked[str(Path(row['path']).resolve())]=ident(row['path'])
 result=dict(success=False,fullDLL=False,sourceCopies=0,deployed=False,liveGameplay=False,scenarioInstantiated=False,layoutBoundary='Native mode0 only; no Scenario/Models/Session objects instantiate the current changed layout against old sealed support objects.',reusedObjectSeal=ident(sealpath),replacements=[],steps=steps)
 def save():result['inputs']=list(tracked.values());(out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n')
 def job(name,command):
  rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf8');started=time.monotonic()
  r=subprocess.run([command[0],'@'+str(rsp)] if name!='behavior' else command,cwd=CWD,env=env,capture_output=True,timeout=240)
  log=out/(name+'.log');log.write_bytes(r.stdout+r.stderr);text=log.read_text(encoding='utf8',errors='replace');steps.append(dict(name=name,command=command,exit=r.returncode,seconds=time.monotonic()-started,log=str(log)))
  for path in re.findall(r'Note: including file:\s*([^\r\n]+)',text):
   d=(CWD/Path(path.strip())).resolve();tracked[str(d)]=ident(d)
  print(name,r.returncode,flush=True);save()
  if r.returncode:print(text[-12000:],flush=True);raise RuntimeError(name+' failed')
 for src in sources:
  obj=out/(src.stem+'.obj')
  prior=args.reuse.resolve() if args.reuse and src.stem!='main' else None
  if prior:
   receipt=json.loads((prior/'receipt.json').read_text());r=next(r for r in receipt['replacements'] if r['source']['path']==str(src.resolve()));assert sha(r['object']['path'])==r['object']['sha256']
   if sha(src)==r['source']['sha256']:obj=Path(r['object']['path'])
   else:job(src.stem,basecmd+['/Fo'+str(obj),'/Fd'+str(out/(src.stem+'.pdb')),str(src)])
  else:job(src.stem,basecmd+['/Fo'+str(obj),'/Fd'+str(out/(src.stem+'.pdb')),str(src)])
  result['replacements'].append(dict(source=ident(src),object=ident(obj)))
 remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:','/subsystem:','/threads:','/opt:lldltojobs=','/lldltocache:')
 link=[]
 for x in seal['linkCommand']:
  if x.lower()=='/dll' or x.lower().startswith(remove):continue
  if Path(x).name.lower() in ('player.obj','multiplay_gamerules.obj'):x=next(r['object']['path'] for r in result['replacements'] if Path(r['object']['path']).name.lower()==Path(x).name.lower())
  link.append(x)
 link+=['/SUBSYSTEM:CONSOLE','/OUT:'+str(out/'null-attacker.exe'),'/PDB:'+str(out/'null-attacker.pdb'),'/threads:1','/opt:lldltojobs=1','/lldltocache:'+str(ROOT/'build/null-attacker-lto-cache-20261112'),str(out/'main.obj')]
 job('link',link);job('behavior',[str(out/'null-attacker.exe')]);text=(out/'behavior.log').read_text(encoding='utf8');assert re.search(r'null_attacker_sdk checks=[1-9]\d* failures=0',text)
 result['changedInputs']=[row['path'] for row in tracked.values() if sha(row['path'])!=row['sha256']];result['success']=not result['changedInputs'];result['executable']=ident(out/'null-attacker.exe');save();print(text.strip());return 0 if result['success'] else 1
if __name__=='__main__':raise SystemExit(main())
