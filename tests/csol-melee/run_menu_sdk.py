#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run only prepared, identity-checked complete SDK menu fixtures."""
import hashlib,json,os,re,subprocess,sys,time
from pathlib import Path
out=Path(sys.argv[1]).resolve();prepared=json.loads((out/'prepared.json').read_text('utf8'))
known={str(Path(x['path']).resolve()).lower():x for x in prepared['inputs']}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert all(sha(x['path'])==x['sha256'] for x in prepared['inputs'])
env=os.environ|prepared['environment'];result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else dict(commands=[],success=False)
def save():(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
for job in prepared['jobs']:
 if any(x['name']==job['name'] and x['exit']==0 and not x['unknown'] for x in result['commands']):continue
 if '--compile-only' in sys.argv and job['name']=='link':break
 cmd=job['command']
 if job['name']=='link':
  rsp=out/'link.rsp';rsp.write_text(subprocess.list2cmdline(cmd[1:]),encoding='utf8');cmd=[cmd[0],'@'+str(rsp)]
 start=time.monotonic();r=subprocess.run(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
 log=out/(job['name']+'.log');log.write_bytes(r.stdout);text=r.stdout.decode('utf8','replace')
 includes=sorted(set(str(Path(x.strip()).resolve()).lower() for x in re.findall(r'Note: including file:\s*(.+)',text)))
 unknown=[x for x in includes if x not in known];changed=[x for x in includes if x in known and sha(x)!=known[x]['sha256']]
 result['commands'].append(dict(name=job['name'],exit=r.returncode,seconds=time.monotonic()-start,log=str(log),includes=includes,unknown=unknown,changed=changed));save()
 print(job['name'],r.returncode,'includes',len(includes),'unknown',len(unknown),'changed',len(changed),flush=True)
 if r.returncode or unknown or changed:print(text[-9000:],unknown,changed);sys.exit(1)
if '--compile-only' in sys.argv:sys.exit(0)
groups=sys.argv[2:] or prepared['groups']
for group in groups:
 assert group in prepared['groups']
 r=subprocess.run([str(out/'menu.exe'),group],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
 log=out/('run-'+group+'.log');log.write_bytes(r.stdout);print(group,r.stdout.decode('utf8','replace'),flush=True)
 result['commands'].append(dict(name='run-'+group,exit=r.returncode,log=str(log)));result['changedAfter']=[x['path'] for x in prepared['inputs'] if sha(x['path'])!=x['sha256']]
 result['success']=r.returncode==0 and not result['changedAfter'];save()
 if not result['success']:sys.exit(1)
