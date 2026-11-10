#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build current sources directly; retain commands, logs, revision and hashes."""
import argparse,hashlib,json,os,re,shlex,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def record(p):
 p=Path(p).resolve();d=p.read_bytes();return dict(path=str(p),bytes=len(d),sha256=hashlib.sha256(d).hexdigest())
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--compile-commands',type=Path,required=True);ap.add_argument('--environment',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--suite',choices=['all','hud','menu'],default='all');a=ap.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh workspace build output')
 out.mkdir(parents=True);plan=json.loads(a.compile_commands.read_text('utf-8-sig'));env=os.environ|json.loads(a.environment.read_text('utf-8-sig'))
 command=next(j['command'] for j in plan if j['file'].replace('\\','/').endswith('/client/hud_style.cpp'));compiler=shlex.split(command,posix=False);flags=[];skip=False
 for v in compiler[1:]:
  if v=='--':break
  if skip:skip=False;continue
  if v=='/clang:-ivfsoverlay':skip=True;continue
  if v=='-c' or v.startswith(('/Fo','/Fd')) or ('\\build\\' in v and v.startswith('-I')):continue
  flags.append(v)
 flags.extend(['-I'+str(ROOT/'native/csol'),'-I'+str(ROOT/'native/csol/client')])
 inputs={};pending=[ROOT/p for p in ['tests/csol-hud-style/client.cpp','tests/csol-hud-style/weapon_selection.cpp','tests/csol-hud-style/pure.cpp','native/csol/hud_style.cpp','native/csol/image_codec.cpp','native/csol/client/weapons/weapon_inventory.cpp','native/csol/client/resource_font_memory.cpp','tests/csol-weapon-slots/menu.cpp','native/csol/weapons/weapon_slots.cpp',str(Path(__file__).relative_to(ROOT))]]
 while pending:
  p=pending.pop().resolve()
  if p in inputs:continue
  if not p.is_relative_to(ROOT):raise ValueError('Source outside workspace')
  inputs[p]=record(p)
  for name in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"',p.read_bytes(),re.M):
   name=name.decode();f=next((v for v in [p.parent/name,ROOT/name] if v.is_file()),None)
   if f:pending.append(f)
 revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();jobs=[];success=False
 def execute(name,argv):
  start=time.monotonic();r=subprocess.run(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180);log=out/(name+'.log');log.write_bytes(r.stdout);jobs.append(dict(name=name,command=argv,exitCode=r.returncode,seconds=time.monotonic()-start,log=record(log)));(out/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf8');print(name,r.returncode,flush=True)
  if r.returncode:print(r.stdout.decode('utf8','replace')[-7000:]);raise RuntimeError(name)
 def compile(name,path):execute('compile-'+name,[compiler[0],*flags,'-I'+str(ROOT),'/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.compile.pdb')),'-c','--',str(ROOT/path)])
 try:
  compile('inventory','native/csol/client/weapons/weapon_inventory.cpp')
  def link_run(name,objects):
   execute('link-'+name,[compiler[0],'/nologo','/clang:-fuse-ld=lld',*[str(out/(n+'.obj')) for n in objects],'/Fe:'+str(out/(name+'.exe')),'/link','/machine:x86','/OPT:REF','user32.lib','gdi32.lib']);execute('run-'+name,[str(out/(name+'.exe'))])
  if a.suite!='menu':
   for n,p in [('style','native/csol/hud_style.cpp'),('codec','native/csol/image_codec.cpp'),('font-memory','native/csol/client/resource_font_memory.cpp')]:compile(n,p)
   for name,path in [('client','tests/csol-hud-style/client.cpp'),('selection','tests/csol-hud-style/weapon_selection.cpp'),('pure','tests/csol-hud-style/pure.cpp')]:
    compile(name,path);link_run(name,[name,'style','codec','font-memory'])
  if a.suite!='hud':
   compile('slots','native/csol/weapons/weapon_slots.cpp');compile('menu','tests/csol-weapon-slots/menu.cpp');link_run('menu',['menu','inventory','slots'])
  success=True
 finally:
  unchanged=all(record(p)==v for p,v in inputs.items());(out/'receipt.json').write_text(json.dumps(dict(success=success and unchanged,inputsUnchanged=unchanged,revision=revision,inputs=list(inputs.values()),jobs=jobs,boundary='Actual x86 HUD consumer and inventory TU; controlled pending inventory, engine/font/surface/resources. No DLL, deployment, GPU or gameplay.'),indent=2),encoding='utf8')
  if not unchanged:raise RuntimeError('Source changed during run')
if __name__=='__main__':main()
