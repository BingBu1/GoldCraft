#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run a prepared SDK fixture one job at a time; preserve the first failure."""
import hashlib,json,os,re,subprocess,sys,time
from pathlib import Path

out=Path(sys.argv[1]).resolve()
prepared=json.loads((out/'prepared.json').read_text('utf-8'))
def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
env=os.environ.copy();env.update(prepared['environment'])
known={str(Path(x['path']).resolve()).lower():x for x in prepared['inputs']}
result={'success':False,'steps':[]}
def save():(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
try:
    for job in prepared['jobs']:
        command=job['command']
        if job['name']=='link':
            rsp=out/'link.rsp';rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf-8')
            command=[command[0],'@'+str(rsp)]
        start=time.monotonic()
        p=subprocess.run(command,cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
        log=out/(job['name']+'.log');log.write_bytes(p.stdout)
        text=p.stdout.decode('utf-8',errors='replace')
        includes=sorted(set(str(Path(x.strip()).resolve()).lower() for x in re.findall(r'Note: including file:\s*(.+)',text)))
        unknown=[x for x in includes if x not in known]
        changed=[x for x in includes if x in known and digest(x)!=known[x]['sha256']]
        result['steps'].append(dict(name=job['name'],exit=p.returncode,seconds=time.monotonic()-start,
            log=str(log),includes=includes,unknown=unknown,changed=changed))
        save();print(job['name'],p.returncode,'includes',len(includes),'unknown',len(unknown),'changed',len(changed),flush=True)
        if p.returncode or unknown or changed:raise RuntimeError('compile/link stage failed: '+job['name'])
    for group in prepared.get('groups',['capacity','reload','drop','pack']):
        start=time.monotonic()
        p=subprocess.run([str(out/'lifecycle.exe'),group],cwd=out,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=45)
        (out/('execute-'+group+'.log')).write_bytes(p.stdout)
        text=p.stdout.decode('utf-8',errors='replace');print(text,flush=True)
        result['steps'].append(dict(name=group,exit=p.returncode,seconds=time.monotonic()-start,output=text));save()
        if p.returncode:raise RuntimeError('behavior group failed: '+group)
    result['success']=True
except BaseException as error:result['error']=str(error)
finally:
    result['changedAfter']=[x['path'] for x in prepared['inputs'] if digest(x['path'])!=x['sha256']]
    if result['changedAfter']:result['success']=False
    save()
sys.exit(0 if result['success'] else 1)
