# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual muzzle host/SDK code; exercise lifecycle with a real Glock P model."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import struct
import importlib.util

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('environment', 'commands', 'model', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--p345-model',type=Path)
    parser.add_argument('--revolver-model',type=Path)
    parser.add_argument('--vp70-model',type=Path)
    parser.add_argument('--tmp-model',type=Path)
    parser.add_argument('--mp5-model',type=Path)
    parser.add_argument('--m16-model',type=Path)
    parser.add_argument('--g43-model',type=Path)
    parser.add_argument('--shotgun-model',type=Path)
    parser.add_argument('--vp70-attachment-proof',type=Path)
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build') or any(
            p.is_symlink() or os.path.isjunction(p) for p in (output, *output.parents)):
        raise ValueError('Use a fresh workspace build directory')
    if sha(args.model) != 'ac158045589ac76fcff9f99ce057872f8037ccdcb8f77acb6314cafca65e9d49':
        raise ValueError('Actual original Glock P model differs')
    if args.p345_model and sha(args.p345_model)!='c090798e8754084506c6859c268c4db08961c3b0d609afba6a14e50457626bb0':
        raise ValueError('Actual original P345 P model differs')
    if args.revolver_model and (not args.p345_model or sha(args.revolver_model)!='2a54b7b911ed2e5145947eed39779db5ec83b6707701eeaf9826cb73f8f69312'):
        raise ValueError('Use the actual Revolver P model together with the P345 model')
    if args.tmp_model and (not args.vp70_model or sha(args.tmp_model)!='ad599b80f9e9d4e37cbb0f981d218a7741f414bfa7d3ddfd4be86368273da87f'):
        raise ValueError('Use actual TMP P together with all four verified pistol models')
    if args.mp5_model and (not args.tmp_model or sha(args.mp5_model)!='7390f49357e9fcdc726324447ee9603d438eb673423e67be0fa876b7dc14e1a2'):
        raise ValueError('Use the reviewed resident MP5 P with its authored muzzle attachment and all five earlier models')
    if args.m16_model and (not args.mp5_model or sha(args.m16_model)!='69e88ff618b4000e572a067712247f0e5f9f7ea4e886d51080ee0d6dc00a5fd9'):
        raise ValueError('Use original police M16 P together with the earlier six models')
    if args.shotgun_model and (not args.g43_model or sha(args.shotgun_model)!='60911831719dc57cca1bcb34801c8b74e24443372afe13d5230e336316a5a5bc'):
        raise ValueError('Use original police 870 P together with the earlier eight models')
    vp70_inputs=[]
    if args.g43_model and (not args.m16_model or sha(args.g43_model)!='e5a2a3e637a8e1234a4a53b90b85b9a347203a442aa5b6722623ad465c101613'):
        raise ValueError('Use original police G43 P together with the earlier seven models')
    if args.vp70_model:
        if not args.p345_model or not args.revolver_model:
            raise ValueError('Use the VP70 P model together with P345 and Revolver')
        original_hash='a4412beb27cbd51478651b87290bb1bff66133478229c56c5879136c5cab93e8'
        if args.vp70_attachment_proof:
            proof=json.loads(args.vp70_attachment_proof.read_text('utf8'))
            source=Path(proof['source']['path']);profile=Path(proof['profile']['path'])
            tool=ROOT/'tools/Author-MissingStudioAttachment.py';helper=ROOT/'tools/cryoffear_studio.py'
            raw=source.read_bytes();derived=args.vp70_model.read_bytes()
            at=struct.unpack_from('<i',raw,188)[0]
            textures,table=struct.unpack_from('<ii',raw,180)
            if not (244<=at<=len(raw) and 0<textures<=1000 and 244<=table and table+80*textures<=at and
                    len(derived)==len(raw)+88 and struct.unpack_from('<ii',derived,212)==(1,at)):
                raise ValueError('VP70 attachment is not inside the resident prefix')
            restored=bytearray(derived[:at]+derived[at+88:])
            restored[72:76]=raw[72:76];restored[188:192]=raw[188:192];restored[212:220]=raw[212:220]
            for i in range(textures):
                offset=table+80*i+76
                if struct.unpack_from('<i',derived,offset)[0]!=struct.unpack_from('<i',raw,offset)[0]+88:
                    raise ValueError('VP70 texture relocation differs')
                restored[offset:offset+4]=raw[offset:offset+4]
            if not (proof['success'] and proof['candidateOnly'] and proof['source']['sha256']==sha(source)==original_hash and
                    proof['artifact']['sha256']==sha(args.vp70_model) and Path(proof['artifact']['path']).resolve()==args.vp70_model.resolve() and
                    proof['profile']['sha256']==sha(profile) and proof['toolSha256']==sha(tool) and proof['studioHelperSha256']==sha(helper) and
                    len(derived)==len(raw)+88 and restored==raw):
                raise ValueError('VP70 authored attachment provenance/preserved payload differs')
            vp70_inputs=[args.vp70_attachment_proof.resolve(),source.resolve(),profile.resolve(),tool,helper]
        elif sha(args.vp70_model)!=original_hash:
            raise ValueError('Original VP70 P identity differs; derived models require their actual author receipt')
    output.mkdir(parents=True)
    helper=ROOT/"tests/common/cpp_function.py"
    spec=importlib.util.spec_from_file_location("cpp_function",helper)
    extractor=importlib.util.module_from_spec(spec);spec.loader.exec_module(extractor)
    plugin=ROOT/"native/csol/client/plugin.cpp"
    source = plugin.read_bytes()
    _,_,callback=extractor.function(source,b"static void StudioDrawPoints()")
    # The extended attachment observer is present in builds that include its interface.
    observes_attachments = int(b'#include "studio_attachments.hpp"' in source)
    expectation = f"constexpr unsigned ExpectedStudioObservers = {observes_attachments};\n"
    (output/"native-studio-points.inc").write_bytes(expectation.encode("ascii") + callback + b"\n")
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
    if args.p345_model:inputs.add(args.p345_model.resolve())
    if args.revolver_model:inputs.add(args.revolver_model.resolve())
    if args.vp70_model:inputs.add(args.vp70_model.resolve())
    if args.tmp_model:inputs.add(args.tmp_model.resolve())
    if args.mp5_model:inputs.add(args.mp5_model.resolve())
    if args.m16_model:inputs.add(args.m16_model.resolve())
    if args.g43_model:inputs.add(args.g43_model.resolve())
    if args.shotgun_model:inputs.add(args.shotgun_model.resolve())
    inputs.update(vp70_inputs)
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
        arguments=[str(executable),str(args.model.resolve())]
        if args.p345_model:arguments.append(str(args.p345_model.resolve()))
        if args.revolver_model:arguments.append(str(args.revolver_model.resolve()))
        if args.vp70_model:arguments.append(str(args.vp70_model.resolve()))
        if args.tmp_model:arguments.append(str(args.tmp_model.resolve()))
        if args.mp5_model:arguments.append(str(args.mp5_model.resolve()))
        if args.m16_model:arguments.append(str(args.m16_model.resolve()))
        if args.g43_model:arguments.append(str(args.g43_model.resolve()))
        if args.shotgun_model:arguments.append(str(args.shotgun_model.resolve()))
        tested = subprocess.run(arguments,cwd=output,capture_output=True,text=True,timeout=30)
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
