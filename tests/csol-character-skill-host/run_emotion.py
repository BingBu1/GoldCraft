#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and execute the actual Action class and J menu entry bodies."""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path

def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def ident(p):
 p=Path(p).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def key(p):return str(Path(p).resolve()).lower()
def function(text,signature):
 a=text.index(signature);i=text.index('{',a)+1;depth=1
 while depth:
  depth+=(text[i]=='{')-(text[i]=='}');i+=1
 return text[a:i]
p=argparse.ArgumentParser(description=__doc__)
for n in ('manifest','sdk-template','output'):p.add_argument('--'+n,type=Path,required=True)
p.add_argument('--negative',choices=['menu-slot5'],help='Inject the old menu slot5 guard into the extracted test unit only')
a=p.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir();sources=out/'source'
selected={};inputs={};manifest=read(a.manifest)
for row in manifest:
 if row['path'] not in ['native/csol/server/scenario_character_skills/host.cpp','native/csol/server/scenario_character_skills/host_actions.cpp','native/csol/server/scenario_character_skills/host_actions.hpp']:continue
 src=a.manifest.resolve().parent/'source'/row['path'];assert ident(src)['sha256']==row['sha256'];selected[row['path']]=src
for rel in ['tests/csol-character-skill-host/action_fixture.hpp','tests/csol-character-skill-host/action_main.inc','tests/csol-character-skill-host/emotion_main.inc','native/csol/scenario/config.cpp','native/csol/scenario/spawn_points.cpp']:
 selected[rel]=root/rel
for src in (root/'native/csol/scenario').rglob('*.hpp'):selected[src.relative_to(root).as_posix()]=src
for rel,src in list(selected.items()):
 dst=sources/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);selected[rel]=dst;inputs[key(dst)]=ident(dst)
host=selected['native/csol/server/scenario_character_skills/host.cpp'].read_text('utf-8-sig')
start=host.index('class Action;\n');end=host.index('std::shared_ptr<Action> FindAction(',start)
blocks=[host[start:end]]
for sig in ['std::shared_ptr<Action> FindAction(','bool CharacterActionPostThink(','bool PreserveCharacterAction(',
 'bool CancelCharacterAction(','float CharacterActionMaxSpeed(','bool OpenCharacterEmotionMenu(',
 'bool CharacterEmotionMenuOpen(','bool SelectCharacterEmotion(']:blocks.append(function(host,sig))
negative=None
if a.negative:
 before,after,oracle={
  'menu-slot5':('if ((!owner || slot != 5) && (slot < 1 || slot > row.emotionCount))',
   'if (slot != 5 && (slot < 1 || slot > row.emotionCount))','menu honors exact inclusive row slot count, including slot5')}[a.negative]
 assert blocks[0].count(before)==1,'negative regression point must be unique'
 blocks[0]=blocks[0].replace(before,after)
 negative=dict(kind=a.negative,before=before,after=after,requiredFailure=oracle)
unit=out/'production.cpp';unit.write_text('#include "source/tests/csol-character-skill-host/action_fixture.hpp"\nconstexpr int EmotionMenu=15;\n'+
 '\n'.join(blocks)+'\n#include "source/tests/csol-character-skill-host/emotion_main.inc"\n',encoding='utf8')
inputs[key(unit)]=ident(unit)
template=read(a.sdk_template/'prepared.json');known={key(x['path']):x for x in template['inputs']};known.update(inputs)
compiler=next(j['command'][0] for j in template['jobs'] if j['name']=='lifecycle_sdk')
env=os.environ.copy();env.update(template['environment']);env.update(TEMP=str(out),TMP=str(out))
exe=out/'emotion.exe'
cmd=[compiler,'/nologo','/std:c++20','/EHsc','/MT','/W4','/utf-8','/showIncludes','/arch:AVX2','/fp:precise',
 '--target=i686-pc-windows-msvc','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld',str(unit),
 str(selected['native/csol/server/scenario_character_skills/host_actions.cpp']),
 str(selected['native/csol/scenario/config.cpp']),str(selected['native/csol/scenario/spawn_points.cpp']),'/Fe'+str(exe)]
for f in [Path(__file__),a.manifest,a.sdk_template/'prepared.json',Path(compiler)]:inputs[key(f)]=ident(f)
result=dict(success=False,sourceHost=ident(selected['native/csol/server/scenario_character_skills/host.cpp']),
 negative=negative,
 sourceBodies=[dict(sha256=hashlib.sha256(x.encode()).hexdigest(),bytes=len(x.encode())) for x in blocks],command=cmd,
 extraction='Verbatim Action registry/class, FindAction and seven public menu/lifecycle entry bodies. Engine/entity boundaries remain explicit test fixtures.',inputs=list(inputs.values()))
def save():(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
try:
 began=time.monotonic();c=subprocess.run(cmd,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
 (out/'build.log').write_bytes(c.stdout);result.update(compileExit=c.returncode,compileSeconds=time.monotonic()-began)
 included=sorted(set(key(x.strip()) for x in re.findall(r'Note: including file:\s*(.+)',c.stdout.decode('utf8',errors='replace'))))
 result['unknown']=[x for x in included if x not in known]
 result['actualIncludes']=[ident(x) for x in included]
 result['changedIncludes']=[x for x in included if x in known and ident(x)['sha256']!=known[x]['sha256']]
 save();assert not c.returncode and not result['unknown'] and not result['changedIncludes'],'compile/include verification failed'
 run=subprocess.run([str(exe)],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
 (out/'execute.log').write_bytes(run.stdout);result.update(exit=run.returncode,output=run.stdout.decode('utf8',errors='replace'),exe=ident(exe))
 result['changedAfter']=[x['path'] for x in inputs.values() if ident(x['path'])['sha256']!=x['sha256']]
 result['expectedOutcomeObserved']=(run.returncode==1 and any(line.strip()=='FAIL '+negative['requiredFailure'] for line in result['output'].splitlines())) if negative else run.returncode==0
 result['success']=result['expectedOutcomeObserved'] and not result['changedAfter'];save();print(result['output'])
except BaseException as error:result['error']=str(error);save();print(result['error'])
raise SystemExit(0 if result['success'] else 1)
