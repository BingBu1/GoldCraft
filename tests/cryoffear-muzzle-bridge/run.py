# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual muzzle host/SDK code; exercise lifecycle with a real Glock P model."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import importlib.util

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('environment', 'commands', 'model', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (output, *output.parents)):
        raise ValueError('Use a fresh workspace build directory')
    if sha(args.model) != 'ac158045589ac76fcff9f99ce057872f8037ccdcb8f77acb6314cafca65e9d49':
        raise ValueError('Actual original Glock P model differs')
    output.mkdir(parents=True)
    helper=ROOT/"tests/common/cpp_function.py"
    spec=importlib.util.spec_from_file_location("cpp_function",helper)
    extractor=importlib.util.module_from_spec(spec);spec.loader.exec_module(extractor)
    plugin=ROOT/"native/csol/client/plugin.cpp"
    _,_,callback=extractor.function(plugin.read_bytes(),b"static void StudioDrawPoints()")
    (output/"native-studio-points.inc").write_bytes(callback+b"\n")
    config = json.loads((ROOT/'.tools/clang-toolchain.json').read_text('utf-8-sig'))
    compiler = Path(config['root'])/'bin/clang-cl.exe'
    database = json.loads(args.commands.read_text('utf-8'))
    command = next(row['command'] for row in database if row['file'].replace(chr(92),'/').endswith('/native/csol/client/plugin.cpp'))
    tokens = re.findall(r'"[^"\r\n]*"|[^\s]+', command)
    flags = ['/nologo','/TP','/std:c++20','/EHsc','/MT','/utf-8','/W3','/WX',
             '--target=i686-pc-windows-msvc','/O2','/clang:-O3','/clang:-flto=thin',
             '/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option',
             '/DNDEBUG','/I'+str(output),*[s for s in tokens if s.startswith('-D')],
             *['/imsvc'+s[2:] for s in tokens if s.startswith('-I')]]
    sources = [ROOT/'tests/cryoffear-muzzle-bridge/main.cpp',
               ROOT/'native/csol/client/weapons/guest_cof_muzzle.cpp',
               ROOT/'native/guest/cof/client/muzzle_flash.cpp']
    inputs = set(sources)|{plugin,helper,Path(__file__).resolve(), args.commands.resolve(),args.environment.resolve(),
                          args.model.resolve(),compiler,compiler.with_name('lld-link.exe')}
    environment = os.environ|json.loads(args.environment.read_text('utf-8-sig'))
    temp = output/'temp';temp.mkdir();environment.update(TEMP=str(temp),TMP=str(temp))
    commands = []
    for i, source in enumerate(sources):
        dep = output/f'{i}.d'
        argv = [str(compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        commands.append(argv)
        result = subprocess.run(argv,cwd=ROOT,env=environment,capture_output=True,text=True,timeout=90)
        (output/f'{i}-headers.log').write_text(result.stdout+result.stderr,'utf-8')
        result.check_returncode()
        make = dep.read_text().replace(chr(92)+chr(10),' ')
        inputs.update(Path(p.replace(chr(92)+' ',' ')).resolve() for p in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    before = {str(p):sha(p) for p in sorted(inputs)}
    executable = output/'muzzle-bridge.exe'
    argv = [str(compiler),*flags,'/clang:-fuse-ld=lld',*map(str,sources),'/Fe:'+str(executable),
            '/link','/machine:x86','/threads:1','/opt:ref']
    built = subprocess.run(argv,cwd=output,env=environment,capture_output=True,text=True,timeout=150)
    (output/'compile.log').write_text(built.stdout+built.stderr,'utf-8')
    tested = None
    if not built.returncode:
        tested = subprocess.run([str(executable),str(args.model.resolve())],cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    after = {str(p):sha(p) for p in sorted(inputs)}
    success = not built.returncode and tested is not None and not tested.returncode and before==after
    receipt = dict(success=success,command=argv,dependencyCommands=commands,before=before,after=after,
        unchanged=before==after,compiled=built.returncode,tested=tested.returncode if tested else None,
        sourceCopies=0,originalModelSha256=sha(args.model),
        boundary='Actual host/SDK emitter with real P attachments; scene readiness/phase and engine services are controlled; no full Renderer, gameplay or live performance acceptance')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print((tested.stdout+tested.stderr) if tested else built.stdout+built.stderr,flush=True)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
