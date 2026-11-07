#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze only FX translation units, fixtures and header dependencies; do not compile."""
from pathlib import Path
import argparse, hashlib, json, os, shutil

ROOT = Path(__file__).resolve().parents[2]

def identity(path):
    value = path.read_bytes()
    return {'path':str(path.resolve()),'bytes':len(value),'sha256':hashlib.sha256(value).hexdigest()}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--clang',type=Path,required=True)
    args=parser.parse_args()
    candidate=args.candidate.resolve();out=args.output.resolve()
    if not candidate.is_relative_to(ROOT/'build') or not out.is_relative_to(ROOT/'build'):
        parser.error('candidate and output must remain in workspace build')
    for value in (candidate,out):
        for node in (value,*value.parents):
            if node.is_symlink() or node.is_junction():raise RuntimeError('linked path refused: '+str(node))
    out.mkdir(parents=True,exist_ok=False); frozen=out/'source';frozen.mkdir()
    candidate_manifest=json.loads((candidate.parent/'manifest.json').read_text(encoding='utf-8-sig'))
    known={row['path']:row for row in candidate_manifest['files']}
    chosen={}; origins={}
    suffixes={'.h','.hpp','.inl','.inc','.def'}
    for name in ('native','external/ReGameDLL_CS'):
        for path in (candidate/name).rglob('*'):
            if path.is_file() and path.suffix.lower() in suffixes:
                rel=path.relative_to(candidate).as_posix();chosen[rel]=path
    for name in ('native/csol/client/scenario/core/scenario_effects.cpp','native/csol/server/scenario/core/scenario_messages.cpp'):
        chosen[name]=candidate/name
    for name in ('client.cpp','messages.cpp'):
        chosen['tests/csol-scenario-monster-skills/'+name]=ROOT/'tests/csol-scenario-monster-skills'/name
    meta=ROOT/'external/MetaHookSv-20261007'
    meta_directories=['MetaHook/include','MetaHook/thirdparty/capstone_fork/include',
        'MetaHook/thirdparty/rapidjson/include','thirdparty/glew_fork/include',
        'Plugins/Renderer/include/Interface','Plugins/VGUI2Extension/include/Interface']
    for directory in meta_directories:
        for path in (meta/directory).rglob('*'):
            if path.is_file() and path.suffix.lower() in suffixes:
                chosen[path.relative_to(ROOT).as_posix()]=path
    manifest=[]
    for name,path in sorted(chosen.items()):
        before=identity(path)
        if path.is_relative_to(candidate):
            record=known[name]
            if record['sha256']!=before['sha256'] or record['bytes']!=before['bytes']:
                raise RuntimeError('candidate drift '+name)
        target=frozen/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
        after=identity(target)
        if before['sha256']!=after['sha256'] or target.stat().st_nlink!=1 or target.stat().st_ino==path.stat().st_ino:
            raise RuntimeError('copy is not independent '+name)
        manifest.append({'relative':name,'origin':before,'frozen':after})
    # Capture toolchain headers before compilation; these external installed
    # SDKs are read-only inputs, never copied or modified by this task.
    system_dirs=[Path(x) for x in os.environ.get('INCLUDE','').split(';') if x]
    system_dirs+=list((args.clang.parent.parent/'lib/clang').glob('*/include'))
    system={}
    for directory in system_dirs:
        for path in directory.rglob('*'):
            if path.is_file() and (path.suffix.lower() in suffixes or not path.suffix):
                system[str(path.resolve()).lower()]=identity(path)
    for library in ('libcmt.lib','libvcruntime.lib','libucrt.lib','oldnames.lib','kernel32.lib'):
        for directory in os.environ.get('LIB','').split(';'):
            path=Path(directory)/library
            if path.is_file():system[str(path.resolve()).lower()]=identity(path);break
    for tool in (args.clang,args.clang.parent/'lld-link.exe'):
        system[str(tool.resolve()).lower()]=identity(tool)
    includes_client=[frozen/'native',frozen/'native/csol/client',frozen/'native/csol']
    sdk=frozen/'external/MetaHookSv-20261007/MetaHook/include'
    includes_client += [sdk/path for path in ('','Interface','HLSDK/cl_dll','HLSDK/common',
        'HLSDK/engine','HLSDK/pm_shared','HLSDK/public','SourceSDK','SourceSDK/tier0',
        'SourceSDK/tier1','VGUI','Interface/VGUI')]
    includes_client += [frozen/'external/MetaHookSv-20261007'/path for path in meta_directories[1:]]
    sdk_server=frozen/'external/ReGameDLL_CS/regamedll'
    includes_server=[sdk_server/path for path in ('','hookers','common','dlls','engine','public',
        'public/regamedll','pm_shared','regamedll','testsuite')]
    includes_server += [frozen/'native/include',frozen/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include']
    common=['--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8',
        '/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/Gy','/Gw','/W3',
        '/DNDEBUG','/DNOMINMAX','/D_CRT_SECURE_NO_WARNINGS']
    client_flags=common+['/DNO_MALLOC_OVERRIDE','/DGLEW_STATIC','/DGLEW_NO_GLU']
    server_flags=common+['/Zc:strictStrings-']+['/D'+x for x in ('REGAMEDLL_ADD','REGAMEDLL_API',
        'REGAMEDLL_FIXES','REGAMEDLL_SSE','REGAMEDLL_SELF','REGAMEDLL_CHECKS','UNICODE_FIXES',
        'BUILD_LATEST','BUILD_LATEST_FIXES','CLIENT_WEAPONS','USE_QSTRING')]
    commands=[]
    for group,production,fixture,flags,includes in [
        ('client','native/csol/client/scenario/core/scenario_effects.cpp','tests/csol-scenario-monster-skills/client.cpp',client_flags,includes_client),
        ('messages','native/csol/server/scenario/core/scenario_messages.cpp','tests/csol-scenario-monster-skills/messages.cpp',server_flags,includes_server)]:
        objects=[]
        for label,name in [('production',production),('fixture',fixture)]:
            obj=out/(group+'-'+label+'.obj');objects.append(obj)
            commands.append({'name':group+'-'+label+'-compile','kind':'compile','command':[
                str(args.clang),*flags,*['/I'+str(x) for x in includes],'/showIncludes','/c',str(frozen/name),'/Fo'+str(obj)]})
        exe=out/(group+'.exe')
        commands.append({'name':group+'-link','kind':'link','command':[str(args.clang),'--target=i686-pc-windows-msvc',
            '/MT','/clang:-fuse-ld=lld',*[str(x) for x in objects],'/Fe'+str(exe),'/link','/OPT:REF','/OPT:ICF',
            '/OPT:LLDLTO=3','/threads:1']})
        commands.append({'name':group+'-run','kind':'test','command':[str(exe)]})
    (out/'inputs.json').write_text(json.dumps({'candidate':str(candidate),'files':manifest,'system':list(system.values()),
        'candidateManifest':identity(candidate.parent/'manifest.json'),'compiled':False,'executed':False},indent=2),encoding='utf-8')
    (out/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
    print(json.dumps({'frozenFiles':len(manifest),'systemInputs':len(system),'commands':len(commands),'output':str(out)}))

if __name__=='__main__':main()
