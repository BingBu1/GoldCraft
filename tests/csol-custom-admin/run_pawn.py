import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import zlib

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--plugin',type=Path,required=True)
parser.add_argument('--environment',type=Path,required=True)
parser.add_argument('--compiler',type=Path,required=True)
parser.add_argument('--group',choices=('root','all','layout','suite'),default='root')
parser.add_argument('--catalog',type=Path,default=root/'native/csol/config/gameplay.example.json')
args=parser.parse_args()
out=args.output.resolve()
assert out.is_relative_to(root/'build') and not out.exists()
out.mkdir(parents=True)
env=dict(os.environ)
environment=args.environment.resolve(strict=True)
config=json.loads(environment.read_text('utf-8-sig'))
for key,value in config.items():
    for prior in list(env):
        if prior.casefold()==key.casefold():del env[prior]
    env[key]=value
compiler=args.compiler.resolve(strict=True)
linker=compiler.with_name('lld-link.exe')
plugin=args.plugin.resolve()
assert plugin.is_relative_to(root/'build')
paths=[root/'amxx/scenario/goldcraft_scenario_admin.sma',root/'amxx/scenario/include/csol_scenario_admin.inc',
       root/'tests/csol-custom-admin/pawn.cpp',root/'tests/csol-scenario-admin/pawn.cpp',compiler,linker,Path(__file__),plugin,environment]
if args.group in ('layout','suite'):paths.append(args.catalog.resolve(strict=True))
include=root/'external/amxmodx/compiler/amxxpc'
paths+=list(include.glob('*.h'))+[include/'amx.cpp']
for dirname in config['INCLUDE'].split(';'):
    path=Path(dirname)
    if path.is_dir():paths += [p for p in path.rglob('*') if p.is_file() and (not p.suffix or p.suffix.lower() in ('.h','.hpp','.inc','.inl'))]
resource=Path(subprocess.check_output([str(compiler),'-print-resource-dir'],text=True).strip())
paths += [p for p in resource.rglob('*') if p.is_file() and p.suffix.lower() in ('.h','.inc')]
libraries=[Path(d)/n for n in ('libcmt.lib','libcpmt.lib','libvcruntime.lib','libucrt.lib','oldnames.lib','kernel32.lib') for d in config['LIB'].split(';') if (Path(d)/n).is_file()]
paths+=libraries
def identity(path):return dict(path=str(path.resolve()),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
inputs={str(path.resolve()).casefold():identity(path) for path in paths}
receipt=dict(success=False,sourceCopies=0,fullDLL=False,deployed=False,liveGameplay=False,inputsBefore=list(inputs.values()),steps=[])
def save():(out/'receipt.json').write_text(json.dumps(receipt,indent=2,ensure_ascii=False),'utf-8')
def run(name,args):
    done=subprocess.run([str(x) for x in args],cwd=root,env=env,capture_output=True,timeout=180)
    (out/(name+'.log')).write_bytes(done.stdout+done.stderr)
    receipt['steps'].append(dict(name=name,exit=done.returncode,command=[str(x) for x in args]));save()
    print(name,done.returncode,(done.stdout+done.stderr).decode('utf-8',errors='replace')[-4000:])
    if done.returncode:raise RuntimeError(name+' failed')
try:
    data=plugin.read_bytes();magic,version,entries=struct.unpack_from('<IHB',data)
    assert magic==0x414d5858 and version==0x0300 and entries==1
    cell,disk,size,memory,offset=struct.unpack_from('<BIIII',data,7)
    assert cell==4 and offset==24 and len(data)==offset+disk
    image=zlib.decompress(data[offset:])
    assert len(image)==size and len(image)>=56
    # AMXX images may append debug data beyond runtime memory. Validate the
    # actual AMX stack boundary rather than comparing memory with debug bytes.
    assert struct.unpack_from('<I',image)[0]<=len(image) and struct.unpack_from('<I',image,24)[0]<=memory
    (out/'admin.amx').write_bytes(image)
    objects=[]
    for name,source in [('fixture',root/'tests/csol-custom-admin/pawn.cpp'),('amx',include/'amx.cpp')]:
        obj=out/(name+'.obj');objects.append(obj)
        run('compile-'+name,[compiler,'/nologo','/c','/std:c++20','/utf-8','/EHsc','/MT','/O2','/fp:precise','/arch:AVX2','/clang:-O3','/clang:-flto=thin','--target=i686-pc-windows-msvc',
            '/DHAVE_STDINT_H','/DAMX_NODYNALOAD','/DNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/I'+str(include),'/Fo'+str(obj),source])
    executable=out/'pawn.exe'
    run('link',[linker,'/subsystem:console','/machine:x86','/threads:1','/opt:lldltojobs=1','/out:'+str(executable),*objects,*libraries])
    if args.group in ('layout','suite'):
        catalog=json.loads(args.catalog.read_text('utf8'))
        keys=[*catalog['features'],*[key for group in catalog['features'].values() for key in group['values']]]
        assert len(keys)==48 and len(set(keys))==48 and all(re.fullmatch('[a-z0-9_]+',key) for key in keys)
        catalogFile=out/'catalog-keys.txt';catalogFile.write_text('\n'.join(keys)+'\n','utf8')
    for group in ('root','all','layout') if args.group=='suite' else (args.group,):
        command=[executable,out/'admin.amx',group]
        if group=='layout':command.append(catalogFile)
        run('tests-'+group,command)
    receipt['success']=True
finally:
    receipt['changedInputs']=[row['path'] for row in inputs.values() if identity(Path(row['path']))!=row]
    receipt['inputsUnchanged']=not receipt['changedInputs'];receipt['success'] &= receipt['inputsUnchanged'];save()
