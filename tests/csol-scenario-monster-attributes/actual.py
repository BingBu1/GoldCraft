"""Compare current readers plus attribute methods with independently frozen 76-row expectations."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import sys
sys.dont_write_bytecode=True

def identity(path):
    data=path.read_bytes()
    return {'path':str(path.resolve()),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]
def main():
    parser=argparse.ArgumentParser()
    for name in ['exe','types','rounds','expected','output']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args(); oracle=json.loads(args.expected.read_text(encoding='utf-8'))
    assert oracle['scope']=={'mode':15,'subType':1,'mapID':98,'headerRecord':6,'firstDataRecord':7}
    assert len(oracle['derived'])==76 and len(oracle['rounds'])==2
    run=subprocess.run([str(args.exe.resolve()),'--actual',str(args.types),str(args.rounds)],text=True,capture_output=True,timeout=30)
    expected_lines=['tables 38 2 6']
    for index,round_row in enumerate(oracle['rounds']):
        deltas=round_row['deltas']; order=['damageAdd','attackRangeAdd','healthAdd','armorAdd','speedAdd','IntervalAdd','moneyAdd']
        expected_lines.append(' '.join(map(str,['round',index,round_row['recordIndex'],round_row['round'],*[deltas[key] for key in order]])))
        for row in oracle['derived']:
            if row['recordIndex']!=round_row['recordIndex']:continue
            value=row['resolved']; order=['health','armor','attackDamage','attackRange','attackInterval','speed']
            expected_lines.append(' '.join(map(str,['value',index,row['typeID'],*[bits(value[key]) for key in order],value['bonusMoney']])))
    actual=run.stdout.splitlines(); failures=[]
    for index in range(max(len(actual),len(expected_lines))):
        got=actual[index] if index<len(actual) else None; want=expected_lines[index] if index<len(expected_lines) else None
        if got!=want:failures.append({'line':index,'expected':want,'actual':got})
    complete=run.returncode==0 and len(actual)==79
    evidence={'scope':'current decoded resource readers -> physical round deltas -> production scalar attributes',
        'derivedExpectationsFromOriginalReview':True,'checks':len(expected_lines),'resolvedRows':76,
        'rounds':2,'types':38,'failures':failures,'complete':complete,'returnCode':run.returncode,'stderr':run.stderr,
        'inputs':[identity(path) for path in [args.exe,args.types,args.rounds,args.expected,Path(__file__)]],
        'limitations':['No encrypted/archive loader or engine file search in this new target',
            'Existing ZT and round parser correctness is referenced separately; this is new attribute integration',
            'No appearance, entity application, behavior or live gameplay'], 'liveGameplay':False}
    args.output.write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'checks':len(expected_lines),'resolvedRows':76,'complete':complete,'failures':len(failures)}))
    return 0 if complete and not failures else 1
if __name__=='__main__':raise SystemExit(main())
