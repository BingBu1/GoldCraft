"""Parse exactly thirteen frozen MDL headers independently, then run real SDK label lookup."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess
import sys
sys.dont_write_bytecode=True
def identity(path):
    data=path.read_bytes();return {'path':str(path.resolve()),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
def main():
    parser=argparse.ArgumentParser()
    for name in ['models','labels','exe','output']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();models=json.loads(args.models.read_text(encoding='utf-8'))['models'];assert len(models)==13
    contract=json.loads(args.labels.read_text(encoding='utf-8'))['cache'];assert len(contract)==33
    requested=[row['name'].encode('ascii') for row in contract]
    failures=[];rows=[]
    for item in models:
        source=item['source'];path=Path(source['path']);actual=identity(path)
        assert (actual['bytes'],actual['sha256'])==(source['bytes'],source['sha256'])
        data=path.read_bytes();assert data[:4]==b'IDST'
        version=struct.unpack_from('<i',data,4)[0];count,offset=struct.unpack_from('<ii',data,164)
        assert version in [10,21] and 0<count<10000 and offset>=0 and offset+count*176<=len(data)
        labels=[]
        for index in range(count):
            field=data[offset+index*176:offset+index*176+32];assert b'\0' in field
            labels.append(field.split(b'\0')[0])
        # ASCII requested labels; no locale assumptions for non-ASCII model labels.
        def find(name):return next((index for index,label in enumerate(labels) if label.lower()==name.lower()),-1)
        wanted=[find(name) for name in requested]
        if wanted[15]==-1:wanted[15:24]=[find(b'death')]*9
        if wanted[30]==-1:wanted[30]=wanted[1]
        if wanted[31]==-1:wanted[31]=wanted[0]
        if wanted[32]==-1:wanted[32]=wanted[0]
        run=subprocess.run([str(args.exe.resolve()),'--model'],input=data,capture_output=True,timeout=30)
        got=list(map(int,run.stdout.decode('ascii').split()))
        if run.returncode!=0 or got!=wanted:failures.append({'source':actual,'returnCode':run.returncode,'expected':wanted,'actual':got,'stderr':run.stderr.decode('utf-8',errors='replace')})
        rows.append({'appearanceIndex':item['appearanceIndex'],'source':actual,'version':version,'sequenceCount':count,
            'sequenceOneHex':labels[1].hex(),'resolved':wanted,'resolvedSlots':sum(value>=0 for value in wanted),'missingSlots':sum(value<0 for value in wanted)})
    record={'scope':'thirteen exact common MDL files; independently parsed IDST header and labels vs production resolver using extracted exact SDK LookupSequence',
        'models':13,'slotComparisons':13*33,'failures':failures,'rows':rows,
        'inputs':[identity(path) for path in [args.models,args.labels,args.exe,Path(__file__)]],
        'limits':['Original LookupSequence model/string cache not executed','No animation advancement, entity setup, AI or rendering',
            'Source MDL bytes pass via stdin; no extra game asset copies','Original ls suffix fallback not used by the thirty-three names'],
        'liveGameplay':False}
    args.output.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'models':13,'slotComparisons':429,'failures':len(failures)}))
    return 1 if failures else 0
if __name__=='__main__':raise SystemExit(main())
