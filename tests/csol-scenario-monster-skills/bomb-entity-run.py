# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import hashlib,json,os,re,subprocess,time,sys
out=Path(sys.argv[1]).resolve();p=json.loads((out/'prepared.json').read_text());env=os.environ.copy();env.update(p['environment'])
known={str(Path(x['path']).resolve()).lower():x for x in p['inputs']}
def sha(q):return hashlib.sha256(Path(q).read_bytes()).hexdigest()
def save(): (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
assert all(sha(x['path'])==x['sha256'] for x in p['inputs']), 'input changed since preparation'
result=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else dict(commands=[],success=False)
for job in p['jobs']:
 if any(x['name']==job['name'] and x['exit']==0 and not x['unknown'] and not x['changed'] for x in result['commands']):continue
 command=job['command']
 if job['name']=='link':
  rsp=out/'link.rsp';rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf-8');command=[command[0],'@'+str(rsp)]
 start=time.monotonic();process=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
 log=out/('compile-test.log' if job['name']=='test' else job['name']+'.log');log.write_bytes(process.stdout)
 text=process.stdout.decode('utf-8',errors='replace')
 includes=sorted(set(str(Path(x.strip()).resolve()).lower() for x in re.findall(r'Note: including file:\s*(.+)',text)))
 unknown=[x for x in includes if x not in known];changed=[x for x in includes if x in known and sha(x)!=known[x]['sha256']]
 result['commands'].append(dict(name=job['name'],exit=process.returncode,seconds=time.monotonic()-start,log=str(log),includes=includes,unknown=unknown,changed=changed));save()
 print(job['name'],process.returncode,'includes',len(includes),'unknown',len(unknown),'changed',len(changed),flush=True)
 if process.returncode or unknown or changed:
  print('\n'.join(text.splitlines()[-30:]));print(unknown);print(changed);sys.exit(1)
groups=sys.argv[2:] or ['spawn','flight','touch','lifetime','edges']
assert groups and all(g in ['spawn','flight','touch','lifetime','edges'] for g in groups), 'unknown test group'
for group in groups:
 start=time.monotonic();q=subprocess.run([str(out/'entity.exe'),group],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
 log=out/('execute-'+group+'.log');log.write_bytes(q.stdout);print(q.stdout.decode(errors='replace'),flush=True)
 result['commands'].append(dict(name='execute-'+group,exit=q.returncode,seconds=time.monotonic()-start,log=str(log)))
 result['changedAfter']=[x['path'] for x in p['inputs'] if sha(x['path'])!=x['sha256']]
 result['success']=q.returncode==0 and not result['changedAfter'];save()
 if not result['success']:sys.exit(1)
sys.exit(0)
