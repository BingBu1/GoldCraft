#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify that scoped build auditing only accepts the exact pinned SDK archive."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();out=args.output.resolve()
    if out.exists()or not out.is_relative_to(ROOT/'build'):raise ValueError('Use a fresh build receipt directory')
    tool=ROOT/'tools/Verify-CSOLServerBuildScope.py'
    spec=importlib.util.spec_from_file_location('scope',tool);scope=importlib.util.module_from_spec(spec);spec.loader.exec_module(scope)
    archive=ROOT/'external/ReGameDLL_CS/regamedll/lib/libacof32.lib'
    inputs=[archive,tool,Path(__file__)];before={str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in inputs}
    row=scope.pinned_archive(archive)
    assert row['sha256']==before[str(archive)]and row['sourceRevision']=='4a50c42e85fd3778c2b7d24731c7d30c83bbab01'
    rejected=0
    try:scope.pinned_archive(tool)
    except ValueError:rejected+=1
    original=scope.subprocess.check_output
    try:
        scope.subprocess.check_output=lambda *a,**k:b'wrong Git archive content'
        try:scope.pinned_archive(archive)
        except ValueError:rejected+=1
    finally:scope.subprocess.check_output=original
    assert rejected==2
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==before[str(p)]for p in inputs)
    out.mkdir(parents=True);result=dict(success=True,checks=3,inputsUnchanged=True,archive=row,inputs=before,dependencyFilesModified=False)
    (out/'receipt.json').write_text(json.dumps(result,indent=2),'utf8');print(json.dumps(dict(success=True,checks=3,inputsUnchanged=True)))
if __name__=='__main__':main()
