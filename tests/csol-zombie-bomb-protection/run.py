# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import hashlib,json,os,re,subprocess,time,sys
out=Path(sys.argv[1]).resolve();plan=json.loads((out/'prepared.json').read_text(encoding='utf8'));env=os.environ.copy();env.update(plan['environment'])
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
known={str(Path(r['path']).resolve()).lower():r for r in plan['inputs']}
assert all(sha(r['path'])==r['sha256']for r in known.values())
result=dict(success=False,commands=[],deployed=False)
def save():(out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
for step in plan['commands']:
 start=time.monotonic();p=subprocess.run(step['argv'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
 log=out/(step['name']+'.log');log.write_bytes(p.stdout);text=p.stdout.decode('utf8',errors='replace')
 headers=sorted(set(str(Path(n.strip()).resolve()).lower()for n in re.findall(r'Note: including file:\s*(.+)',text)))
 unknown=[n for n in headers if n not in known];changed=[n for n in headers if n in known and sha(n)!=known[n]['sha256']]
 result['commands'].append(dict(name=step['name'],exit=p.returncode,seconds=time.monotonic()-start,log=str(log),includes=headers,unknown=unknown,changed=changed));save()
 print(step['name'],p.returncode,'headers',len(headers),'unknown',len(unknown),'changed',len(changed),flush=True)
 if p.returncode or unknown or changed:print(text[-7000:],unknown,changed);sys.exit(1)
result['inputsUnchanged']=all(sha(r['path'])==r['sha256']for r in known.values());result['success']=result['inputsUnchanged'];save()
