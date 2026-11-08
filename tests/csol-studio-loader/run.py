#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the actual client studio loader and guard against pinned SDK headers."""
import argparse, hashlib, json, os, pathlib, re, subprocess, time
ROOT=pathlib.Path(__file__).resolve().parents[2]
def ident(p):
    p=pathlib.Path(p).resolve(strict=True)
    return {'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed',type=pathlib.Path,required=True,help='Verified run_current_client.py plan')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--baseline-ref',help='Compile prior loader directly from Git via stdin; expect duplicate-scan rejection')
    args=parser.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('Fresh workspace build output required')
    seed=json.loads(args.seed.read_text(encoding='utf-8-sig'))
    for row in (seed['compiler'],seed['environment']):
        if ident(row['path'])['sha256']!=row['sha256']:raise ValueError('Changed toolchain')
    env=dict(os.environ);env.update(json.loads(pathlib.Path(seed['environment']['path']).read_text(encoding='utf-8-sig')))
    env['VSLANG']='1033';out.mkdir(parents=True)
    compile_row=next(c for c in seed['commands'] if c['name']=='compile-sequence-client')
    flags=compile_row['argv'][:compile_row['argv'].index('/c')]
    sources=[ROOT/'tests/csol-studio-loader'/s for s in ('main.cpp','loader.cpp','append.cpp')]+[ROOT/'native/csol/model_codec.cpp',ROOT/'native/csol/sprite_codec.cpp']
    watched={p.resolve():ident(p) for root in (ROOT/'native/csol',ROOT/'tests/csol-studio-loader',ROOT/'tests/vgui-resource-refresh') for p in root.rglob('*') if p.suffix in ('.cpp','.hpp')}
    watched[pathlib.Path(__file__).resolve()]=ident(__file__)
    result={'success':False,'sourceTreesCopied':False,'liveGameplay':False,'commands':[],'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'seed':ident(args.seed)}
    def save(): (out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    baseline=None
    if args.baseline_ref:
        old=subprocess.check_output(['git','show',args.baseline_ref+':native/csol/client/resources.cpp'],cwd=ROOT)
        wrapper=(ROOT/'tests/csol-studio-loader/loader.cpp').read_text(encoding='utf-8').split('void LoadFixture',1)[1]
        baseline=('#include "'+str(ROOT/'tests/csol-studio-loader/validation.hpp').replace('\\','/')+'"\n').encode()+old+('\nvoid LoadFixture'+wrapper).encode()
        result['baseline']={'ref':args.baseline_ref,'loaderSha256':hashlib.sha256(old).hexdigest(),'stdinSourceOnly':True}
    def source_args(i,src):
        return ['/I'+str(ROOT/'native/csol/client'),'/TP','-'] if baseline and i==1 else [str(src)]
    def run(name,cmd,stdin=None):
        start=time.monotonic();done=subprocess.run(cmd,input=stdin,env=env,cwd=out,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
        output=(done.stdout+done.stderr).decode('utf-8',errors='replace')
        log='\n'.join(line for line in output.splitlines() if 'Note: including file:' in line or ': error:' in line) if name.startswith('deps-') else output
        (out/(name+'.log')).write_text(log,encoding='utf-8')
        result['commands'].append({'name':name,'argv':cmd,'exit':done.returncode,'seconds':time.monotonic()-start});save()
        print(name,done.returncode,flush=True)
        if name=='test' and baseline:
            if done.returncode!=1 or 'packed model validated more than once' not in output:raise RuntimeError('Baseline did not reproduce duplicate scan')
            result['baselineFailureReproduced']=True
        elif done.returncode:raise RuntimeError('See '+str(out/(name+'.log')))
        return output
    try:
        # Preprocess first to pin the actual SDK and production dependencies.
        dependencies=set()
        for i,src in enumerate(sources):
            text=run('deps-'+str(i),[*flags,'/E',*source_args(i,src)],baseline if i==1 else None)
            dependencies.update(pathlib.Path(p).resolve() for p in re.findall(r'^\s*Note: including file:\s*(.+?)\s*$',text,re.M))
            # Do not retain preprocessed source copies.
            (out/('deps-'+str(i)+'.log')).write_text('\n'.join(p for p in text.splitlines() if 'Note: including file:' in p),encoding='utf-8')
        if not dependencies:raise RuntimeError('No actual includes')
        for p in dependencies:watched.setdefault(p,ident(p))
        (out/'inputs.json').write_text(json.dumps(list(watched.values()),indent=2),encoding='utf-8')
        objects=[]
        for i,src in enumerate(sources):
            obj=out/(str(i)+'.obj');objects.append(str(obj));run('compile-'+str(i),[*flags,'/c',*source_args(i,src),'/Fo'+str(obj)],baseline if i==1 else None)
        glew=ROOT/'build/resource-bridge-20261108/renderer/thirdparty/GLEW/lib/glew.lib'
        result['glew']=ident(glew)
        exe=out/'test.exe'
        run('link',[flags[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objects,str(glew),'opengl32.lib','gdi32.lib','user32.lib','/Fe'+str(exe),'/link','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1'])
        run('test',[str(exe)])
        result['inputsUnchanged']=all(ident(p)==v for p,v in watched.items())
        if not result['inputsUnchanged']:raise RuntimeError('Inputs changed')
        result['artifact']=ident(exe);result['success']=True;save()
    except Exception:save();raise
if __name__=='__main__':main()
