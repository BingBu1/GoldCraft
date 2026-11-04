#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run only Frame-to-redraw weapon-switch cases with the real HUD/SDK fixture."""
import argparse,hashlib,importlib.util,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def record(path):
    path=Path(path).resolve();data=path.read_bytes();return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def read(path):return json.loads(Path(path).read_text('utf-8-sig'))
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('proposal','output','deployed-source'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--old-ref',default='HEAD');args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Use a new workspace build directory')
    out.mkdir(parents=True);plan=read(args.proposal);env=os.environ|read(plan['environment']['path'])
    compiler=next(j['command'] for j in plan['jobs'] if j['name']=='compile-client');flags=[];skip=False
    for value in compiler[1:]:
        if value=='--':break
        if skip:skip=False;continue
        if value=='/clang:-ivfsoverlay':skip=True;continue
        if value=='-c' or value.startswith(('/Fo','/Fd')) or ('\\build\\' in value and value.startswith('-I')):continue
        flags.append(value)
    roots=['tests/csol-hud-style/switch.cpp','native/csol/hud_style.cpp','native/csol/image_codec.cpp']
    frozen={};pending=[ROOT/p for p in roots]
    while pending:
        path=pending.pop().resolve()
        if path in frozen:continue
        if not path.is_relative_to(ROOT):raise ValueError('Source outside workspace')
        target=out/'source'/path.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(path.read_bytes())
        assert target.stat().st_nlink==1
        frozen[path]=dict(original=record(path),frozen=record(target))
        for name in re.findall(rb'^\s*#\s*include\s*"([^"\r\n]+)"',path.read_bytes(),re.M):
            name=name.decode();found=next((p for p in (path.parent/name,ROOT/name) if p.is_file()),None)
            if found:pending.append(found)
    overlay=dict(version=0,**{'case-sensitive':False,'use-external-names':False},roots=[dict(type='file',name=str(p),**{'external-contents':v['frozen']['path']}) for p,v in frozen.items()])
    original_ref=subprocess.check_output(['git','rev-parse',args.old_ref],cwd=ROOT,text=True).strip()
    hud='native/csol/client/hud_style.cpp';working=Path(frozen[ROOT/hud]['frozen']['path']).read_bytes()
    head=subprocess.check_output(['git','show',original_ref+':'+hud],cwd=ROOT)
    deployed=args.deployed_source.read_bytes()
    spec=importlib.util.spec_from_file_location('cpp_function',ROOT/'tests/common/cpp_function.py');extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
    _,_,old_body=extract.function(deployed,b'void BeginHudStyle(')
    _,_,new_body=extract.function(working,b'void BeginHudStyle(')
    normal=lambda b:b.replace(b'\r\n',b'\n')
    old_normal,new_normal=normal(old_body),normal(new_body)
    inserted=new_normal[new_normal.index(b'    // Prediction/CurWeapon'):new_normal.index(b'    const auto* definition=FindWeaponDefinition(weapon.id);')]
    assert inserted.endswith(b'    PrepareSelection();\n')
    anchor=b'    const auto* definition=FindWeaponDefinition(weapon.id);'
    assert old_normal.count(anchor)==1 and old_normal.replace(anchor,inserted+anchor)==new_normal
    deployed_fixed=deployed.replace(old_body,new_body)
    variants=[('working',working,0),('head-old',head,1),('deployed-fixed',deployed_fixed,0),('deployed-old',deployed,1)]
    jobs=[];sources={};success=False
    def execute(name,argv,expected=0):
        started=time.monotonic();p=subprocess.run(argv,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
        log=out/(name+'.log');log.write_bytes(p.stdout);jobs.append(dict(name=name,command=argv,exitCode=p.returncode,expectedExit=expected,seconds=time.monotonic()-started,log=record(log)))
        (out/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf8');print(name,p.returncode,flush=True)
        if p.returncode!=expected:print(p.stdout.decode('utf8','replace')[-5000:]);raise RuntimeError(name+' failed')
        if name.startswith('run-') and expected:
            assert b'same redraw owns new weapon HUD without legacy ammo fallback' in p.stdout
    def compile_one(name,path,overlay_path):
        execute('compile-'+name,[compiler[0],*flags,'-I'+str(ROOT),'/clang:-ivfsoverlay','/clang:'+str(overlay_path),'/Fo'+str(out/(name+'.obj')),'/Fd'+str(out/(name+'.compile.pdb')),'-c','--',str(ROOT/path)])
    base_overlay=out/'overlay.json';base_overlay.write_text(json.dumps(overlay),encoding='utf8')
    link_base=next(j['command'] for j in plan['jobs'] if j['name']=='link-client')
    link_flags=[x for x in link_base[1:] if not x.startswith(('/out:','/pdb:')) and not x.endswith('.obj')]
    try:
        compile_one('style',roots[1],base_overlay);compile_one('codec',roots[2],base_overlay)
        for label,data,expected in variants:
            target=out/label/hud;target.parent.mkdir(parents=True);target.write_bytes(data);sources[label]=record(target)
            ov=json.loads(json.dumps(overlay));next(r for r in ov['roots'] if Path(r['name'])==ROOT/hud)['external-contents']=str(target)
            path=out/(label+'-overlay.json');path.write_text(json.dumps(ov),encoding='utf8');compile_one(label,roots[0],path)
            execute('link-'+label,[link_base[0],*link_flags,'/out:'+str(out/(label+'.exe')),'/pdb:'+str(out/(label+'.pdb')),*[str(out/(x+'.obj')) for x in (label,'style','codec')]])
            execute('run-'+label,[str(out/(label+'.exe'))],expected)
        success=True
    finally:
        unchanged=all(record(Path(v['original']['path']))==v['original'] for v in frozen.values())
        result=dict(success=success and unchanged,inputsUnchanged=unchanged,oldRef=original_ref,source=list(frozen.values()),variants=sources,jobs=jobs,
            deployedSource=record(args.deployed_source),boundary='Complete real HUD consumer/Init/Paint and MetaHook/VGUI SDK types; controlled CurrentWeapon/resource/surface boundaries. No full DLL, legacy renderer execution, GPU or gameplay.')
        (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    if not unchanged:raise RuntimeError('Source changed during testing')
if __name__=='__main__':main()
