# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze a verified SDK baseline, compile real collector/loader, compare old code."""
import argparse, hashlib, json, os, re, shutil, subprocess, time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def key(path):return os.path.normcase(str(Path(path).resolve()))
def identity(path):
    path=Path(path).resolve();return dict(path=str(path),bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf8')
def verify(row):assert identity(row['path'])==row,row['path']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','before','common','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();baseline=args.baseline.resolve();out=args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    source=out/'source';output=out/'output';output.mkdir()
    previous=read(baseline/'output/result.json');assert previous['success'] and previous['inputsUnchanged']
    plan=read(baseline/'plan.json');inputs={};copies={};linked=[]
    def keep(path):row=identity(path);inputs[key(path)]=row;return row
    # Only verified immutable baseline snapshots are linked. Replaced candidates
    # use a new independent file and os.replace, never writes through a link.
    for item in plan['inputs']:
        verify(item['frozen']);src=Path(item['frozen']['path']);dest=source/item['relative'];dest.parent.mkdir(parents=True,exist_ok=True)
        os.link(src,dest);assert src.stat().st_ino==dest.stat().st_ino
        copies[item['relative']]=dict(original=item['frozen'],frozen=identity(dest));linked.append(item['relative'])
    def copy(relative,src):
        src=Path(src);dest=source/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        temporary=dest.with_name(dest.name+'.candidate');assert not temporary.exists()
        shutil.copy2(src,temporary);assert temporary.stat().st_nlink==1
        os.replace(temporary,dest);assert dest.stat().st_nlink==1 and dest.stat().st_ino!=src.stat().st_ino
        copies[relative]=dict(original=identity(src),frozen=identity(dest));return dest
    visited=set()
    def closure(relative):
        if relative in visited:return
        visited.add(relative);dest=copy(relative,ROOT/relative)
        for token in re.findall(r'^\s*#\s*include\s+"([^"\r\n]+)"',dest.read_text('utf8'),re.M):
            child=Path(os.path.normpath(str(Path(relative).parent/token))).as_posix()
            if child.startswith(('native/','tests/')) and (ROOT/child).is_file():closure(child)
            elif (ROOT/'native/include'/token).is_file():closure('native/include/'+token)
    names=('dependencies','model_codec','sprite_codec','texture_model','model_json','events','model_source','sequences')
    for name in names:closure('native/csol/'+name+'.cpp')
    closure('native/engine/csol_loader.cpp')
    closure('tests/csol-dependency-cache/loader.cpp')
    fixture='tests/csol-studio-sound-resources/main.cpp';copy(fixture,ROOT/fixture)
    copy('tests/csol-studio-sound-resources/run.py',Path(__file__))
    old=copy('native/csol/dependencies-before.cpp',args.before)
    for item in copies.values():keep(item['frozen']['path'])
    for row in plan['system']+plan['tools']+plan['libraries']+[plan['environment']]:verify(row);inputs[key(row['path'])]=row
    for path in (Path(__file__),args.before,baseline/'plan.json',baseline/'output/result.json'):keep(path)
    env=os.environ.copy();env.update(read(plan['environment']['path']));env.update(TEMP=str(output),TMP=str(output),VSLANG='1033')
    template=next(c['argv']for c in plan['commands']if c['name']=='loader')
    flags=[a.replace(str(baseline/'source'),str(source))for a in template[1:]if not a.endswith('.cpp')and not a.startswith('/Fo')]
    commands=[];objects=[]
    for name,relative in [(n,'native/csol/'+n+'.cpp')for n in names]+[('fixture',fixture),('dependencies-before','native/csol/dependencies-before.cpp')]:
        obj=output/(name+'.obj');objects.append(obj)
        commands.append(dict(name=name,kind='compile',argv=[template[0],*flags,str(source/relative),'/Fo'+str(obj)]))
    for variant in ('current','old'):
        chosen=[p for p in objects if p.stem not in ('dependencies','dependencies-before')]
        chosen.append(output/('dependencies.obj'if variant=='current'else'dependencies-before.obj'))
        exe=output/(variant+'.exe')
        commands.append(dict(name='link-'+variant,kind='link',argv=[plan['tools'][1]['path'],'/nologo','/machine:x86','/debug','/opt:ref','/opt:icf','/opt:lldltojobs=2','/out:'+str(exe),'/pdb:'+str(output/(variant+'.pdb')),*map(str,chosen),str(ROOT/'external/ReHLDS/rehlds/lib/libacof32.lib')]))
        commands.append(dict(name='run-'+variant,kind='test',expected=0 if variant=='current'else 1,argv=[str(exe),str(args.common.resolve())]))
    save(out/'plan.json',dict(inputs=list(inputs.values()),copies=copies,commands=commands,
        immutableBaselineLinks=linked,changedProduction=['native/csol/dependencies.cpp','native/engine/csol_loader.cpp'],
        boundary='Actual collector and current loader with frozen ReHLDS SDK; controlled FS/advertisement. No Session, DLL, HTTP or gameplay.'))
    result=dict(success=False,steps=[],objects=[],unknown=[],resources=[])
    try:
        for job in commands:
            start=time.monotonic();run=subprocess.run(job['argv'],cwd=output,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240,creationflags=subprocess.CREATE_NO_WINDOW)
            log=output/(job['name']+'.log');log.write_bytes(run.stdout);text=run.stdout.decode('utf8',errors='replace');headers=[]
            for path in set(re.findall(r'Note: including file:\s*(.+)',text)):
                row=identity(path.strip());headers.append(row)
                if key(path.strip())not in inputs:result['unknown'].append(row)
                else:assert row==inputs[key(path.strip())]
            result['steps'].append(dict(name=job['name'],exit=run.returncode,seconds=time.monotonic()-start,log=identity(log),includes=headers))
            print(job['name'],run.returncode,flush=True)
            assert not result['unknown'],result['unknown']
            assert run.returncode==job.get('expected',0),job['name']
            if job['kind']=='compile':result['objects'].append(identity(output/(job['name']+'.obj')))
            if job['name']=='run-old':assert 'Contains(Foot1)'in text
            if job['name']=='run-current':
                for path,size,sha in re.findall(r'^resource (.+) (\d+) ([a-f0-9]{64})[\r\t ]*$',text,re.M):
                    row=identity(args.common/path);assert row['bytes']==int(size)and row['sha256']==sha
                    result['resources'].append(row)
                assert result['resources'],'missing real resource identity output'
                match=re.search(r'studio-sound-resources checks=(\d+) failures=(\d+)',text);assert match
                result['checks'],result['failures']=map(int,match.groups());assert result['failures']==0
        result['success']=True
    except BaseException as error:result['error']=repr(error)
    result['inputDrift']=[r['path']for r in inputs.values()if identity(r['path'])!=r]
    result['resourceDrift']=[r['path']for r in result['resources']if identity(r['path'])!=r]
    result['inputsUnchanged']=not result['inputDrift']and not result['resourceDrift'];result['success']&=result['inputsUnchanged']
    save(out/'result.json',result);print(json.dumps({k:result.get(k)for k in ['success','error','checks','failures','inputsUnchanged']}))
    raise SystemExit(0 if result['success']else 1)
if __name__=='__main__':main()
