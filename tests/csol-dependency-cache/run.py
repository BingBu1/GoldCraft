# SPDX-License-Identifier: GPL-3.0-or-later
"""Run one frozen regression plan; stop at the first failure and preserve evidence."""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import time
from prepare import identity,read

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('prepared',type=Path)
    args=parser.parse_args();root=args.prepared.resolve();plan=read(root/'plan.json');out=root/'output'
    assert not out.exists();out.mkdir();(out/'temp').mkdir()
    inputs=[r['frozen'] for r in plan['inputs']]+plan['system']+plan['tools']+plan['libraries']+[plan['environment'],plan['candidate']]
    registry={os.path.normcase(r['path']):r for r in inputs}
    for row in inputs:assert identity(row['path'])==row,row['path']
    env=os.environ.copy();env.update(read(plan['environment']['path']))
    result=dict(success=False,steps=[],actualIncludes=[],unknownIncludes=[],boundary=plan['boundary'])
    try:
        for command in plan['commands']:
            start=time.monotonic();log=out/(command['name']+'.log')
            with log.open('wb') as stream:
                process=subprocess.run(command['argv'],cwd=out,env=env,stdout=stream,stderr=subprocess.STDOUT,
                    timeout=command['seconds'],creationflags=subprocess.CREATE_NO_WINDOW)
            step=dict(name=command['name'],exit=process.returncode,seconds=time.monotonic()-start,log=identity(log))
            result['steps'].append(step);print(json.dumps(step),flush=True)
            for path in set(re.findall(r'Note: including file:\s*(.+)',log.read_text(encoding='utf-8',errors='replace'))):
                row=identity(path.strip());key=os.path.normcase(row['path'])
                if key not in registry:result['unknownIncludes'].append(row)
                else:assert row==registry[key],row['path']
                result['actualIncludes'].append(row)
            assert not result['unknownIncludes'],f"{len(result['unknownIncludes'])} unregistered includes"
            assert process.returncode==0,command['name']
        result.update(success=True,executable=identity(out/'dependency_cache.exe'))
    except BaseException as error:result['error']=repr(error)
    finally:
        for row in inputs:assert identity(row['path'])==row,row['path']
        result['inputsUnchanged']=True
        (out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(success=result['success'],error=result.get('error'))))
    if not result['success']:raise SystemExit(1)

if __name__=='__main__':main()
