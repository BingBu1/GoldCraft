# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and test the explicit-mode encrypted wrapper without deployment."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import time

from service_mode_oracle import generate

ROOT = Path(__file__).resolve().parents[2]
TEST = 'tests/csol-scenario-positional-resource/'
SCENARIO = 'native/csol/scenario/'


def identity(path):
    path = Path(path).resolve(strict=True)
    return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def read(path):
    return json.loads(Path(path).read_text('utf-8-sig'))


def save(path,value):
    Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2),'utf8')


def encrypt(data):
    # Independent inverse of the observed original TEA block transform. These
    # generated malformed files exercise wrapper failure, not gameplay data.
    data += bytes((-len(data)) % 8)
    key = (0xb38520fd,0x5f30e75f,0xe2eb67e0,0xfc67a521)
    output = bytearray()
    for offset in range(0,len(data),8):
        left,right = struct.unpack_from('<II',data,offset)
        total = 0
        for _ in range(32):
            total = (total + 0x9e3779b9) & 0xffffffff
            left = (left + (((right << 4) + key[0]) ^ (right + total) ^ ((right >> 5) + key[1]))) & 0xffffffff
            right = (right + (((left << 4) + key[2]) ^ (left + total) ^ ((left >> 5) + key[3]))) & 0xffffffff
        output += struct.pack('<II',left,right)
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','rounds-result','evidence','hw','hw-metadata','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    args = parser.parse_args()
    source_root=args.source_root.resolve(strict=True)
    assert source_root.is_relative_to(ROOT)
    out = args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    for parent in (out,*out.parents):
        assert not parent.is_symlink() and not parent.is_junction()
    out.mkdir()
    for name in ('temp','objects','fixtures'):
        (out/name).mkdir()
    inputs,frozen = {},{}

    def record(path):
        item=identity(path);inputs[item['path']]=item;return item

    def freeze(relative):
        if relative in frozen:
            return
        source=source_root/relative;before=record(source)
        destination=out/'source'/relative
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_bytes(source.read_bytes())
        assert identity(source)==before and destination.stat().st_nlink==1
        frozen[relative]=dict(original=before,frozen=record(destination))
        if destination.suffix in ('.cpp','.hpp'):
            for token in re.findall(r'^\s*#\s*include\s+"([^"]+)"',destination.read_text('utf8'),re.M):
                child=(source.parent/token).resolve()
                if not child.exists():
                    child=source_root/SCENARIO/token
                if child.is_file():
                    freeze(child.relative_to(source_root).as_posix())

    names=('config','spawn_points','resources','map_resources','monster_types','trigger_rules','chaos_routes',
           'map_catalog','boss_skills','upgrade_tables','difficulty','difficulty_monsters','monster_attributes')
    core=[SCENARIO+n+'.cpp'for n in names]
    programs={'wrapper':TEST+'main.cpp','legacy-resources':'tests/csol-scenario/resources_tests.cpp',
              'legacy-map':'tests/csol-scenario-map-profile/main.cpp'}
    units=core+list(programs.values())
    for relative in [*units,TEST+'run.py',TEST+'service_mode_oracle.py']:
        freeze(relative)
    previous=read(args.rounds_result)
    record(args.rounds_result)
    assert previous['success'] and not previous['changedAfter']
    expected=read(args.rounds_result.parent/'oracle.json')
    record(args.rounds_result.parent/'oracle.json')
    datasets=[]
    for row in expected['datasets']:
        assert record(row['source']['path'])==row['source']
        assert record(row['plain']['path'])==row['plain']
        for key in ('source','plain'):
            source=Path(row[key]['path'])
            destination=out/'fixtures'/source.name
            destination.write_bytes(source.read_bytes());record(destination)
        datasets.append(row)
    prefix=b'a,4\r\nb,0\r\nc,500\r\nd,800\r\ne,400\r\nf,0\r\nroom,trigger id\r\n'
    good=prefix+b'opaque label,1/2-1\r\n\r\n'
    cases={'embedded-nul':prefix+b'name,1\0tail\r\n','late-scanner':good+b'name,1234567890\r\n',
           'missing-column':b'a\r\nb\r\nc\r\nd\r\ne\r\nf\r\ng\r\nh\r\n',
           'broken-quote':prefix+b'"unfinished','short-metadata':b'a,b\r\n',
           'padded':good+b'\0'*16}
    for name,data in cases.items():
        path=out/'fixtures'/(name+'.cso');path.write_bytes(encrypt(data));record(path)
    (out/'fixtures/padded.csv').write_bytes(good);record(out/'fixtures/padded.csv')
    mode=generate(args.hw,args.hw_metadata,args.evidence,out/'service-mode-oracle.json')
    for path in (args.hw,args.hw_metadata,args.evidence/'round-service-mode-getter-before.json',
                 out/'service-mode-oracle.json'):
        record(path)
    commands=read(args.baseline/'build-commands.json')
    record(args.baseline/'build-commands.json');record(args.baseline/'environment.json')
    compiler=next(x['command'][0]for x in commands if x['kind']=='compile')
    linker=next(token for job in commands if job['kind']=='link'for token in job['command']
                if Path(token).name.lower()=='lld-link.exe')
    record(compiler);record(linker)
    env=os.environ|read(args.baseline/'environment.json')
    env['TEMP']=env['TMP']=str(out/'temp')
    receipt=dict(success=False,source=frozen,steps=[],datasets=datasets,modeGetterCases=mode['cases'],
                 fullDLL=False,deployed=False,liveGameplay=False,maxWorkers=2)

    def run(name,kind,argv):
        rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(argv[1:]),'utf8')
        start=time.monotonic()
        done=subprocess.run(argv if kind=='test'else[argv[0],'@'+str(rsp)],cwd=ROOT,env=env,
                            capture_output=True,timeout=180)
        log=out/(name+'.log');log.write_bytes(done.stdout+done.stderr)
        text=log.read_text('utf-8-sig',errors='replace')
        includes=sorted(set(re.findall(r'Note: including file:\s*([^\r\n]+)',text)))
        for path in includes:record(path.strip())
        receipt['steps'].append(dict(name=name,kind=kind,command=argv,exit=done.returncode,
            seconds=time.monotonic()-start,log=record(log),includes=includes))
        save(out/'result.json',receipt);print(name,done.returncode,flush=True)
        if kind=='test' or done.returncode:print(text[-5000:],flush=True)
        assert done.returncode==0,name+' failed'

    try:
        objects={}
        for index,relative in enumerate(units):
            name=str(index)+'-'+Path(relative).stem;obj=out/'objects'/(name+'.obj')
            objects[relative]=str(obj)
            run(name,'compile',[compiler,'/c','/nologo','/W4','/WX','/EHsc','/MT','/utf-8','/std:c++20',
                '--target=i686-pc-windows-msvc','/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin',
                '/Gy','/Gw','/showIncludes','/I'+str(out/'source'/SCENARIO),'/Fo'+str(obj),str(out/'source'/relative)])
        for name,source in programs.items():
            run(name+'-link','link',[linker,'/subsystem:console','/machine:x86','/out:'+str(out/(name+'.exe')),
                '/threads:2','/opt:lldltojobs=2','/opt:ref',*[objects[p]for p in core],objects[source]])
            run(name,'test',[str(out/(name+'.exe'))]+([str(out/'fixtures')]if name=='wrapper'else[]))
            record(out/(name+'.exe'))
        receipt['success']=True
    finally:
        receipt['inputs']=list(inputs.values())
        receipt['changedAfter']=[p for p,r in inputs.items()if identity(p)!=r]
        receipt['success'] &= not receipt['changedAfter']
        save(out/'result.json',receipt)


if __name__=='__main__':main()
