#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check actual host phase protocol, presentation lifetime and viewport geometry."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
def identity(path):
    path=path.resolve(strict=True)
    return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','environment','output'):parser.add_argument('--'+key,type=Path,required=True)
    args=parser.parse_args();out=args.output.resolve()
    if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh workspace receipt directory')
    out.mkdir(parents=True)
    source=ROOT/'tests/csol-challenge-hud/protocol.cpp'
    paths=[source,ROOT/'native/csol/scenario/custom/phase_snapshot.hpp',ROOT/'native/csol/scenario/custom/phase_art.hpp',ROOT/'native/csol/scenario/custom/phase.hpp',Path(__file__),args.compiler,args.environment]
    before=[identity(p)for p in paths];env=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    command=[str(args.compiler.resolve()),'/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/clang:-O3','/clang:-flto=thin','/clang:-fuse-ld=lld',str(source),'/Fe:'+str(out/'phase.exe'),'/link','/machine:x86','/threads:1','/opt:lldltojobs=1']
    compiled=subprocess.run(command,cwd=out,env=env,capture_output=True,timeout=180)
    (out/'compile.log').write_bytes(compiled.stdout+compiled.stderr)
    run=subprocess.run([out/'phase.exe'],cwd=out,capture_output=True,timeout=30)if compiled.returncode==0 else None
    text=(run.stdout+run.stderr).decode('utf8','replace')if run else'';(out/'test.log').write_text(text,'utf8')
    match=re.fullmatch(r'challenge phase HUD: (\d+) checks passed\s*',text)
    stable=before==[identity(p)for p in paths]
    result=dict(success=compiled.returncode==0 and run is not None and run.returncode==0 and bool(match)and stable,inputsUnchanged=stable,checks=int(match[1])if match else 0,command=command,inputs=before,boundary='Production protocol/state/layout/art only; actual VGUI and dedicated message consumer require separate validation',deployed=False,gameplay=False)
    (out/'receipt.json').write_text(json.dumps(result,indent=2),'utf8');print(json.dumps({k:result[k]for k in ('success','inputsUnchanged','checks')}))
    if not result['success']:print((compiled.stdout+compiled.stderr).decode('utf8','replace')[-4000:]if compiled.returncode else text)
    raise SystemExit(0 if result['success']else 1)
if __name__=='__main__':main()
