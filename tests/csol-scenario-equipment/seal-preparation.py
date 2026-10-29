"""Seal the one combined preparation handoff, without running native tools."""
from pathlib import Path
import ast,hashlib,json
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/csol-scenario-upgrade-equipment-tests-20261026'
def ident(p):
    b=p.read_bytes();return {'path':str(p),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()}
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
assert not (OUT/'handoff.json').exists(),'combined handoff immutable'
plan=read(OUT/'source-plan.json');audit=read(OUT/'preparation-check.json')
assert not audit['identityMismatches'] and not audit['unresolvedTextIncludes']
assert audit['compilerInvocations']==audit['linkerInvocations']==audit['nativeRuns']==0
names=['fixture.hpp','fixture.cpp','main.cpp','resources.cpp','selection.cpp','effects.cpp','services.cpp',
    'prepare.py','audit-preparation.py','seal-preparation.py','README.md']
for name in names:
    if name.endswith('.py'):ast.parse((ROOT/'tests/csol-scenario-equipment'/name).read_text(encoding='utf-8-sig'))
files=[ident(OUT/name) for name in ('source-plan.json','preparation-check.json','preparation-report.md')]
files += [ident(ROOT/'tests/csol-scenario-equipment'/name) for name in names]
responses=[ident(Path(j['response'])) for j in plan['jobs']]+[ident(Path(j['response'])) for j in plan['links']]
evidence=[ident(ROOT/'analysis/csol-model-20261017/scenario'/name) for name in
    ('equipment-abilities/extraction.json','equipment-abilities/buff-resource.json',
     'equipment-abilities/buff-query.asm','equipment-abilities/buff-clear-rebuild-global.asm',
     'upgrade-consumers-final/extraction.json','upgrade-consumers-final/2026-10-26_分析-升级状态与实际消费者-report.md')]
result={'status':'single combined upgrade/equipment batch prepared; not executed',
    'equipmentCheckpoint':plan['equipmentCheckpoint'],'baseUpgradeHandoff':plan['baseUpgradeHandoff'],
    'files':files,'responses':responses,'originalEvidence':evidence,'newTranslationUnits':31,'reusedObjects':3,
    'linkObjectCount':34,'groups':plan['groups'],'oldFiveGroupsUnchanged':True,
    'staticPreparation':{'verifiedInputs':audit['verifiedInputs'],'identityMismatches':0,
        'textIncludeFilesVisited':audit['textIncludeFilesVisited'],'unresolvedTextIncludes':0},
    'compiled':False,'linked':False,'executed':False,'deployed':False,'gitIndexChanged':False,
    'buildClosureObserved':False,'sessionCompiled':False,'mode2Playable':False,'fullAcceptance':'0/9',
    'supersedesExecutionPlan':plan['baseUpgradeHandoff']['path'],'preservesOldFiles':True,
    'remaining':plan['remaining'],'nextAction':'parent assigns independent narrow preparation review, complete input gate and one coordinator execution window',
    'sharedDocumentHandoff':'pending parent /root receipt; shared documentation/index unchanged'}
(OUT/'handoff.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(ident(OUT/'handoff.json'),ensure_ascii=False))
