"""Seal preparation artifacts only; no build or native execution."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/csol-scenario-player-upgrades-tests-20261026'
def ident(path):
    b=path.read_bytes();return {'path':str(path),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def read(path):return json.loads(path.read_text(encoding='utf-8-sig'))
assert not (OUT/'handoff.json').exists(),'Prepared handoff is immutable'
plan=read(OUT/'source-plan.json');audit=read(OUT/'preparation-check.json')
assert not audit['mismatches'] and not audit['unresolvedQuotedIncludes']
assert audit['compilerInvocations']==audit['linkerInvocations']==audit['nativeRuns']==0
test_names=['fixture.hpp','fixture.cpp','main.cpp','resources.cpp','progress.cpp','consumers.cpp','messages.cpp',
    'cancellation.cpp','sdk-boundary.cpp','prepare.py','audit-preparation.py','seal-preparation.py','README.md']
files=[ident(OUT/name) for name in ('source-plan.json','preparation-check.json','preparation-report.md')]
files += [ident(ROOT/'tests/csol-scenario-player-upgrades'/name) for name in test_names]
responses=[ident(Path(job['response'])) for job in plan['jobs']]+[ident(Path(row['response'])) for row in plan['links']]
handoff={'status':'one upgrade function batch prepared; independent review and execution still pending',
    'productionBaseSha256':plan['productionBase']['sha256'],'emptyStatFixSha256':plan['correctedDelta']['corrected']['sha256'],
    'files':files,'responses':responses,'newTranslationUnits':len(plan['jobs']),'reusedObjects':len(plan['reusedObjects']),
    'linkObjectCount':len(plan['links'][0]['objects']),'groups':plan['groups'],
    'staticPreparation':{'verifiedInputs':audit['verifiedInputs'],'identityMismatches':0,
        'quotedIncludeFilesVisited':audit['quotedIncludeFilesVisited'],'unresolvedQuotedIncludes':0},
    'compiled':False,'linked':False,'executed':False,'deployed':False,'gitIndexChanged':False,
    'buildClosureObserved':False,'mode2Playable':False,'fullAcceptance':'0/9',
    'boundary':'actual SDK ResetMaxSpeed/AddAccount methods; character services and rules routing/rejection are controlled edges, not complete GameRules acceptance',
    'remaining':plan['remaining'],'nextAction':'parent assigns independent preparation review and one bounded execution preparation/window',
    'sharedDocumentHandoff':'pending parent /root receipt; this agent does not edit shared continuity or index'}
(OUT/'handoff.json').write_text(json.dumps(handoff,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(ident(OUT/'handoff.json'),ensure_ascii=False))
