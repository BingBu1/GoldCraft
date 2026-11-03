# SPDX-License-Identifier: GPL-3.0-or-later
"""Build one public-ABI x86 driver, then test a frozen filesystem DLL in child processes."""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import time
from prepare import identity, read


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',required=True,type=Path,help='Previously prepared actual SDK plan')
    parser.add_argument('--dll',required=True,type=Path)
    parser.add_argument('--legacy-dll',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();sdk=args.sdk.resolve();out=args.output.resolve();root=Path(__file__).resolve().parents[2]
    assert out.is_relative_to(root/'build') and not out.exists()
    out.mkdir();(out/'temp').mkdir()
    inherited=read(sdk/'plan.json');env=read(sdk/'environment.json');env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'))
    frozen=sdk/'source';source=out/'provider.cpp';source.write_bytes(Path(__file__).with_suffix('.cpp').read_bytes())
    assert source.stat().st_nlink==1
    compiler=inherited['tools'][0]['path'];linker=inherited['tools'][1]['path']
    registry={os.path.normcase(row['path']):row for row in inherited['system']}
    registry.update({os.path.normcase(row['frozen']['path']):row['frozen'] for row in inherited['inputs']})
    registry[os.path.normcase(str(source))]=identity(source)
    libraries=[]
    for name in ['libcmt.lib','libcpmt.lib','libvcruntime.lib','libucrt.lib','oldnames.lib','kernel32.lib']:
        found=next((Path(folder)/name for folder in env['LIB'].split(';') if (Path(folder)/name).is_file()),None)
        assert found,name
        libraries.append(identity(found))
    inputs=list(registry.values())+libraries+inherited['tools']+[identity(args.dll)]
    if args.legacy_dll:inputs.append(identity(args.legacy_dll))
    for row in inputs:assert identity(row['path'])==row,row['path']
    flags=['/nologo','/c','/TP','/std:c++20','/EHsc','/MT','/Z7','/utf-8','/Gy',
           '/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin','/clang:-ffp-contract=off',
           '--target=i686-pc-windows-msvc','/showIncludes','/D_CRT_SECURE_NO_WARNINGS','/DNOMINMAX','/DREHLDS_FIXES']
    flags+=['/I'+str(frozen/p) for p in ['native/include','external/ReHLDS/rehlds/public','external/ReHLDS/rehlds/public/rehlds']]
    commands=[dict(name='compile',seconds=180,argv=[compiler,*flags,str(source),'/Fo'+str(out/'provider.obj')]),
              dict(name='link',seconds=180,argv=[linker,'/nologo','/machine:x86','/debug','/opt:ref','/opt:icf','/nodefaultlib','/verbose',
                   '/out:'+str(out/'provider.exe'),'/pdb:'+str(out/'provider.pdb'),str(out/'provider.obj'),*[r['path'] for r in libraries]])]
    if args.legacy_dll:commands.append(dict(name='legacy',seconds=90,argv=[str(out/'provider.exe'),str(args.legacy_dll.resolve()),'legacy',str(out/'files-legacy')]))
    for mode in ['loose','pack','pack64','appended','appended64','invalid','invalid64']:
        commands.append(dict(name=mode,seconds=90,argv=[str(out/'provider.exe'),str(args.dll.resolve()),mode,str(out/('files-'+mode))]))
    plan=dict(commands=commands,inputs=inputs,source=identity(source),inherited=identity(sdk/'plan.json'),
              script=identity(Path(__file__)),boundary='Actual candidate DLL through public IFileSystem/optional factory. No engine deployment or gameplay.')
    (out/'plan.json').write_text(json.dumps(plan,indent=2),encoding='utf-8')
    result=dict(success=False,steps=[],actualIncludes=[],unknownIncludes=[],actualLibraries=[])
    try:
        for command in commands:
            start=time.monotonic();log=out/(command['name']+'.log')
            with log.open('wb') as stream:
                process=subprocess.run(command['argv'],cwd=out,env={**os.environ,**env},stdout=stream,stderr=subprocess.STDOUT,
                                       timeout=command['seconds'],creationflags=subprocess.CREATE_NO_WINDOW)
            row=dict(name=command['name'],exit=process.returncode,seconds=time.monotonic()-start,log=identity(log))
            result['steps'].append(row);print(json.dumps(row),flush=True)
            if command['name']=='compile':
                for name in sorted(set(re.findall(r'Note: including file:\s*(.+)',log.read_text(errors='replace')))):
                    value=identity(name.strip());key=os.path.normcase(value['path']);result['actualIncludes'].append(value)
                    if key not in registry:result['unknownIncludes'].append(value)
                    else:assert value==registry[key],name
                assert not result['unknownIncludes']
            if command['name']=='link':
                known={os.path.normcase(r['path']):r for r in libraries}
                for value in sorted(set(re.findall(r'^lld-link: Reading (.+)$',log.read_text(errors='replace'),re.M))):
                    path=Path(value.strip())
                    if not path.is_absolute() or path.suffix.lower()!='.lib':continue
                    row=identity(path);assert row==known[os.path.normcase(row['path'])],str(path)
                    result['actualLibraries'].append(row)
                assert len(result['actualLibraries'])==len(libraries)
            assert process.returncode==0,command['name']
        result.update(success=True,executable=identity(out/'provider.exe'))
    except BaseException as error:result['error']=repr(error)
    finally:
        for row in inputs:assert identity(row['path'])==row,row['path']
        result['inputsUnchanged']=True
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    if not result['success']:raise SystemExit(result['error'])


if __name__=='__main__':main()
