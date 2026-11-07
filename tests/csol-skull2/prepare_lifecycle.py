#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare real SDK lifecycle testing using a verified complete object baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil


def read(path): return json.loads(Path(path).read_text('utf-8-sig'))
def identity(path):
    path=Path(path).resolve()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def key(path): return os.path.normcase(str(Path(path).resolve()))
def write(path,value): path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--supplement',type=Path)
    parser.add_argument('--edges',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[2];base=args.sdk.resolve();out=args.output.resolve()
    out.mkdir();source=out/'source'
    manifest=read(base/'effective-source-manifest.json')['files']
    provenance=read(base/'complete-object-provenance.json')
    oldjobs=read(base/'build-commands.json')
    inputs={};selected={};overlay={}
    def keep(path):
        row=identity(path);inputs[key(path)]=row;return row
    for row in manifest:
        frozen=Path(row['frozen']['path'])
        assert identity(frozen)['sha256']==row['frozen']['sha256'],str(frozen)
        selected[row['relative']]=frozen
        keep(frozen)
    supplemental=[]
    for frozen_manifest in [args.candidate]+([args.supplement] if args.supplement else []):
        for row in read(frozen_manifest):
            relative=row['path'];original=frozen_manifest.resolve().parent/'source'/relative
            assert identity(original)['sha256']==row['sha256'],relative
            target=source/relative;target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(original,target);assert target.stat().st_nlink==1
            selected[relative]=target;keep(target)
            if frozen_manifest==args.supplement and relative.endswith('.cpp'):supplemental.append(relative)
    fixtures=['tests/csol-skull2/lifecycle_sdk.cpp','tests/csol-scenario-rules-session/sdk_engine.hpp',
        'tests/csol-scenario-rules-session/resources.hpp','tests/csol-scenario-map-session/fixtures.hpp']
    for relative in fixtures:
        target=source/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(root/relative,target);selected[relative]=target;keep(target)
    for name in ['engine_edge_names.inc','engine_edge_defaults.inc']:
        relative='tests/csol-scenario-rules-session/'+name
        target=source/relative;shutil.copy2(args.edges/name,target);selected[relative]=target;keep(target)
    def mapping(virtual,real): overlay[key(virtual)]=dict(type='file',name=str(virtual),**{'external-contents':str(real)})
    for relative,frozen in selected.items():
        mapping(root/relative,frozen);mapping(source/relative,frozen)
    for row in manifest:
        mapping(Path(row['frozen']['path']),selected[row['relative']])
    # Retain original overlay aliases used by historical include directories.
    frozen_aliases={key(x['frozen']['path']):selected[x['relative']] for x in manifest}
    for row in read(base/'source-overlay.json')['roots']:
        real=Path(row['external-contents'])
        replacement=frozen_aliases.get(key(real),real)
        mapping(Path(row['name']),replacement)
    write(out/'overlay.json',{'version':0,'case-sensitive':False,'use-external-names':True,'roots':list(overlay.values())})
    keep(out/'overlay.json')
    # Capture toolchain/SDK identities; original changed source is not a reused
    # source claim: all actual project reads are routed to frozen files above.
    for row in read(base/'inputs.json')['files']:
        path=Path(row['path'])
        if path.is_file() and key(path) not in inputs: keep(path)
    replaced=['native/csol/server/weapons/wpn_skull2.cpp','native/csol/server/weapons/weapon_registry.cpp',
        'native/csol/server/weapons/wpn_tknifeex2.cpp','external/ReGameDLL_CS/regamedll/dlls/weapons.cpp',
        'external/ReGameDLL_CS/regamedll/dlls/player.cpp','external/ReGameDLL_CS/regamedll/dlls/client.cpp']+supplemental
    compiler=oldjobs[0]['command'][0]
    command=[compiler,'--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8',
        '/c','/showIncludes','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/Zc:strictStrings-',
        '/W3','/Gy','/Gw']
    command+=['/D'+x for x in ['REGAMEDLL_ADD','REGAMEDLL_API','REGAMEDLL_FIXES','REGAMEDLL_SSE',
        'REGAMEDLL_SELF','REGAMEDLL_CHECKS','UNICODE_FIXES','BUILD_LATEST','BUILD_LATEST_FIXES',
        'CLIENT_WEAPONS','USE_QSTRING','_CRT_SECURE_NO_WARNINGS','NDEBUG','NOMINMAX','_WINDLL','_MBCS']]
    command+=['/I'+str(root/'external/ReGameDLL_CS/regamedll'/x) for x in ['', 'hookers','common','dlls',
        'engine','public','public/regamedll','pm_shared','regamedll','testsuite']]
    command+=['/I'+str(root/x) for x in ['native/include','native/csol/server',
        'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include','external/ReGameDLL_CS/dep/cppunitlite/include']]
    command+=['/clang:-ivfsoverlay','/clang:'+str(out/'overlay.json')]
    jobs=[];replace_objects={};reused=[]
    for relative in replaced+fixtures[:1]:
        object=out/(Path(relative).stem+'.obj')
        jobs.append(dict(name=Path(relative).stem,command=command+[str(source/relative),'/Fo'+str(object)]))
        matching=[x for x in provenance['units'] if x['source']['path'].replace('\\','/').lower().endswith('/'+relative.lower())]
        assert len(matching)==(0 if relative in supplemental or 'wpn_skull2.cpp' in relative or relative.startswith('tests/') else 1),relative
        if matching: replace_objects[key(matching[0]['object']['path'])]=str(object)
    link=[]
    remove=('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:')
    for arg in oldjobs[-1]['command']:
        if arg.lower()=='/dll' or arg.lower().startswith(remove):continue
        if arg.lower().endswith(('.obj','.lib')) and Path(arg).is_absolute():
            if key(arg) in replace_objects: arg=replace_objects[key(arg)]
            else:
                keep(arg);reused.append(arg)
        link.append(arg)
    for unit in provenance['units']:
        if key(unit['object']['path']) not in replace_objects:
            assert identity(unit['object']['path'])['sha256']==unit['object']['sha256'],'reused object changed'
    link += ['/OUT:'+str(out/'lifecycle.exe'),'/PDB:'+str(out/'lifecycle.pdb'),'/SUBSYSTEM:CONSOLE',
        str(out/'wpn_skull2.obj'),str(out/'lifecycle_sdk.obj')]+[str(out/(Path(x).stem+'.obj')) for x in supplemental]
    jobs.append(dict(name='link',command=link))
    for path in [args.candidate,Path(__file__),base/'complete-object-provenance.json',base/'build-commands.json',Path(compiler),Path(link[0])]:keep(path)
    write(out/'prepared.json',dict(jobs=jobs,inputs=list(inputs.values()),environment=read(base/'environment.json'),
        baseline=str(base),candidate=str(args.candidate.resolve()),reusedObjects=reused,
        layoutBoundary='CSOLWeaponLifecycle is a separate interface; no existing ReGameDLL base-class members/vtable slots were changed. New SKULL2 owns its own private state. Existing objects use the complete verified baseline.',
        boundary='Complete real weapon/base/player/pack functions with controlled engine APIs. Player SetAnimation is an observation seam. No live server, real delta encoder, full DLL publication or gameplay.'))
    print(json.dumps(dict(output=str(out),jobs=len(jobs),inputs=len(inputs),reusedObjects=len(reused))))


if __name__=='__main__':main()
