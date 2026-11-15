#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Pair client transport DLLs with verified precache data in a stopped sandbox B."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
from win32_processes import processes
import csol_hud_resources

ROOT = Path(__file__).resolve().parents[1]
CLIENT = ROOT / 'sandbox/cs-client-b/Half-Life'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text('utf-8-sig'))


def protected():
    root = Path('E:/Steam/steamapps/common/Half-Life')
    return {str(path): sha(path) for name in ('hl.exe', 'hw.dll', 'cstrike/cl_dlls/client.dll', 'cstrike/dlls/mp.dll')
            if (path := root / name).is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--goldcraft-build', type=Path, required=True)
    parser.add_argument('--parallel-build', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--goldcraft-receipt', type=Path, required=True)
    parser.add_argument('--parallel-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / 'build'): raise ValueError('Receipt must stay under workspace build')
    pairs = [(args.goldcraft_build / name, CLIENT / 'cstrike/metahook/plugins' / name)
             for name in ('GoldCraft.dll', 'GoldCraft.pdb')]
    pairs += [(args.parallel_build / name, CLIENT / 'cstrike/metahook/plugins' / name)
              for name in ('ParallelDownload.dll', 'ParallelDownload.pdb')]
    pairs += [(args.catalog / name, CLIENT / 'cstrike/metahook/gamedata/goldcraft-precache' / name)
              for name in ('index.json', 'goldcraft-precache-hl-10210.json')]
    for receipt in (args.goldcraft_receipt, args.parallel_receipt):
        value = load(receipt)
        if not value['success']: raise ValueError('Build receipt did not pass')
        for row in value['artifacts']:
            if sha(Path(row['path'])) != row['sha256']: raise ValueError('Built artifact changed')
    for source, target in pairs:
        if not source.resolve(strict=True).is_relative_to(ROOT / 'build') or not target.resolve().is_relative_to(CLIENT.resolve()):
            raise ValueError('Unexpected source/destination outside selected sandbox')
    hud = csol_hud_resources.verify(ROOT / 'sandbox/cs-server/Half-Life/cstrike')
    if not hud['success']: raise ValueError('Complete HUD download closure failed')
    plan = dict(files=[dict(source=str(source.resolve()), target=str(target.resolve()), sourceSha256=sha(source),
                           beforeSha256=sha(target) if target.is_file() else None) for source, target in pairs], hudPreflight=hud)
    if not args.apply:
        out.mkdir(parents=True, exist_ok=False)
        (out / 'plan.json').write_text(json.dumps(plan, indent=2), encoding='utf-8')
        print(json.dumps(dict(plannedFiles=len(pairs), hudResources=hud['resources']))); return
    if load(out / 'plan.json') != plan: raise ValueError('Deployment inputs changed since review')
    if (out / 'deployment.json').exists(): raise ValueError('Deployment already has a terminal receipt')
    live = [row for row in processes(('hl.exe', 'MetaHook.exe')) if row.get('image') and
            Path(row['image']).resolve().is_relative_to(CLIENT.resolve())]
    if live: raise ValueError('Single B is running; no files replaced')
    spec = importlib.util.spec_from_file_location('transport_atomic', ROOT / 'tools/Build-CSOLCandidate.py')
    guard = importlib.util.module_from_spec(spec); spec.loader.exec_module(guard)
    baseline = protected()
    changed = []
    try:
        for source, target in pairs:
            if target.is_file() and sha(source) == sha(target): continue
            backup = out / ('rollback-' + str(len(changed)))
            before = sha(target) if target.is_file() else None
            if before:
                shutil.copy2(target, backup)
                if sha(backup) != before: raise ValueError('Rollback copy changed')
            changed.append(dict(target=str(target), beforeSha256=before, backup=str(backup) if before else None))
            guard.atomic_copy(source, target)
            if sha(target) != sha(source): raise ValueError('Deployed digest differs')
        if protected() != baseline: raise ValueError('Protected installation baseline changed')
    except BaseException:
        for row in reversed(changed):
            if row['backup']: guard.atomic_copy(Path(row['backup']), Path(row['target']))
            elif Path(row['target']).exists(): Path(row['target']).unlink()
        raise
    result = dict(success=True, files=plan['files'], changed=changed, protectedBaseline=baseline,
                  protectedUnchanged=True, hudPreflight=hud, started=False)
    (out / 'deployment.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(dict(success=True, changedFiles=len(changed), protectedUnchanged=True)))


if __name__ == '__main__':
    main()
