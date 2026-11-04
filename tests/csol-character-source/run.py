#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify the original generic source contract against actual deployed SDK objects."""
import argparse,hashlib,json,os,re,shutil,subprocess,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def record(p):
    p=Path(p).resolve();b=p.read_bytes()
    return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def key(p):return os.path.normcase(str(Path(p).resolve()))
def write(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','edges','output'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();base=a.baseline.resolve();out=a.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir(parents=True)
    selected={row['relative']:Path(row['frozen']['path']) for row in read(base/'effective-source-manifest.json')['files']}
    known={key(row['path']):row for row in read(base/'inputs.json')['files']}
    smoke='native/csol/server/scenario_player_runtime.cpp'
    original=record(selected[smoke]);data=selected[smoke].read_bytes()
    assert original['sha256']=='adc5abcdb465c53c012810d70cfa4723f57dc5176f37d5fc6234d95ae6c705e6'
    import importlib.util
    spec=importlib.util.spec_from_file_location('cpp_function',ROOT/'tests/common/cpp_function.py')
    extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
    signature=b'bool CCSOLScenarioPlayerRuntime::ReadEntitySource('
    before=extract.function(data,signature)[2]
    shared=(ROOT/smoke).read_bytes();after=extract.function(shared,signature)[2]
    assert b'dynamic_cast<const CGrenade*>' in after
    assert b'scenario source entity has no verified source adapter' in after
    assert after.count(b'error.message')==1
    data=data.replace(before,after)
    candidate=out/'source'/smoke;candidate.parent.mkdir(parents=True);candidate.write_bytes(data)
    assert candidate.stat().st_nlink==1;selected[smoke]=candidate
    # Freeze only new fixture headers. Native production files resolve to the
    # deployed baseline manifest, including its strict ReadEntitySource body.
    pending=[ROOT/'tests/csol-character-source/sdk.cpp'];captured=set()
    while pending:
        src=pending.pop().resolve();rel=src.relative_to(ROOT).as_posix()
        if rel in captured:continue
        captured.add(rel)
        if rel in selected:continue
        dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst);selected[rel]=dst
        for include in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"',src.read_bytes(),re.M):
            path=(src.parent/include.decode()).resolve()
            if path.is_file() and path.is_relative_to(ROOT):pending.append(path)
    for name in ('engine_edge_names.inc','engine_edge_defaults.inc'):
        rel='tests/csol-scenario-rules-session/'+name;dst=out/'source'/rel
        shutil.copy2(a.edges/name,dst);selected[rel]=dst
    overlay={key(row['name']):row for row in read(base/'source-overlay.json')['roots']}
    for rel,src in selected.items():
        for alias in (ROOT/rel,out/'source'/rel,src):overlay[key(alias)]=dict(type='file',name=str(alias),**{'external-contents':str(src)})
    for row in overlay.values():
        if row['name'].replace('\\','/').lower().endswith('/'+smoke):row['external-contents']=str(candidate)
    write(out/'overlay.json',dict(version=0,**{'case-sensitive':False,'use-external-names':True},roots=list(overlay.values())))
    for p in list(selected.values())+[out/'overlay.json',Path(__file__)]:known[key(p)]=record(p)
    commands=read(base/'build-commands.json');template=commands[0]['command']
    def compile(rel,name):
        cmd=list(template)
        for i,arg in enumerate(cmd):
            if arg.lower().endswith('.cpp'):cmd[i]=str(selected[rel])
            elif arg.startswith('/Fo'):cmd[i]='/Fo'+str(out/(name+'.obj'))
            elif arg.startswith('/Fd'):cmd[i]='/Fd'+str(out/(name+'.compile.pdb'))
            elif arg.startswith('/clang:') and arg.endswith('source-overlay.json'):cmd[i]='/clang:'+str(out/'overlay.json')
        return cmd
    units=read(base/'complete-object-provenance.json')['units']
    smokeObject=next(u['object'] for u in units if u['source']['path'].replace('\\','/').lower().endswith('/'+smoke))
    assert record(smokeObject['path'])==smokeObject
    jobs=[dict(name='fixture',command=compile('tests/csol-character-source/sdk.cpp','fixture')),
          dict(name='smoke',command=compile(smoke,'smoke'))]
    for variant in ('old','current'):
        cmd=[]
        for arg in commands[-1]['command']:
            if arg.lower()=='/dll' or arg.lower().startswith(('/out:','/pdb:','/implib:','/def:','/ltcgout:','/manifest','/natvis:')):continue
            if variant=='current' and key(arg)==key(smokeObject['path']):arg=str(out/'smoke.obj')
            elif Path(arg).is_absolute() and Path(arg).suffix.lower() in ('.obj','.lib','.res'):
                actual=record(arg)
                if key(arg) in known:assert actual==known[key(arg)]
                known[key(arg)]=actual
            cmd.append(arg)
        cmd+=['/OUT:'+str(out/(variant+'.exe')),'/PDB:'+str(out/(variant+'.pdb')),'/SUBSYSTEM:CONSOLE',str(out/'fixture.obj')]
        jobs.append(dict(name='link-'+variant,command=cmd))
    env=os.environ|read(base/'environment.json');env.update(TEMP=str(out),TMP=str(out),VSLANG='1033')
    for header in ('iostream','cstdarg'):
        path=next(Path(directory)/header for directory in env['INCLUDE'].split(';')
            if (Path(directory)/header).is_file())
        known[key(path)]=record(path)
    result=dict(success=False,baseline=str(base),candidate=record(candidate),original=original,sourceChanges=[smoke],
        sourceRuntime=record(selected['native/csol/server/scenario_player_runtime.cpp']),jobs=[],runs=[],
        boundary='Actual complete source-resolver TU and deployed map/smoke/grenade factories/damage callbacks linked from existing SDK objects. Engine and damage-owner boundaries controlled; no deployment or gameplay.')
    write(out/'plan.json',dict(jobs=jobs,inputs=list(known.values()),candidate=result['candidate'],original=original))
    def save():write(out/'result.json',result)
    try:
        for job in jobs:
            response=out/(job['name']+'.rsp')
            response.write_text(subprocess.list2cmdline(job['command'][1:])+'\n',encoding='utf8')
            invocation=[job['command'][0],'@'+str(response)]
            begin=time.monotonic();r=subprocess.run(invocation,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
            log=out/(job['name']+'.log');log.write_bytes(r.stdout)
            row=dict(name=job['name'],exit=r.returncode,seconds=time.monotonic()-begin,log=record(log),response=record(response))
            if not job['name'].startswith('link'):
                included=set(key(p.strip()) for p in re.findall(r'Note: including file:\s*(.+)',r.stdout.decode(errors='replace')))
                row['unknown']=[p for p in included if p not in known]
                row['changed']=[p for p in included if p in known and record(p)!=known[p]]
                row['actualIncludes']=len(included)
            result['jobs'].append(row);save()
            assert not r.returncode and not row.get('unknown') and not row.get('changed'),job['name']+' failed'
        for variant in ('old','current'):
            r=subprocess.run([str(out/(variant+'.exe'))],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
            log=out/(variant+'-run.log');log.write_bytes(r.stdout)
            text=r.stdout.decode(errors='replace');expected=1 if variant=='old' else 0
            observed=r.returncode==expected and ('FAIL smoke damage callback succeeds without disabling character skill' in text if variant=='old' else 'failures=0' in text)
            result['runs'].append(dict(variant=variant,exit=r.returncode,expectedExit=expected,expectedObserved=observed,log=record(log),output=text));save()
            assert observed,variant+' outcome differs'
        result['changedInputs']=[p for p,row in known.items() if record(p)!=row]
        result['success']=not result['changedInputs'];save()
    except BaseException as error:result['error']=str(error);save()
    print(json.dumps({k:v for k,v in result.items() if k not in ('jobs','runs')},ensure_ascii=False))
    return 0 if result['success'] else 1
if __name__=='__main__':raise SystemExit(main())
