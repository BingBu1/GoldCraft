"""Prepare the first-failure HUD continuation, reusing only five verified objects."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import re
import shutil

ROOT=Path(__file__).resolve().parents[2]


def read(path):return json.loads(Path(path).read_text('utf-8-sig'))
def key(path):return os.path.normcase(str(Path(path).resolve()))
def identity(path):
    path=Path(path).resolve()
    with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)
def save(path,value):Path(path).write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def verify(row):
    assert identity(row['path'])['sha256']==row['sha256'],row['path']
def safe(path):
    p=Path(path).absolute()
    for item in (p,*p.parents):
        if item.is_symlink() or item.is_junction():raise ValueError('Reparse path: '+str(item))
    return p.resolve()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous',type=Path,default=ROOT/'build/csol-hud-style-20261102/prepare-01')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();previous,out=map(safe,(args.previous,args.output))
    if out.exists() or not out.is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace build directory')
    plan,result=read(previous/'proposal.json'),read(previous/'result.json')
    assert result['failure']=='compile-weapons-full-tu unexpected exit 1' and result['inputsUnchanged']
    assert [j['name'] for j in result['results'] if j['exitCode']==0]==['compile-pure','compile-client','compile-contracts','compile-style','compile-codec']
    production,external=read(plan['productionManifest']['path']),read(plan['externalManifest']['path'])
    rows=plan['inputs']+[r['frozen'] for r in production['files']+production['controls']]+external['files']
    known={key(r['path']):r for r in rows}
    for row in known.values():verify(row)
    aliases={key(r['name']):r for r in read(plan['overlay']['path'])['roots']}
    source_pairs=read(previous/'source-changes.json')
    out.mkdir(parents=True);source=out/'source';output=out/'output'
    revised_pairs=[]
    for row in source_pairs:
        verify(row['frozen']);original=safe(row['original']['path']);target=source/row['relative']
        data=Path(row['frozen']['path']).read_bytes()
        if row['relative']=='native/csol/client/weapons.cpp':
            expected=data.replace(b'const auto path="sprites/"+definition->name+".txt";',b'auto path="sprites/"+definition->name+".txt";')
            expected=expected.replace(b'engine.pfnSPR_GetList(path.c_str(),&count)',b'engine.pfnSPR_GetList(path.data(),&count)')
            eol=b'\r\n' if b'\r\n' in data else b'\n'
            expected=expected.replace(b'auto* list=engine.pfnSPR_GetList("sprites/hud.txt",&count);',
                b'char hudPath[]="sprites/hud.txt";'+eol+b'    auto* list=engine.pfnSPR_GetList(hudPath,&count);')
            assert expected!=data and original.read_bytes()==expected,'Unexpected production difference'
            data=expected
        elif row['relative']=='tests/csol-hud-style/README.md':
            data=original.read_bytes()
        else:verify(row['original'])
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
        assert target.stat().st_nlink==1 and not os.path.samefile(target,original)
        revised_pairs.append(dict(relative=row['relative'],original=identity(original),frozen=identity(target),previousFrozen=row['frozen']))
    for row in revised_pairs:
        suffix='\\'+row['relative'].replace('/','\\').lower()
        for name in list(aliases):
            if name.lower().endswith(suffix):aliases[name]['external-contents']=row['frozen']['path']
        for name in (ROOT/row['relative'],source/row['relative']):
            aliases[key(name)]=dict(type='file',name=str(name),**{'external-contents':row['frozen']['path']})
    overlay=out/'source-overlay.json';save(overlay,dict(version=0,**{'case-sensitive':False,'use-external-names':False},roots=list(aliases.values())))
    save(out/'source-changes.json',revised_pairs)
    old_aliases={key(r['name']):r['external-contents'] for r in read(plan['overlay']['path'])['roots']}
    reuse=[]
    for name in ('pure','client','contracts','style','codec'):
        job=next(j for j in plan['jobs'] if j['name']=='compile-'+name)
        log=previous/('compile-'+name+'.log');text=log.read_text('utf-8');included={}
        for value in re.findall(r'^\s*Note: including file:\s*(.+?)\s*$',text,re.M):
            path=old_aliases.get(key(value),value);row=known[key(path)];verify(row);included[key(path)]=row
        expected=next(r['actualIncludes'] for r in result['results'] if r['name']==job['name'])
        assert len(included)==expected
        input_source=identity(job['command'][-1]);verify(known[key(input_source['path'])])
        reuse.append(dict(name=name,object=identity(previous/'output'/(name+'.obj')),source=input_source,
            originalCompileCommand=job['command'],compileLog=identity(log),actualIncludes=list(included.values()),
            actualIncludeCount=len(included),sourceAndIncludesUnchanged=True,originResult=identity(previous/'result.json')))
    save(out/'reused-objects.json',reuse)
    jobs=[]
    for old in plan['jobs']:
        if old['kind']=='compile' and old['name'] not in ('compile-weapons-full-tu','compile-beamgun-full-tu'):continue
        job=copy.deepcopy(old)
        job['command']=[arg.replace(str(previous),str(out)) for arg in job['command']]
        if job['kind']=='link':
            for record in reuse:
                candidate=str(output/(record['name']+'.obj'))
                job['command']=[record['object']['path'] if arg==candidate else arg for arg in job['command']]
        jobs.append(job)
    assert len(jobs)==8 and sum(j['kind']=='compile' for j in jobs)==2
    flags=[arg.replace(str(previous),str(out)) for arg in plan['compileFlags']]
    shutil.copy2(previous/'run.py',out/'run.py')
    assert identity(out/'run.py')['sha256']=='855d9126ee15573b45ac44a45606d1d1d792fa07e1856d6787fce2f67cfe8d1c'
    control=out/'prepare_retry.py';shutil.copy2(__file__,control)
    direct=list(plan['inputs'])+[r['frozen'] for r in revised_pairs]+[r['object'] for r in reuse]
    direct.extend(identity(p) for p in (overlay,out/'source-changes.json',out/'reused-objects.json',out/'run.py',control,
        previous/'proposal.json',previous/'result.json',previous/'actual-includes.json',previous/'execution-handoff.json'))
    direct.extend(r['compileLog'] for r in reuse)
    revised=copy.deepcopy(plan)
    revised.update(inputs=list({key(r['path']):r for r in direct}.values()),jobs=jobs,output=str(output),compileFlags=flags,
        overlay=identity(overlay),maxSeconds=600,reusedObjects=5,reusedObjectManifest=identity(out/'reused-objects.json'),
        previousFailure=identity(previous/'result.json'),previousProposal=identity(previous/'proposal.json'),
        expectedOnlyProductionChange='LoadNativeAmmo mutable string/path.data and LoadNativeHudSprites local mutable char array for matched char* SDK parameter')
    save(out/'proposal.json',revised)
    for row in revised_pairs:verify(row['original']);verify(row['frozen'])
    print(json.dumps(dict(proposal=identity(out/'proposal.json'),independentSources=len(revised_pairs),newCompiles=2,
        reusedObjects=5,links=3,runs=3,compilerInvoked=False),indent=2))


if __name__=='__main__':main()
