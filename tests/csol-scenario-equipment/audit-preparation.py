"""Static identity and dependency audit; does not run any native program."""
from pathlib import Path
import ast,hashlib,json,re
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/csol-scenario-upgrade-equipment-tests-20261026'
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def ident(p):
    p=Path(p);b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
plan=read(OUT/'source-plan.json');old=read(plan['baseUpgradePlan']['path']);mismatches=[]
rows=[r['candidate'] for r in plan['copies']+plan['forwarders']]+plan['equipmentSavedFiles']+plan['rapidjsonHeaderClosure']
rows += [r['candidate'] for r in old['copies']]+[r['output'] for r in old['extractions']]+old['sdkHeaderReferences']+old['reusedObjects']
rows += [r['saved'] for r in plan['resourceCopies']]+[plan['baseUpgradeHandoff'],plan['baseUpgradePlan'],plan['equipmentCheckpoint']]
for row in rows:
    a=ident(row['path'])
    if (a['bytes'],a['sha256'])!=(row['bytes'],row['sha256']):mismatches.append(row['path'])
flags=Path(plan['jobs'][0]['response']).read_text(encoding='utf-8-sig').splitlines()
directories=[Path(l[3:-1]) for l in flags if l.startswith('/I"')]
env=read(ROOT/'build/csol-scenario-navigation-idle-events-20261026/compiler-environment.json')
directories.extend(Path(p) for p in env['INCLUDE'].split(';') if p)
pending=[Path(p) for p in plan['sources']];seen=set();missing=[]
while pending:
    p=pending.pop().resolve()
    if p in seen:continue
    seen.add(p)
    text=re.sub(r'/\*.*?\*/','',p.read_text(encoding='utf-8-sig',errors='replace'),flags=re.S)
    for quote,name in re.findall(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]',text,re.M):
        if quote=='<' and not name.startswith('rapidjson/'):continue
        possibilities=([p.parent/name] if quote=='"' else [])+[d/name for d in directories]
        found=next((f.resolve() for f in possibilities if f.is_file()),None)
        if found is None:missing.append({'from':str(p),'include':name})
        elif found not in seen:pending.append(found)
for p in (ROOT/'tests/csol-scenario-equipment').glob('*.py'):ast.parse(p.read_text(encoding='utf-8-sig'))
old_group_paths=[s for s in old['sources'] if s.endswith(('resources.cpp','progress.cpp','consumers.cpp','messages.cpp','cancellation.cpp')) and '\\tests\\' in s]
assert len(old_group_paths)==5 and all(p in plan['sources'] for p in old_group_paths)
result={'verifiedInputs':len(rows),'identityMismatches':mismatches,'textIncludeFilesVisited':len(seen),'unresolvedTextIncludes':missing,
    'includeAuditLimitation':'textual quoted and rapidjson includes, not a preprocessor/compiler proof',
    'oldFiveBodiesUnchanged':[ident(p) for p in old_group_paths],'newTUs':len(plan['jobs']),
    'reusedObjects':len(plan['reusedObjects']),'objects':len(plan['links'][0]['objects']),'groups':plan['groups'],
    'pythonAst':'parsed','compilerInvocations':0,'linkerInvocations':0,'nativeRuns':0,
    'compiled':False,'linked':False,'executed':False,'fullAcceptance':'0/9'}
(OUT/'preparation-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({k:result[k] for k in ('verifiedInputs','identityMismatches','textIncludeFilesVisited','unresolvedTextIncludes','newTUs','reusedObjects','objects','compilerInvocations','nativeRuns')}))
assert not mismatches and not missing
