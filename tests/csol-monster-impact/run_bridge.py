#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run actual messages and CsoMonFX dispatcher TUs with bounded engine stubs."""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def read(p): return json.loads(Path(p).read_text('utf-8-sig'))
def record(p):
    p=Path(p).resolve(); b=p.read_bytes(); return dict(path=str(p),bytes=len(b),sha256=hashlib.sha256(b).hexdigest())
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--client-sdk',type=Path,required=True);ap.add_argument('--server-sdk',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    out=a.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir()
    groups={'messages':('server', ['native/csol/server/scenario_messages.cpp','tests/csol-scenario-monster-skills/messages.cpp']),
            'receiver':('client',['native/csol/client/scenario_effects.cpp','native/csol/client/monster_impact.cpp','tests/csol-scenario-monster-skills/client.cpp'])}
    bases={'client':a.client_sdk.resolve(),'server':a.server_sdk.resolve()}
    sources={};pending=[ROOT/n for _,ns in groups.values() for n in ns]
    pending += [p for p in (ROOT/'native').rglob('*') if p.is_file() and p.suffix in ('.h','.hpp','.inl','.inc')]
    while pending:
        p=pending.pop().resolve()
        if p in sources:continue
        b=p.read_bytes();dst=out/'source'/p.relative_to(ROOT);dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(b)
        sources[p]=dict(original=record(p),frozen=record(dst))
        for token in re.findall(rb'^\s*#\s*include\s+"([^"\r\n]+)"',b,re.M):
            child=(p.parent/token.decode()).resolve()
            if child.is_file() and child.is_relative_to(ROOT) and not child.is_relative_to(ROOT/'external'):pending.append(child)
    jobs=[]
    for group,(side,names) in groups.items():
        base=bases[side];env=os.environ|read(base/'environment.json');template=next(j['command'] for j in read(base/'build-commands.json') if j['kind']=='compile')
        known={str(Path(x['path']).resolve()).lower():x for x in read(base/'inputs.json')['files']}
        for header in ('iostream','source_location'):
            for directory in env.get('INCLUDE','').split(';'):
                p=Path(directory)/header
                if p.is_file():known[str(p.resolve()).lower()]=record(p);break
        overlay={'version':0,'case-sensitive':False,'use-external-names':False,'roots':[dict(type='file',name=str(p),**{'external-contents':r['frozen']['path']}) for p,r in sources.items()]}
        ov=out/(side+'-overlay.json');ov.write_text(json.dumps(overlay),encoding='utf8')
        def execute(label,cmd):
            start=time.monotonic();r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
            log=out/(label+'.log');log.write_bytes(r.stdout);text=r.stdout.decode('utf8','replace');unknown=[];used=[]
            for n in sorted(set(re.findall(r'Note: including file:\s*(.+)',text))):
                p=Path(n.strip()).resolve();row=sources[p]['frozen'] if p in sources else known.get(str(p).lower())
                if row is None:unknown.append(str(p))
                else:assert record(row['path'])['sha256']==row['sha256'];used.append(row)
            jobs.append(dict(name=label,command=cmd,exit=r.returncode,seconds=time.monotonic()-start,log=record(log),inputs=used,unknown=unknown))
            (out/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf8');print(label,r.returncode,'unknown',len(unknown),flush=True)
            if r.returncode or unknown:print(text[-6000:],unknown);raise RuntimeError(label)
        objects=[]
        for i,n in enumerate(names):
            obj=out/(group+str(i)+'.obj');objects.append(str(obj));cmd=[];skip=False
            for arg in template:
                if skip:skip=False;continue
                if arg=='/clang:-ivfsoverlay':skip=True;continue
                if arg.startswith('/Fo'):arg='/Fo'+str(obj)
                elif arg.startswith('/Fd'):arg='/Fd'+str(out/(group+str(i)+'.pdb'))
                elif arg.lower().endswith('.cpp'):arg=str(ROOT/n)
                elif arg=='--':continue
                cmd.append(arg)
            cmd+=['/clang:-ivfsoverlay','/clang:'+str(ov)]
            execute(group+'-compile'+str(i),cmd)
        execute(group+'-link',[template[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objects,'/Fe'+str(out/(group+'.exe')),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1'])
        execute(group+'-run',[str(out/(group+'.exe'))])
    unchanged=all(record(p)['sha256']==r['original']['sha256'] for p,r in sources.items())
    (out/'result.json').write_text(json.dumps(dict(success=unchanged,sources=list(sources.values()),jobs=jobs,inputsUnchanged=unchanged,boundary='Actual production message serializer and dispatcher, engine/draw APIs captured; no network or GPU.'),indent=2),encoding='utf8');assert unchanged
if __name__=='__main__':main()
