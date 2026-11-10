# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the production menu font helper against the pinned host VGUI SDK."""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def identity(p):
 return dict(path=str(p.relative_to(ROOT)),sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--compiler',type=Path,required=True)
 p.add_argument('--environment',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 a=p.parse_args();out=a.output.resolve()
 if out.exists() or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace build output')
 out.mkdir(parents=True)
 inputs={ROOT/'native/csol/client/menu_font.hpp',ROOT/'tests/csol-menu-font/main.cpp',Path(__file__).resolve(),ROOT/'tests/csol-hud-style/surface_stub.hpp',a.environment.resolve()}
 sdk=ROOT/'external/MetaHookSv-20261007/MetaHook/include'
 cmd=[str(a.compiler),'/nologo','--target=i686-pc-windows-msvc','-fuse-ld=lld','/std:c++20','/EHsc','/MT','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/DNOMINMAX','/DWIN32',
      '/I'+str(ROOT),'/I'+str(sdk),'/I'+str(sdk/'Interface'),'/I'+str(sdk/'Interface/VGUI'),
      '/I'+str(sdk/'SourceSDK'),'/I'+str(sdk/'SourceSDK/tier1'),'/I'+str(sdk/'VGUI'),str(ROOT/'tests/csol-menu-font/main.cpp'),'/Fo'+str(out/'font.obj'),'/Fe:'+str(out/'font.exe'),'/link','/machine:x86']
 includes=[Path(x[2:]) for x in cmd if x.startswith('/I')]
 pending=[x for x in inputs if x.suffix in ('.cpp','.hpp')]
 while pending:
  source=pending.pop()
  for bracket,name in re.findall(r'^\s*#\s*include\s*([<"])([^>"\n]+)[>"]',source.read_text('utf-8-sig',errors='replace'),re.M):
   candidates=([source.parent/name] if bracket=='"' else [])+[base/name for base in includes]
   found=next((x.resolve() for x in candidates if x.is_file() and x.resolve().is_relative_to(ROOT)),None)
   if found and found not in inputs:inputs.add(found);pending.append(found)
 inputs=sorted(inputs);before=[identity(x) for x in inputs]
 env=os.environ|json.loads(a.environment.read_text('utf-8-sig'))|{'TEMP':str(out),'TMP':str(out)}
 compiled=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,errors='replace')
 (out/'compile.log').write_text(compiled.stdout+compiled.stderr)
 run=subprocess.run([str(out/'font.exe')],cwd=out,capture_output=True,text=True) if compiled.returncode==0 else None
 if run:(out/'test.log').write_text(run.stdout+run.stderr)
 after=[identity(x) for x in inputs]
 receipt=dict(command=cmd,compiler=dict(path=str(a.compiler),sha256=hashlib.sha256(a.compiler.read_bytes()).hexdigest(),version=subprocess.run([str(a.compiler),'--version'],capture_output=True,text=True).stdout),compile=compiled.returncode,test=run.returncode if run else None,inputsBefore=before,inputsAfter=after,unchanged=before==after,success=compiled.returncode==0 and run is not None and run.returncode==0 and before==after,boundaries='actual menu_font.hpp with pinned SDK; controlled surface font metrics, no GDI pixels/gameplay/FPS')
 (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print(compiled.stdout+compiled.stderr)
 if run:print(run.stdout+run.stderr)
 raise SystemExit(0 if receipt['success'] else 1)
if __name__=='__main__':main()
