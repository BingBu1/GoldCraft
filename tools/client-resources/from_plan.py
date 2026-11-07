"""Stream a reviewed multi-root resource plan to a resources-only ZIP."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import zipfile

spec=importlib.util.spec_from_file_location('resources',Path(__file__).with_name('package.py'))
resources=importlib.util.module_from_spec(spec);spec.loader.exec_module(resources)


def build(plan_path,receipt_path,previous_sha):
    plan_path,receipt_path=map(resources.plain,(plan_path,receipt_path))
    plan_sha=resources.sha(plan_path)
    plan=json.loads(plan_path.read_text('utf-8'))
    output=resources.plain(plan['output']);partial=output.with_suffix('.zip.partial')
    if resources.sha(output)!=previous_sha or receipt_path.exists():
        raise ValueError('Existing output identity changed or receipt already exists')
    expected={}
    for row in plan['files']:
        name=row['path']
        if ':' in name or '\\' in name or any(p in ('','.','..') for p in name.split('/')) or not resources.selected(name):
            raise ValueError('Not a deliverable resource: '+name)
        key='cstrike_downloads/'+name
        if key.lower() in {n.lower() for n in expected}:raise ValueError('Case-colliding destination')
        expected[key]=row
    with zipfile.ZipFile(partial,'x',zipfile.ZIP_DEFLATED,compresslevel=6,allowZip64=True) as archive:
        for i,(name,row) in enumerate(expected.items(),1):
            path=resources.plain(row['source']);before=path.stat();identity=(row['bytes'],row['mtime_ns'],row['file_id'])
            if (before.st_size,before.st_mtime_ns,before.st_ino)!=identity:raise ValueError('Source changed')
            info=zipfile.ZipInfo.from_file(path,name);info.compress_type=zipfile.ZIP_DEFLATED;info._compresslevel=6
            digest=hashlib.sha256()
            with path.open('rb') as source,archive.open(info,'w',force_zip64=True) as target:
                while chunk:=source.read(1024*1024):digest.update(chunk);target.write(chunk)
            after=path.stat()
            if digest.hexdigest()!=row['sha256'] or (after.st_size,after.st_mtime_ns,after.st_ino)!=identity:raise ValueError('Source changed during compression')
            if i%1000==0:print(json.dumps({'phase':'compress','files':i,'total':len(expected)}),flush=True)
    with zipfile.ZipFile(partial) as archive:
        if len(archive.namelist())!=len(expected) or set(archive.namelist())!=set(expected):raise ValueError('ZIP inventory mismatch')
        for name,row in expected.items():
            with archive.open(name) as source:digest=hashlib.file_digest(source,'sha256').hexdigest()
            if digest!=row['sha256'] or archive.getinfo(name).file_size!=row['bytes']:raise ValueError('ZIP content mismatch')
    if resources.sha(plan_path)!=plan_sha or resources.sha(output)!=previous_sha:raise ValueError('Plan or output changed')
    for row in expected.values():
        current=Path(row['source']).stat()
        if (current.st_size,current.st_mtime_ns,current.st_ino)!=(row['bytes'],row['mtime_ns'],row['file_id']):raise ValueError('Source changed before publication')
    os.replace(partial,output)
    receipt=dict(archive=str(output),bytes=output.stat().st_size,sha256=resources.sha(output),
                 files=len(expected),uncompressedBytes=sum(r['bytes'] for r in expected.values()),
                 topDirectory='cstrike_downloads',allPayloadHashesVerified=True,scriptsOrProgramsIncluded=False,
                 excludedGeneratedCaches=plan.get('excludedGeneratedCaches',[]),extractedStagingBytes=0,sourceModified=False)
    receipt_path.write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n','utf-8')
    print(json.dumps(receipt,ensure_ascii=False),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan',type=Path,required=True);parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--previous-sha',required=True)
    args=parser.parse_args();build(args.plan,args.receipt,args.previous_sha)
