#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute frozen tests one job at a time, reject undeclared includes, preserve failure."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import time

def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def key(p):return os.path.normcase(str(Path(p).resolve()))
def identity(p):
    p=Path(p).resolve()
    with p.open('rb') as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(p),bytes=p.stat().st_size,sha256=sha)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('candidate',type=Path);args=ap.parse_args()
    candidate=args.candidate.resolve();plan=read(candidate/'plan.json');assert plan['jobs']==1
    assert not (candidate/'result.json').exists();output=Path(plan['output'])
    inputs={key(r['path']):r for r in plan['inputs']}
    def verify():
        for row in inputs.values():assert identity(row['path'])==row,'input drift: '+row['path']
    verify();env=os.environ.copy();env.update(plan['environment']);start=time.monotonic()
    result=dict(success=False,steps=[],unknown=[],actualInputs=[],inputsUnchanged=False,plan=identity(candidate/'plan.json'));seen={}
    try:
        for step in plan['commands']:
            assert time.monotonic()-start<plan['maxSeconds']
            print('START '+step['name'],flush=True);began=time.monotonic();log=output/(step['name']+'.log')
            with log.open('xb') as stream:p=subprocess.run(step['argv'],cwd=output,env=env,stdout=stream,stderr=subprocess.STDOUT,timeout=180)
            content=log.read_text('utf-8',errors='replace')
            for match in re.finditer(r'^\s*Note: including file:\s*(.*?)\s*$',content,re.M):
                token=key(match[1])
                if token not in inputs:result['unknown'].append(match[1])
                else:seen[token]=inputs[token]
            row=dict(name=step['name'],exit=p.returncode,seconds=time.monotonic()-began,log=identity(log));result['steps'].append(row)
            if step['kind']=='test':
                summary=re.search(r'checks=(\d+) failures=(\d+)',content)
                if summary:row.update(checks=int(summary[1]),failures=int(summary[2]))
                else:raise RuntimeError('missing behavioral summary: '+step['name'])
            print(json.dumps(row),flush=True)
            if p.returncode or result['unknown']:raise RuntimeError('first failure: '+step['name'])
        before=(output/'before-observations.txt').read_bytes();after=(output/'after-observations.txt').read_bytes()
        result['observations']=[identity(output/(name+'-observations.txt')) for name in ('before','after')]
        result['observationLines']=len(before.splitlines());assert before==after,'behavioral observations differ'
        result['success']=True
    except BaseException as exc:result['error']=str(exc)
    finally:
        try:verify();result['inputsUnchanged']=True
        except BaseException as exc:result['success']=False;result['inputError']=str(exc)
        result['actualInputs']=list(seen.values());result['seconds']=time.monotonic()-start
        (candidate/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps({k:v for k,v in result.items() if k!='actualInputs'}),flush=True)
    if not result['success']:raise SystemExit(1)
if __name__=='__main__':main()
