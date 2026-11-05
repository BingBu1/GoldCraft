# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,time
out=Path(sys.argv[1]).resolve();mode=sys.argv[2];assert mode in ['compile','link','run']
p=json.loads((out/'prepared.json').read_text('utf8'));known={str(Path(x['path']).resolve()).casefold():x for x in p['inputs']}
env=os.environ.copy();env.update(p['environment']);env['TEMP']=env['TMP']=str(out/'tmp');(out/'tmp').mkdir(exist_ok=True)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def identity(path):return dict(path=str(Path(path).resolve()),sha256=sha(path),bytes=Path(path).stat().st_size)
assert all(sha(x['path'])==x['sha256']for x in p['inputs']),'frozen input drift'
result=dict(mode=mode,commands=[],success=False)
def save():(out/(mode+'-result.json')).write_text(json.dumps(result,indent=2)+'\n','utf8')
if mode=='link':
 build=json.loads((Path(p['baseline'])/'build-result.json').read_text('utf8'));assert build['success'] and build['inputsUnchanged']
for job in p['jobs'] if mode!='run' else [dict(name='run-'+g,command=[str(out/('old.exe'if g=='old'else'new.exe')),str(out/'data'),g])for g in ['collision','damage','lifecycle','callbacks','old']]:
 if mode!='run' and not job['name'].startswith(mode):continue
 command=job['command'];dependencies=[]
 if mode=='link':
  dependencies=[identity(x)for x in command if Path(x).is_absolute()and Path(x).is_file()and Path(x).suffix.lower()in['.obj','.lib','.res']]
  command=command+['/lldltocache:'+str(out.parent/'shield-sdk-lto-cache')]
  rsp=out/(job['name']+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),'utf8');command=[command[0],'@'+str(rsp)]
 start=time.monotonic();q=subprocess.run(command,env=env,cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
 log=out/(job['name']+'.log');log.write_bytes(q.stdout);text=q.stdout.decode('utf8',errors='replace')
 includes=sorted(set(str(Path(x.strip()).resolve()).casefold()for x in re.findall(r'Note: including file:\s*(.+)',text)))
 unknown=[x for x in includes if x not in known];changed=[x for x in includes if x in known and sha(x)!=known[x]['sha256']]
 expected=job['name']=='run-old';ok=(q.returncode==1 if expected else q.returncode==0)and not unknown and not changed
 if expected:ok=ok and 'ASSERT factory model application sets ordinary shield exclusion' in text and 'hp=1000->1000 blood=0 sparks=1'in text
 result['commands'].append(dict(name=job['name'],exit=q.returncode,seconds=time.monotonic()-start,log=identity(log),includes=includes,unknown=unknown,changed=changed,dependencies=dependencies,executionCommand=command,success=ok));save()
 print(job['name'],q.returncode,'unknown',len(unknown),'changed',len(changed),flush=True)
 if mode=='run'or not ok:print(text[-7000:],flush=True)
 if not ok:sys.exit(1)
result['changedAfter']=[x['path']for x in p['inputs']if sha(x['path'])!=x['sha256']];result['success']=not result['changedAfter'];save()
