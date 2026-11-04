#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual unified hit renderer and compare decisions with original oracle."""
import argparse,hashlib,json,os,re,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def record(p):
    p=Path(p).resolve();data=p.read_bytes();return dict(path=str(p),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--sdk',type=Path,required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir()
    baseline=args.sdk.resolve();env=os.environ|read(baseline/'environment.json')
    source={};pending=[ROOT/'tests/csol-monster-impact'/n for n in ['client.cpp','decide.cpp']]
    while pending:
        p=pending.pop().resolve()
        if p in source:continue
        data=p.read_bytes();frozen=out/'source'/p.relative_to(ROOT);frozen.parent.mkdir(parents=True,exist_ok=True);frozen.write_bytes(data)
        source[p]=dict(original=record(p),frozen=record(frozen))
        for token in re.findall(rb'^\s*#\s*include\s+"([^"\r\n]+)"',data,re.M):
            child=(p.parent/token.decode()).resolve()
            if child.is_file() and child.is_relative_to(ROOT) and not child.is_relative_to(ROOT/'external'):pending.append(child)
    overlay={'version':0,'case-sensitive':False,'use-external-names':False,'roots':[dict(type='file',name=str(p),**{'external-contents':r['frozen']['path']}) for p,r in source.items()]}
    overlay_path=out/'overlay.json';overlay_path.write_text(json.dumps(overlay),encoding='utf8')
    template=next(j['command'] for j in read(baseline/'build-commands.json') if j['kind']=='compile')
    jobs=[];known={str(Path(x['path']).resolve()).lower():x for x in read(baseline/'inputs.json')['files']}
    def execute(name,cmd):
        start=time.monotonic();p=subprocess.run(cmd,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
        log=out/(name+'.log');log.write_bytes(p.stdout);text=p.stdout.decode('utf8','replace');includes=sorted(set(x.strip() for x in re.findall(r'Note: including file:\s*(.+)',text)))
        inputs=[];unknown=[]
        for filename in includes:
            path=Path(filename).resolve()
            if path in source:row=source[path]['frozen'];assert record(row['path'])['sha256']==row['sha256'];inputs.append(row)
            elif str(path).lower() in known:
                row=known[str(path).lower()];assert record(path)['sha256']==row['sha256'];inputs.append(row)
            else:unknown.append(filename)
        jobs.append(dict(name=name,command=cmd,exit=p.returncode,seconds=time.monotonic()-start,log=record(log),inputs=inputs,unknown=unknown))
        (out/'jobs.json').write_text(json.dumps(jobs,indent=2),encoding='utf8');print(name,p.returncode,'unknown',len(unknown),flush=True)
        if p.returncode or unknown:print(text[-5000:]);raise RuntimeError(name+' failed')
    for label in ['client','decide']:
        cmd=[];skip=False
        for arg in template:
            if skip:skip=False;continue
            if arg=='/clang:-ivfsoverlay':skip=True;continue
            if arg.startswith('/Fo'):arg='/Fo'+str(out/(label+'.obj'))
            elif arg.startswith('/Fd'):arg='/Fd'+str(out/(label+'.pdb'))
            elif arg==template[-1]:arg=str(ROOT/'tests/csol-monster-impact'/(label+'.cpp'))
            elif arg=='--':cmd.extend(['/clang:-ivfsoverlay','/clang:'+str(overlay_path)])
            cmd.append(arg)
        execute('compile-'+label,cmd)
        execute('link-'+label,[template[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(out/(label+'.obj')),'/Fe'+str(out/(label+'.exe')),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1'])
    execute('run-client',[str(out/'client.exe')]);execute('run-original-oracle',[os.sys.executable,'-B',str(ROOT/'tests/csol-monster-impact/run.py'),str(out/'decide.exe')])
    unchanged=all(record(p)['sha256']==r['original']['sha256'] for p,r in source.items())
    (out/'result.json').write_text(json.dumps(dict(success=unchanged,sources=list(source.values()),jobs=jobs,inputsUnchanged=unchanged,
        boundary='Complete production server-message decoder/render TU, real x86 SDK layout, controlled drawing APIs. No live GPU or network claim.',deployed=False),indent=2),encoding='utf8')
    assert unchanged
if __name__=='__main__':main()
