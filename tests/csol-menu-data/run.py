# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile production menu parsers directly and consume real generated resources."""
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
    parser.add_argument('--buy',type=Path,required=True)
    parser.add_argument('--characters',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    output=args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    inputs=[Path(__file__).resolve(),ROOT/'tests/csol-menu-data/main.cpp',
            ROOT/'native/csol/buy_menu.cpp',ROOT/'native/csol/buy_menu.hpp',
            ROOT/'native/csol/character_menu.cpp',ROOT/'native/csol/character_menu.hpp',
            args.buy.resolve(),args.characters.resolve()]
    def identities():
        return [dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                for path in inputs]
    before=identities()
    environment=os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    rapidjson=ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'
    executable=output/'menu-data.exe'
    command=[str(args.compiler),'/nologo','--target=i686-pc-windows-msvc','/std:c++20',
             '/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin',
             '/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/I'+str(ROOT),
             '/I'+str(rapidjson),str(inputs[1]),str(inputs[2]),str(inputs[4]),
             '/Fe:'+str(executable),'/link','/machine:x86','/threads:1']
    compiled=subprocess.run(command,cwd=output,env=environment,capture_output=True,
                            text=True,encoding='utf-8',errors='replace',timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8')
    tested=None
    if compiled.returncode==0:
        tested=subprocess.run([str(executable),str(args.buy.resolve()),str(args.characters.resolve())],
                              cwd=output,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    after=identities()
    receipt=dict(command=command,compile=compiled.returncode,
                 test=tested.returncode if tested else None,inputsBefore=before,
                 inputsAfter=after,sourceCopies=0,unchanged=before==after,
                 success=compiled.returncode==0 and tested is not None and tested.returncode==0 and before==after,
                 boundary='Actual production parser and generated data; no rendering or gameplay')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print((tested.stdout+tested.stderr) if tested else (compiled.stdout+compiled.stderr))
    raise SystemExit(0 if receipt['success'] else 1)


if __name__=='__main__':main()
