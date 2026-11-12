#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare actual shared spark Receive/Frame/Spawn/Tick with private original Gift outputs.

Requires matching private producer and real-client instruction replay receipts.
Nonzero first-vector ABI diagnostics are outside the fixed GiftTouch grammar.
No original DLL is loaded natively and no production source is copied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

def identity(path):
    raw=path.read_bytes()
    return dict(path=str(path.resolve()),bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())

def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('executable','producer','client','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():raise FileExistsError(args.output)
    producer=json.loads(args.producer.read_text('utf8'));client=json.loads(args.client.read_text('utf8'))
    if not producer['success'] or not client['success']:raise ValueError('Original replay must pass')
    inputs=[identity(p) for p in (Path(__file__),args.executable,args.producer,args.client)]
    for row in producer['inputs']+client['inputs']:
        if identity(Path(row['path']))!=row:raise ValueError('Original input changed: '+row['path'])
    lines=[];expected=[]
    for source,case in zip(producer['cases'],client['cases'],strict=True):
        if source['firstVector']!=[0,0,0]:continue
        for packet,spawn in zip(source['packets'][:2],case['spawn'],strict=True):
            fields=spawn['fields']
            for update in spawn['updates']:
                lines.append(' '.join([packet['payload'],*[str(bits(update[k])) for k in ('now','previous','engineGravity')]]))
                values=[update['size'],update['alpha'],update['roll'],update['die'],*update['origin'],
                    *update['velocity'],float(update['frame']),fields['born'],fields['growth'],fields['rotationVelocity'][2],fields['gravity']]
                expected.append([int(update['die']==0 or update['now']<update['die']),*map(bits,values)])
    process=subprocess.run([str(args.executable.resolve()),'--gift-runtime-oracle'],input='\n'.join(lines)+'\n',text=True,capture_output=True,timeout=60)
    if process.returncode:raise RuntimeError(process.stderr)
    actual=[json.loads(line) for line in process.stdout.splitlines()]
    differences=[dict(case=n,expected=a,actual=b) for n,(a,b) in enumerate(zip(expected,actual)) if a!=b]
    if len(actual)!=len(expected):differences.append(dict(expectedRows=len(expected),actualRows=len(actual)))
    unchanged=all(identity(Path(row['path']))==row for row in inputs)
    receipt=dict(success=unchanged and not differences,cases=len(lines),fields=sum(map(len,expected)),
        inputs=inputs,inputsUnchanged=unchanged,differences=differences,sourceCopies=0,deployed=False,liveGameplay=False,
        boundary='Actual production shared Receive/Add/Frame/Spawn and actual Tick compared bitwise against matching original producer/parser/factory/Update outputs. Engine RNG/time/sprite availability and camera remain explicit fixtures.')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(receipt,indent=2)+'\n','utf8')
    print(json.dumps({k:receipt[k] for k in ('success','cases','fields','inputsUnchanged')}))
    return 0 if receipt['success'] else 1

if __name__=='__main__':raise SystemExit(main())
