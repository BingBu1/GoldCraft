"""Run a frozen single-job component plan and retain the first failure."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
from prepare import identity, read


def verify(row):
    assert identity(row['path']) == row, row['path']


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('prepared',type=Path)
    args=parser.parse_args()
    root=args.prepared.resolve();plan=read(root/'plan.json')
    sources=[x['frozen'] for x in plan['inputs']]+plan['tools']+plan['resources']+[plan['environment']]
    for row in sources:verify(row)
    out=root/'output';out.mkdir()
    env=os.environ.copy();env.update(read(plan['environment']['path']))
    result=dict(success=False,error=None,steps=[],actualIncludes=[],deployed=False)
    try:
        for command in plan['commands']:
            start=time.monotonic();log=out/(command['name']+'.log')
            with log.open('wb') as stream:
                process=subprocess.run(command['argv'],cwd=out,env=env,stdout=stream,stderr=subprocess.STDOUT,
                    timeout=command['seconds'],creationflags=subprocess.CREATE_NO_WINDOW)
            result['steps'].append(dict(name=command['name'],exit=process.returncode,
                seconds=time.monotonic()-start,log=identity(log)))
            print(json.dumps(result['steps'][-1]),flush=True)
            assert process.returncode==0,command['name']
            for path in set(re.findall(r'Note: including file:\s*(.+)',log.read_text(encoding='utf-8',errors='replace'))):
                result['actualIncludes'].append(identity(path.strip()))
        if plan.get('suite','large') == 'large':
            body=identity(out/'run/font-http-body.bin')
            assert body['sha256']==plan['resources'][0]['sha256'] and body['bytes']==22309736
            result.update(body=body,header=identity(out/'run/font-http-header.txt'))
        result.update(success=True,executable=identity(out/'large_http.exe'),
            testOutput=(out/'test.log').read_text(encoding='utf-8'))
    except BaseException as error:
        result['error']=repr(error)
    finally:
        for row in sources:verify(row)
        result['inputsUnchanged']=True
        (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(success=result['success'],error=result['error'])))
    if not result['success']:raise SystemExit(1)


if __name__=='__main__':main()
