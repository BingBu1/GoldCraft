# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the production layout helper directly, without a source snapshot."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--environment',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    output=args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a new workspace build directory')
    output.mkdir(parents=True)
    paths=[ROOT/'native/csol/menu_layout.hpp',Path(__file__),ROOT/'tests/csol-menu-layout/main.cpp']
    def identities():
        return [dict(path=str(path.relative_to(ROOT)),sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for path in paths]
    before=identities()
    command=[str(args.compiler),'/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/O2','/clang:-O3','/arch:AVX2','/fp:precise','/I'+str(ROOT),str(paths[2]),'/Fe:'+str(output/'layout.exe'),'/link','/machine:x86']
    compiled=subprocess.run(command,cwd=output,env=os.environ|json.loads(args.environment.read_text('utf-8-sig')),capture_output=True,text=True,errors='replace')
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr)
    test=subprocess.run([str(output/'layout.exe')],cwd=output,capture_output=True,text=True) if compiled.returncode==0 else None
    if test:(output/'test.log').write_text(test.stdout+test.stderr)
    after=identities()
    receipt=dict(command=command,compile=compiled.returncode,test=test.returncode if test else None,inputsBefore=before,inputsAfter=after,unchanged=before==after,success=compiled.returncode==0 and test is not None and test.returncode==0 and before==after)
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(compiled.stdout+compiled.stderr)
    if test:print(test.stdout+test.stderr)
    raise SystemExit(0 if receipt['success'] else 1)

if __name__=='__main__':main()
