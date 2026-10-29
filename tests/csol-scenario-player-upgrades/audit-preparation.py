"""Static identities/include resolution only. Does not run component tests."""
from pathlib import Path
import ast, hashlib, json, re
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/csol-scenario-player-upgrades-tests-20261026'
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def ident(path):
    p=Path(path);b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
plan=read(OUT/'source-plan.json');mismatches=[]
rows=[r['candidate'] for r in plan['copies']]+[r['output'] for r in plan['extractions']]+plan['sdkHeaderReferences']+plan['reusedObjects']+[r['saved'] for r in plan['resourceCopies']]
for row in rows:
    actual=ident(row['path'])
    if (actual['bytes'],actual['sha256'])!=(row['bytes'],row['sha256']):mismatches.append(row['path'])
flags=Path(plan['jobs'][0]['response']).read_text(encoding='utf-8-sig').splitlines()
directories=[Path(line[3:-1]) for line in flags if line.startswith('/I"')]
directories.extend(Path(p) for p in read(ROOT/'build/csol-scenario-navigation-idle-events-20261026/compiler-environment.json')['INCLUDE'].split(';') if p)
sources=[Path(s) for s in plan['sources']];visited=set();pending=sources.copy();unresolved=[]
while pending:
    source=pending.pop()
    if source in visited:continue
    visited.add(source)
    for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"',source.read_text(encoding='utf-8-sig',errors='replace'),re.M):
        match=next((p.resolve() for p in [source.parent/name]+[d/name for d in directories] if p.is_file()),None)
        if match is None:unresolved.append({'from':str(source),'include':name})
        elif match not in visited:pending.append(match)
for name in ('prepare.py','audit-preparation.py'):
    ast.parse((ROOT/'tests/csol-scenario-player-upgrades'/name).read_text(encoding='utf-8-sig'))
evidence=read(ROOT/'analysis/csol-model-20261017/scenario/wall-upgrade-consumer/tables.json')
levels,classes=(r['rows'] for r in evidence['resources'])
anchors={'realPhysicalLevelCount':len(levels)-1,'class45':next(r for r in classes[1:] if r[0]=='45'),
    'level1To9ThresholdSum':sum(int(row[1]) for row in levels[2:11]),
    'emptyStatRows':[i for i,row in enumerate(levels[1:]) if all(cell=='' for cell in row[3:8])],
    'emptyXpRows':[i for i,row in enumerate(levels[1:]) if row[1]==''],
    'emptyClassNumericCells':[(i,c) for i,row in enumerate(classes[1:]) for c in range(6) if row[c]=='']}
assert anchors['level1To9ThresholdSum']==18600 and anchors['class45'][6]=='hdahdamms'
assert anchors['emptyStatRows']==list(range(31,101)) and not anchors['emptyXpRows'] and not anchors['emptyClassNumericCells']
factory=ROOT/'build/csol-scenario-monster-factory-20261025'
factory_plan=read(factory/'build-plan.json');reuse_closure=[]
for obj in plan['reusedObjects']:
    job=next(j for j in factory_plan['jobs'] if Path(j['object']).name==Path(obj['path']).name)
    source=Path(job['source']);headers=[];pending=[source];seen=set()
    while pending:
        f=pending.pop()
        if f in seen:continue
        seen.add(f)
        for name in re.findall(r'^\s*#\s*include\s+"([^"]+)"',f.read_text(encoding='utf-8-sig'),re.M):
            old=(f.parent/name).resolve();assert old.is_file(),name
            current=OUT/'candidate'/old.relative_to(factory/'candidate')
            a,b=ident(old),ident(current);assert (a['bytes'],a['sha256'])==(b['bytes'],b['sha256']),(old,current)
            headers.append({'compiledWith':a,'candidate':b})
            pending.append(old)
    reuse_closure.append({'object':obj,'source':ident(source),'equalQuotedHeaders':headers})
result={'verifiedInputs':len(rows),'mismatches':mismatches,'quotedIncludeFilesVisited':len(visited),
    'unresolvedQuotedIncludes':unresolved,'includeAuditLimitation':'textual quoted includes only; not preprocessor/compiler closure',
    'fixedOracleAnchors':anchors,'reuseHeaderClosure':reuse_closure,'pythonAst':'parsed without executing native code',
    'sourceCount':len(sources),'reusedObjects':len(plan['reusedObjects']),'groups':plan['groups'],
    'compilerInvocations':0,'linkerInvocations':0,'nativeRuns':0,'compiled':False,'executed':False}
(OUT/'preparation-check.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'verifiedInputs':len(rows),'mismatches':mismatches,'quotedIncludeFilesVisited':len(visited),'unresolved':unresolved,'compiled':False,'executed':False}))
assert not mismatches
