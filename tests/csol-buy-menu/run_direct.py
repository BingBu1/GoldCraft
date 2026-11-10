# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the entire actual buy client with its matching SDK and controls library."""
import argparse, hashlib, json, os, re, shlex, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def identity(path):
 return dict(path=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--output',type=Path,required=True)
 p.add_argument('--resources',type=Path,required=True)
 p.add_argument('--environment',type=Path,default=ROOT/'build/csol-season2-trace-20261109/compiler-env.json')
 p.add_argument('--compile-database',type=Path,default=ROOT/'build/csol-client-crc/client/compile_commands.json')
 a=p.parse_args();out=a.output.resolve();resources=a.resources.resolve()
 if out.exists() or not out.is_relative_to(ROOT/'build'):raise ValueError('fresh build directory required')
 out.mkdir(parents=True)
 entry=next(x for x in json.loads(a.compile_database.read_text()) if x['file'].replace(chr(92),'/').endswith('/client/buy_menu.cpp'))
 original=shlex.split(entry['command'],posix=False)
 compiler=original[0].replace(chr(92)*2,chr(92));flags=[]
 for flag in original[1:]:
  if flag in ('-c','--','-TP') or flag.startswith(('/Fo','/Fd')) or flag==original[-1]:continue
  if flag=='-DCSOL_RESOURCE_REFRESH=0':flag='-DCSOL_RESOURCE_REFRESH=1'
  flags.append(flag.replace(chr(92)*2,chr(92)))
 flags+=['/I'+str(ROOT)]
 flags+=['-D'+v for v in ['VGUI_USE_SURFACE2','VGUI_USE_SCHEME2','VGUI_USE_INPUT2','NO_MALLOC_OVERRIDE','NO_TIER0_MINIDUMP','NO_VCR']]
 flags+=['-I'+str(ROOT/'external/MetaHookSv/MetaHook/include'/v) for v in ['VGUI','Interface/VGUI','vgui_controls','SourceSDK/vstdlib']]
 flags+=['-I'+str(ROOT/'external/MetaHookSv/Plugins/VGUI2Extension/include/Interface')]
 library=ROOT/'build/csol-client-crc/client/csol_damage_vgui.lib'
 sources=[ROOT/'tests/csol-buy-menu/direct.cpp',ROOT/'native/csol/buy_menu.cpp',ROOT/'native/csol/image_codec.cpp']
 tracked=set(sources+[Path(__file__),library,a.compile_database.resolve(),a.environment.resolve()])
 # Resolve actual quoted and angle includes against the production search paths.
 includes=[ROOT]+[Path(x[2:]) for x in flags if x.startswith(('-I','/I'))]
 pending=list(sources)
 while pending:
  source=pending.pop()
  for bracket,name in re.findall(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]',source.read_text('utf-8-sig',errors='replace'),re.M):
   options=([source.parent/name] if bracket=='"' else [])+[base/name for base in includes]
   found=next((x.resolve() for x in options if x.is_file() and x.resolve().is_relative_to(ROOT)),None)
   if found and found not in tracked:tracked.add(found);pending.append(found)
 tracked.update(x.resolve() for x in resources.rglob('*') if x.is_file())
 manifest=resources.with_name(resources.name+'.manifest.json')
 if manifest.exists():tracked.add(manifest)
 before=[identity(x) for x in sorted(tracked)]
 env=os.environ|json.loads(a.environment.read_text('utf-8-sig'));steps=[]
 def run(name,command):
  result=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,errors='replace',timeout=180);log=result.stdout+result.stderr;(out/(name+'.log')).write_text(log,encoding='utf-8');steps.append(dict(name=name,command=command,exit=result.returncode));print(name+' exit='+str(result.returncode));print(log[-9000:] if result.returncode or name=='test' else '');return result.returncode==0
 objects=[];ok=True
 for i,source in enumerate(sources):
  obj=out/(source.stem+str(i)+'.obj');objects.append(obj)
  if not run('compile-'+str(i),[compiler]+flags+['/c',str(source),'/Fo'+str(obj)]):ok=False;break
 if ok:ok=run('link',[compiler,'/nologo','--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT']+[str(x) for x in objects]+[str(library),'/Fe'+str(out/'buy.exe'),'/link','/machine:x86','/OPT:REF','/OPT:ICF','user32.lib','gdi32.lib','imm32.lib','winmm.lib','advapi32.lib'])
 if ok:ok=run('test',[str(out/'buy.exe'),str(resources)])
 after=[identity(x) for x in sorted(tracked)]
 revision=subprocess.run(['git','rev-parse','HEAD'],cwd=ROOT,capture_output=True,text=True).stdout.strip()
 compilerIdentity=dict(path=compiler,sha256=hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),version=subprocess.run([compiler,'--version'],capture_output=True,text=True).stdout)
 receipt=dict(success=ok and before==after,inputsUnchanged=before==after,revision=revision,compiler=compilerIdentity,resourceRefresh=1,sourceMode='complete production translation unit; no function extraction',boundaries='controlled engine/input/surface; actual upstream controls linked; no live VGUI pixels or gameplay',steps=steps,inputsBefore=before,inputsAfter=after)
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');raise SystemExit(0 if receipt['success'] else 1)
if __name__=='__main__':main()
