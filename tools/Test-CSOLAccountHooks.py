"""Run exact production AddAccount chain with controlled rules/entity engine edges."""
from pathlib import Path
import argparse, subprocess, json, hashlib, os
from csol_account_fixture import ROOT, account_source

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--clang',required=True);p.add_argument('--no-add',action='store_true')
a=p.parse_args();output=a.output.resolve()
if not output.is_relative_to(ROOT/'build'):raise ValueError('workspace build only')
output.mkdir(parents=True,exist_ok=False)
source,inputs=account_source(add=not a.no_add);tail=ROOT/'tests/csol-money/hooks.cpp';inputs.append(tail)
identity=lambda path:dict(path=path.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
before=[identity(x) for x in inputs]
generated=output/'hooks.cpp';generated.write_text(source+tail.read_text('utf-8'),'utf-8')
exe=output/'hooks.exe'
cmd=[a.clang,'--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/I'+str(ROOT),'/I'+str(ROOT/'external/ReGameDLL_CS/regamedll/public/regamedll'),str(generated),'/Fe'+str(exe),'/Fo'+str(output)+'/', '/link','/MACHINE:X86']
(output/'command.json').write_text(json.dumps(cmd,indent=2),'utf-8')
c=subprocess.run(cmd,cwd=output,capture_output=True,text=True);(output/'compile.log').write_text(c.stdout+c.stderr,'utf-8')
r=None
if c.returncode==0:
    r=subprocess.run([str(exe)],capture_output=True,text=True);(output/'test.log').write_text(r.stdout+r.stderr,'utf-8');print(r.stdout+r.stderr)
after=[identity(x) for x in inputs]
(output/'receipt.json').write_text(json.dumps(dict(compileExit=c.returncode,testExit=None if r is None else r.returncode,regamedllAdd=not a.no_add,inputsBefore=before,inputsAfter=after,inputsUnchanged=before==after,boundary='Exact production wrapper/selected original/guard stack and actual hookchains_impl.h plus registry implementation. SDK scope and entity access/serial are controlled edges; no full server or Pawn VM.'),indent=2),'utf-8')
raise SystemExit(c.returncode or (r.returncode if r is not None else 1) or (0 if before==after else 1))
