#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual production presence checks and compare their old full-read version."""
import argparse,hashlib,importlib.util,json,os,re,shutil,subprocess,time
from pathlib import Path

def identity(path):
    p=Path(path).resolve();return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def read(path):return json.loads(Path(path).read_text('utf-8-sig'))
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','output','before','hud','menu'):ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--compiled',type=Path)
    a=ap.parse_args();root=Path(__file__).resolve().parents[2];out=a.output.resolve();out.mkdir();source=out/'source';inputs=[]
    if a.compiled:a.compiled=a.compiled.resolve()
    production=['native/csol/server/characters.cpp','native/csol/server/buy_menu.cpp','native/csol/character_catalog.cpp','native/csol/hud_style.cpp','native/csol/buy_menu.cpp']
    production_before=[identity(root/p) for p in production]
    paths=set(root/r for r in production+['tests/csol-resource-presence/main.cpp'])
    for tree in ('native/csol','native/include'):
        paths.update(p for p in (root/tree).rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.inl','.inc'))
    for p in sorted(paths):
        target=source/p.relative_to(root);target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,target);assert target.stat().st_nlink==1;inputs.append(identity(target))
    overlay=out/'source-overlay.json'
    overlay.write_text(json.dumps({'version':0,'case-sensitive':False,'use-external-names':True,'roots':[{'type':'file','name':str(p),'external-contents':str(source/p.relative_to(root))} for p in sorted(paths)]}),encoding='utf8')
    env=os.environ.copy();env.update(read(a.baseline/'environment.json'));(out/'temp').mkdir();env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'),VSLANG='1033')
    original=next(c['command'] for c in read(a.baseline/'build-commands.json') if c['kind']=='compile')
    common=[original[0]]
    for arg in original[1:-1]:
        if arg.startswith(('/Fo','/Fd','/clang:-ivfsoverlay')) or arg.endswith('source-overlay.json') or arg in ('/c','/JMC','/Zi','/arch:IA32'):continue
        if arg.startswith('/I') and 'BUILD' in arg.upper():continue
        common.append(arg)
    common+=['/I'+str(source/'native/include'),'/I'+str(source/'native/csol/server'),'/clang:-ivfsoverlay','/clang:'+str(overlay)]
    spec=importlib.util.spec_from_file_location('cpp_function',root/'tests/common/cpp_function.py');extract=importlib.util.module_from_spec(spec);spec.loader.exec_module(extract)
    extracted=[];generated=[]
    def focused(rel,raw,old=False):
        if rel.endswith('characters.cpp'):
            marker=b'std::string Load(';names=[b'std::string Load(',b'void PrecacheHudStyle()',b'void CSOL_PrecacheCharacters()'];inside=2
        else:
            marker=b'void Send(';names=[b'void Send(',b'void CSOL_ResetBuyMenu()',b'bool CSOL_PrecacheBuyMenu()'];inside=1
        parts=[raw[:raw.index(marker)]]
        for index,name in enumerate(names):
            if index==inside:parts.append(b'}\n')
            start,end,body=extract.function(raw,name);parts.append(body+b'\n');extracted.append(dict(path=rel,old=old,signature=name.decode(),start=start,end=end,sha256=hashlib.sha256(body).hexdigest()))
        p=source/rel;p=p.with_name(p.stem+('_presence_old.cpp' if old else '_presence.cpp'));p.write_bytes(b''.join(parts));generated.append(identity(p));return p
    commands=[];steps=[];artifacts=[];all_includes=set()
    def run(name,argv):
        commands.append(dict(name=name,argv=argv));log=out/(name+'.log');start=time.monotonic()
        with log.open('wb') as stream:p=subprocess.run(argv,cwd=out,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
        text=log.read_text('utf8',errors='replace');all_includes.update(re.findall(r'^\s*Note: including file:\s*(.*?)\s*$',text,re.M))
        steps.append(dict(name=name,exit=p.returncode,seconds=time.monotonic()-start,log=identity(log)));print(name,p.returncode,flush=True);return p.returncode
    success=False
    try:
        objects=[]
        for rel in production+['tests/csol-resource-presence/main.cpp']:
            name=rel.replace('/','_').replace('.cpp','');obj=out/(name+'.obj');objects.append(obj)
            if rel in production[:2]:
                if a.compiled:
                    previous=read(a.compiled/'receipt.json');expected=next(x for x in previous['source'] if x['path']==str(root/rel));assert identity(root/rel)==expected
                    assert next(x for x in previous['steps'] if x['name']=='compile-'+name)['exit']==0
                elif run('compile-full-'+name,common+['/c',str(source/rel),'/Fo'+str(out/(name+'-full.obj'))]):return 1
                unit=focused(rel,(source/rel).read_bytes())
            else:unit=source/rel
            if a.compiled and rel in production[2:]:
                previous=read(a.compiled/'receipt.json');expected=next(x for x in previous['source'] if x['path']==str(root/rel));assert identity(root/rel)==expected
                obj=a.compiled/(name+'.obj');objects[-1]=obj
            elif run('compile-'+name,common+['/c',str(unit),'/Fo'+str(obj)]):return 1
        link=[common[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT']
        if run('link-new',link+list(map(str,objects))+['/Fe'+str(out/'new.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']):return 1
        if run('test-new',[str(out/'new.exe'),str(a.hud.resolve()),str(a.menu.resolve())]):return 1
        old_objects=list(objects)
        for index,rel in enumerate(production[:2]):
            p=a.before/rel;target=focused(rel,p.read_bytes(),True);obj=out/('old-'+objects[index].name);old_objects[index]=obj
            if run('compile-old-'+str(index),common+['/c',str(target),'/Fo'+str(obj)]):return 1
        if run('link-old',link+list(map(str,old_objects))+['/Fe'+str(out/'old.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']):return 1
        old_exit=run('test-old',[str(out/'old.exe'),str(a.hud.resolve()),str(a.menu.resolve())])
        old_text=(out/'test-old.log').read_text('utf8');success=old_exit==1 and 'presence validation never reads asset body' in old_text
        return 0 if success else 1
    finally:
        unchanged=all(identity(row['path'])==row for row in inputs+production_before)
        result=dict(success=success and unchanged,inputsUnchanged=unchanged,steps=steps,commands=commands,compiledReuse=identity(a.compiled/'receipt.json') if a.compiled else None,extracted=extracted,generated=generated,source=production_before,inputs=inputs,headers=[identity(p) for p in sorted(all_includes)],artifacts=[identity(p) for p in out.glob('*.exe')])
        (out/'receipt.json').write_text(json.dumps(result,indent=2),encoding='utf8')
        if not unchanged:raise RuntimeError('test inputs changed')
if __name__=='__main__':raise SystemExit(main())
