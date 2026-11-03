#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze actual SDK module-registry tests; reuse explicitly identified SDK objects."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

def read(p): return json.loads(Path(p).read_text('utf-8-sig'))
def identity(p):
    p=Path(p).resolve()
    with p.open('rb') as stream: sha=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(p),bytes=p.stat().st_size,sha256=sha)
def key(p): return os.path.normcase(str(Path(p).resolve()))
def write(p,obj): p.write_text(json.dumps(obj,ensure_ascii=False,indent=2),encoding='utf-8')
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sdk',type=Path,required=True);ap.add_argument('--after',type=Path,required=True)
    ap.add_argument('--parent-after',type=Path,required=True);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--native',type=Path,required=True)
    ap.add_argument('--reuse-build',type=Path)
    args=ap.parse_args();root=Path(__file__).resolve().parents[2];out=args.output.resolve();out.mkdir()
    source=out/'source';output=out/'output';output.mkdir();(output/'temp').mkdir()
    previous=read(args.sdk/'proposal.json');previous_sources=read(args.sdk/'source-manifest.json')
    assert read(args.sdk/'result.json')['success']
    parent=read(args.parent_after)
    if (args.after/'manifest.json').exists():frozen=read(args.after/'manifest.json')['files']
    else:
        initial=read(args.after.parent/'source-01/before.json')
        old={r['path']:r for r in initial}
        frozen=[dict(relative=r['path'],frozen=identity(args.after/'after'/r['path']),before=identity(args.after.parent/'source-01/before'/r['path']) if r['path'] in old else None) for r in parent]
    assert {r['relative']:r['frozen']['sha256'] for r in frozen}=={r['path']:r['sha256'] for r in parent}
    for row in previous_sources['files']:
        assert identity(row['frozen']['path'])==row['frozen']
        dst=source/row['relative'];dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(row['frozen']['path'],dst)
    delta=[]
    for row in frozen:
        assert identity(row['frozen']['path'])==row['frozen']
        dst=source/row['relative'];dst.parent.mkdir(parents=True,exist_ok=True)
        if row.get('before') and dst.exists():assert identity(dst)['sha256']==row['before']['sha256']
        shutil.copy2(row['frozen']['path'],dst);assert dst.stat().st_nlink==1
        delta.append(dict(relative=row['relative'],after=identity(dst),before=row.get('before')))
    for name in ('registry.cpp','client.cpp'):
        dst=source/'tests/csol-weapon-modules'/name;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(root/'tests/csol-weapon-modules'/name,dst)
    # Copy the exact passing SDK02 fixture and keep its stock ID0 propagation fix.
    assert '*to=*from;' in (source/'tests/csol-skull-balrog/skull5_integration_client.cpp').read_text()
    template=previous['commands'][0]['argv'];base=template[:template.index('--')]
    base=[a for a in base if not a.startswith(('/Fo','/Fd'))]
    commands=[]
    def compile(name,relative,extra=()):
        obj=output/(name+'.obj');argv=[*base,*extra,'/Fo'+str(obj),'/Fd'+str(output/(name+'.pdb')),'--',str(source/relative)]
        commands.append(dict(name=name,kind='compile',argv=argv));return str(obj)
    compiled={}
    for row in delta:
        rel=row['relative']
        if rel.endswith('.cpp'):
            stem=Path(rel).stem
            if args.reuse_build:
                old=args.reuse_build/'output'/('after-'+stem+'.obj');assert old.exists();compiled[stem]=str(old.resolve())
            else:compiled[stem]=compile('after-'+stem,rel,('/clang:-finstrument-functions',))
    if args.reuse_build:
        registry_fixture=str((args.reuse_build/'output/registry-fixture.obj').resolve());assert Path(registry_fixture).exists()
    else:registry_fixture=compile('registry-fixture','tests/csol-weapon-modules/registry.cpp')
    before_fixture=compile('before-fixture','tests/csol-weapon-modules/client.cpp',('/DREGISTRY_AFTER=0',))
    after_fixture=compile('after-fixture','tests/csol-weapon-modules/client.cpp',('/DREGISTRY_AFTER=1',))
    old_link=next(c['argv'] for c in previous['commands'] if c['name']=='client-link')
    old_objects=[a for a in old_link if a.lower().endswith('.obj') and 'skull5_integration_client_' not in a]
    assert len(old_objects)==19
    after_objects=[];reused=[]
    for obj in old_objects:
        stem=Path(obj).stem.rsplit('_',1)[0]
        after_objects.append(compiled.get(stem,obj));reused.append(identity(obj))
    after_objects.append(compiled['weapon_module'])
    def link(name,objects):
        exe=output/(name+'.exe');argv=[old_link[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objects,'/Fe'+str(exe),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1','/opt:lldltojobs=1']
        commands.append(dict(name=name+'-link',kind='link',argv=argv));return str(exe)
    registry=link('registry',[compiled['weapon_module'],registry_fixture])
    before=link('before',old_objects+[before_fixture]);after=link('after',after_objects+[after_fixture])
    for mode in ('valid','empty','negative','zero','suit','stock','shield','limit','max','duplicate','same-id','same-order'):
        commands.append(dict(name='registry-'+mode,kind='test',argv=[registry,mode]))
    for name,exe in [('before',before),('after',after)]:commands.append(dict(name=name+'-test',kind='test',argv=[exe,str(args.native.resolve()),str(output/(name+'-observations.txt'))]))
    known={key(r['path']):r for r in previous['inputs']}
    for p in source.rglob('*'):
        if p.is_file():known[key(p)]=identity(p)
    for row in reused:known[key(row['path'])]=row
    if args.reuse_build:
        for obj in [*compiled.values(),registry_fixture]:known[key(obj)]=identity(obj)
    for p in [args.native,Path(__file__),root/'tests/csol-weapon-modules/run.py',args.parent_after,args.sdk/'proposal.json',args.sdk/'result.json',args.sdk/'source-manifest.json']:
        known[key(p)]=identity(p)
    env=previous['environment'].copy();env.update(TEMP=str(output/'temp'),TMP=str(output/'temp'))
    write(out/'delta.json',delta);write(out/'reused.json',reused)
    write(out/'plan.json',dict(jobs=1,commands=commands,inputs=list(known.values()),environment=env,output=str(output),source=str(source),maxSeconds=900,
        boundary='Actual 8 module/controller TUs, registry and controlled SDK interfaces. No full DLL, GPU, deployment, UI or multiplayer acceptance.'))
    print(json.dumps(dict(plan=identity(out/'plan.json'),delta=identity(out/'delta.json'),inputs=len(known),commands=len(commands))))
if __name__=='__main__':main()
