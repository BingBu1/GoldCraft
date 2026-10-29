"""Read-only independent audit of the concrete cannon deployment plan."""
from pathlib import Path
import hashlib
import json

root=Path(__file__).resolve().parents[2]
out=root/'build/csol-cannon-20261024';sandbox=root/'sandbox'
plan=json.loads((out/'deployment-plan.json').read_text())
resources=json.loads((out/'resources-manifest.json').read_text())['files']
report={'checks':[],'changes':[]}
def check(value,label):
    report['checks'].append({'label':label,'passed':bool(value)})
    if not value:raise AssertionError(label)
def verify(row):
    path=Path(row['path']);data=path.read_bytes()
    check(len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256'],'fingerprint '+str(path))
for row in plan['changes']:
    verify(row['source']);target=Path(row['target']);backup=Path(row['backup'])
    check(target.resolve().is_relative_to(sandbox),'sandbox target')
    check(backup.resolve().is_relative_to(out/'runtime-before') and not backup.exists(),'new backup under this batch')
    check(not any(p.is_symlink() or p.is_junction() for p in (target,*target.parents) if p.is_relative_to(root)),'no destination links')
    if row['before']:verify(row['before'])
    else:check(not target.exists(),'new target still absent')
    report['changes'].append({'target':str(target.relative_to(sandbox)),'sourceSha256':row['source']['sha256'],'willReplace':bool(row['before'])})
for row in plan['protected']:verify(row)
for row in plan['unchangedResources']:verify(row)
client=[r for r in plan['changes'] if Path(r['target']).is_relative_to(sandbox/'cs-client-b')]
check(len(client)==2 and {Path(r['target']).suffix for r in client}=={'.dll','.pdb'},'client changes only paired DLL/PDB; no asset preseeding')
check(len({r['target'].lower() for r in plan['changes']})==len(plan['changes']),'unique targets')
check(len(resources)==21,'complete 21 resource closure')
resource_targets={str((sandbox/'cs-server/Half-Life/cstrike'/r['path']).resolve()).lower() for r in resources}
planned_targets={str(Path(r['target']).resolve()).lower() for r in plan['changes']}
unchanged_targets={str(Path(r['path']).resolve()).lower() for r in plan['unchangedResources']}
check(resource_targets.issubset(planned_targets|unchanged_targets),'all resources applied or pinned unchanged')
old=json.loads((sandbox/'cs-server/rehlds-deployment.json').read_text(encoding='utf-8-sig'))
new=json.loads((out/'rehlds-deployment-updated.json').read_text())
oldentry=next(r for r in old['files'] if r['name'].replace('\\','/').lower()=='cstrike/dlls/mp.dll')
newentry=next(r for r in new['files'] if r['name'].replace('\\','/').lower()=='cstrike/dlls/mp.dll')
newentry.update(bytes=oldentry['bytes'],sha256=oldentry['sha256'])
check(old==new,'launcher metadata differs only in current mp fingerprint')
report.update(planSha256=hashlib.sha256((out/'deployment-plan.json').read_bytes()).hexdigest(),
    resourceSha256=hashlib.sha256((out/'resources-manifest.json').read_bytes()).hexdigest(),
    changeCount=len(plan['changes']),protectedCount=len(plan['protected']),unchangedResourceCount=len(plan['unchangedResources']),applied=False)
(out/'server-tests/deployment-plan-review.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('changes','checks')}))
print(len(report['checks']),'checks PASS')
for row in report['changes']:print(row['target'])
