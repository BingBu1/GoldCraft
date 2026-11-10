#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build current sources directly; retain commands, logs, revision and hashes."""
import argparse,hashlib,json,os,re,shlex,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def record(p):
 p=Path(p).resolve();d=p.read_bytes();return dict(path=str(p),bytes=len(d),sha256=hashlib.sha256(d).hexdigest())
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--compile-commands',type=Path,required=True);ap.add_argument('--environment',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);ap.add_argument('--menu',type=Path,required=True);a=ap.parse_args();out=a.output.resolve()
 if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a fresh workspace build output')
 out.mkdir(parents=True);plan=json.loads(a.compile_commands.read_text('utf-8-sig'));env=os.environ|json.loads(a.environment.read_text('utf-8-sig'))
 command=next(j['command'] for j in plan if j['file'].replace('\\','/').endswith('/client/characters.cpp'));compiler=shlex.split(command,posix=False);flags=[];skip=False
 for v in compiler[1:]:
  if v=='--':break
  if skip:skip=False;continue
  if v=='/clang:-ivfsoverlay':skip=True;continue
  if v=='-c' or v.startswith(('/Fo','/Fd')) or ('\\build\\' in v and v.startswith('-I')):continue
  flags.append(v)
 flags.extend(['-I'+str(ROOT/'native/csol'),'-I'+str(ROOT/'native/csol/client')])
 flags.extend('-D'+v for v in ['VGUI_USE_SURFACE2','VGUI_USE_SCHEME2','VGUI_USE_INPUT2','NO_MALLOC_OVERRIDE','NO_TIER0_MINIDUMP','NO_VCR'])
 flags.extend('-I'+str(ROOT/'external/MetaHookSv/MetaHook/include'/v) for v in ['VGUI','Interface/VGUI','vgui_controls','SourceSDK/vstdlib'])
 flags.append('-I'+str(ROOT/'external/MetaHookSv/Plugins/VGUI2Extension/include/Interface'))
 library=ROOT/'build/csol-client-crc/client/csol_damage_vgui.lib'
 inputs={};pending=[ROOT/p for p in ['tests/csol-characters/frame.cpp','native/csol/character_menu.cpp','native/csol/character_catalog.cpp','native/csol/image_codec.cpp',str(Path(__file__).relative_to(ROOT))]];pending.extend([a.menu.resolve(),a.compile_commands.resolve(),a.environment.resolve(),library])
 includes=[ROOT]+[Path(v[2:]) for v in flags if v.startswith(('-I','/I'))]
 while pending:
  p=pending.pop().resolve()
  if p in inputs:continue
  if not p.is_relative_to(ROOT):raise ValueError('Source outside workspace')
  inputs[p]=record(p)
  for bracket,name in re.findall(rb'^\s*#\s*include\s*([<"])([^>"\r\n]+)[>"]',p.read_bytes(),re.M):
   name=name.decode();f=next((v for v in ([p.parent/name] if bracket==b'"' else [])+[base/name for base in includes] if v.is_file() and v.resolve().is_relative_to(ROOT)),None)
   if f:pending.append(f)
 revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip();jobs=[];success=False
 def execute(name,argv):
  start=time.monotonic();r=subprocess.run(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180);log=out/(name+'.log');log.write_bytes(r.stdout);jobs.append(dict(name=name,command=argv,exitCode=r.returncode,seconds=time.monotonic()-start,log=record(log)));(out/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf8');print(name,r.returncode,flush=True)
  if r.returncode:print(r.stdout.decode('utf8','replace')[-7000:]);raise RuntimeError(name)
 def compile(name,path):execute('compile-'+name,[compiler[0],*flags,'-I'+str(ROOT),'/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.compile.pdb')),'-c','--',str(ROOT/path)])
 try:
  for name,path in [('frame','tests/csol-characters/frame.cpp'),('menu','native/csol/character_menu.cpp'),('catalog','native/csol/character_catalog.cpp'),('codec','native/csol/image_codec.cpp')]:compile(name,path)
  execute('link-frame',[compiler[0],'/nologo','/clang:-fuse-ld=lld',*[str(out/(n+'.obj')) for n in ('frame','menu','catalog','codec')],str(library),'/Fe:'+str(out/'frame.exe'),'/link','/machine:x86','/OPT:REF','/threads:1','user32.lib','gdi32.lib','imm32.lib','winmm.lib','advapi32.lib'])
  execute('run-frame',[str(out/'frame.exe'),str(a.menu.resolve())])
  success=True
 finally:
  unchanged=all(record(p)==v for p,v in inputs.items());(out/'receipt.json').write_text(json.dumps(dict(success=success and unchanged,inputsUnchanged=unchanged,revision=revision,inputs=list(inputs.values()),jobs=jobs,boundary='Current x86 characters.cpp included directly, actual menu/catalog/image parsers; controlled engine/input/scheme/font/surface. No DLL, deployment, GPU or gameplay.'),indent=2),encoding='utf8')
  if not unchanged:raise RuntimeError('Source changed during run')
if __name__=='__main__':main()
