#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run already frozen FX plan sequentially, only in the assigned compiler window."""
from pathlib import Path
import argparse,datetime,hashlib,json,re,subprocess

def digest(path):
    value=path.read_bytes();return len(value),hashlib.sha256(value).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prepared',type=Path,required=True)
    args=parser.parse_args();out=args.prepared.resolve()
    root=Path(__file__).resolve().parents[2]
    if not out.is_relative_to(root/'build') or (out/'receipt.json').exists():
        parser.error('fresh prepared build directory required')
    inputs=json.loads((out/'inputs.json').read_text(encoding='utf-8'))
    records=[row['frozen'] for row in inputs['files']]+inputs['system']
    known={str(Path(row['path']).resolve()).lower():row for row in records}
    def verify():
        differences=[]
        for row in records:
            path=Path(row['path'])
            if not path.is_file() or digest(path)!=(row['bytes'],row['sha256']):differences.append(str(path))
        return differences
    initial=verify()
    if initial:raise RuntimeError('input drift before execution '+str(initial))
    commands=json.loads((out/'commands.json').read_text(encoding='utf-8'))
    result={'at':datetime.datetime.now(datetime.timezone.utc).isoformat(),'inputsUnchangedBefore':True,
        'candidate':inputs['candidate'],'maxJobs':1,'results':[],'actualIncludes':[],
        'deployed':False,'liveGameplay':False,'boundary':'Actual production translation units; engine callbacks and SPR calls captured. No GL/network/gameplay. Amber is host policy, not evidenced CSO default.'}
    failure=None
    try:
        for entry in commands:
            completed=subprocess.run(entry['command'],cwd=out,capture_output=True,text=True,encoding='utf-8',errors='replace')
            log=completed.stdout+completed.stderr
            logfile=out/(entry['name']+'.log');logfile.write_text(log,encoding='utf-8')
            used=[]
            if entry['kind']=='compile':
                for value in re.findall(r'Note: including file:\s*(.+)',log):
                    path=Path(value.strip()).resolve();key=str(path).lower()
                    if key not in known:raise RuntimeError('unfrozen actual include '+str(path))
                    used.append(key)
            result['actualIncludes']+=used
            result['results'].append({'name':entry['name'],'exit':completed.returncode,
                'logSha256':digest(logfile)[1],'kind':entry['kind']})
            (out/'progress.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
            print(entry['name'],completed.returncode,flush=True)
            if completed.returncode:raise RuntimeError(entry['name']+' failed')
            if entry['kind']=='test':
                match=re.search(r'(\d+) checks passed',log)
                if not match:raise RuntimeError('missing test count')
                result['results'][-1]['checks']=int(match[1])
    except Exception as error:failure=str(error)
    finally:
        result['actualIncludes']=sorted(set(result['actualIncludes']))
        result['inputDriftAfter']=verify()
        result['inputsUnchangedAfter']=not result['inputDriftAfter']
        result['error']=failure
        result['artifacts']=[{'path':str(path),'bytes':digest(path)[0],'sha256':digest(path)[1]}
            for path in out.iterdir() if path.is_file() and path.suffix in ('.exe','.obj','.log')]
        (out/'receipt.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    if failure or result['inputDriftAfter']:raise RuntimeError(failure or 'input drift after execution')

if __name__=='__main__':main()
