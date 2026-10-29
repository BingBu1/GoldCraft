"""Freeze exact production method plus original Git reference for guard tests."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
RELATIVE='regamedll/dlls/basemonster.cpp'

def ident(p):
    data=p.read_bytes()
    return dict(path=str(p.resolve()),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())

def method(source):
    start=source.index('int CBaseMonster::IRelationship(CBaseEntity *pTarget)')
    at=source.index('{',start)
    depth=1; end=at+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[start:end]+'\n'

def table(source):
    start=source.index('static int const iEnemy[14][14]')
    return source[start:source.index('\n\t};',start)+5]

def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    repo=ROOT/'external/ReGameDLL_CS'; source=repo/RELATIVE
    reference_commit=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
    assert reference_commit=='4a50c42e85fd3778c2b7d24731c7d30c83bbab01'
    old=subprocess.check_output(['git','-C',str(repo),'show',f'{reference_commit}:{RELATIVE}']).decode('utf8')
    current=method(source.read_text(encoding='utf-8-sig'));reference=method(old)
    assert table(current)==table(reference),'legacy table changed beyond guard scope'
    assert 'return iEnemy[Classify()][pTarget->Classify()];' in reference
    assert 'static_cast<unsigned int>(ownClass)' in current and 'static_cast<unsigned int>(targetClass)' in current
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'current_method.inc').write_text(current,encoding='utf8')
    (args.output/'reference_method.inc').write_text(reference.replace('CBaseMonster::IRelationship','ReferenceMonster::IRelationship',1),encoding='utf8')
    data=dict(scope='Exact method extraction with controlled Classify methods, not SDK ABI or AI; reference invalid indices are never executed',
        originalCommit=reference_commit,originalPath=RELATIVE,productionSource=ident(source),tableUnchanged=True,
        referenceMethodSha256=hashlib.sha256(reference.encode()).hexdigest(),
        files=[ident(args.output/name) for name in ['current_method.inc','reference_method.inc']],generator=ident(Path(__file__)))
    (args.output/'extraction.json').write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(ident(args.output/'extraction.json'),ensure_ascii=False))

if __name__=='__main__':main()
