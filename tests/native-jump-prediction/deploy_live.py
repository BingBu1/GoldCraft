#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply the reviewed single-B DLL/catalog plan after live server HUD preflight."""
import argparse
import hashlib
import json
from pathlib import Path
import runpy
import sys

ROOT = Path(__file__).resolve().parents[2]
GAME = ROOT / 'sandbox/cs-client-b/Half-Life'
sys.path.insert(0, str(ROOT / 'tools'))
from win32_processes import processes
import csol_hud_resources


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--ready', type=Path, required=True)
    args = parser.parse_args()
    deploy = runpy.run_path(str(ROOT / 'tools/Deploy-SandboxEngine.py'))
    safe, copy = deploy['safe'], deploy['atomic_copy']
    plan_path, ready_path = safe(args.plan), safe(args.ready)
    if not plan_path.is_relative_to(ROOT / 'build') or not ready_path.is_relative_to(ROOT / 'build'):
        raise ValueError('Use bounded workspace receipts')
    plan = json.loads(plan_path.read_text('utf-8'))
    ready = json.loads(ready_path.read_text('utf-8'))
    live = processes(('hlds.exe', 'MetaHook.exe', 'hl.exe'))
    if any(row.get('imageError') for row in live):
        raise ValueError('A game process identity is inaccessible')
    if any(row.get('image') and Path(row['image']).resolve().parent == GAME for row in live):
        raise ValueError('B must be stopped before replacing DLLs')
    if not any(row['pid'] == ready['pid'] and row.get('image') and
               Path(row['image']).resolve() == Path(ready['game']) / 'hlds.exe' for row in live):
        raise ValueError('The preflight server is no longer running')
    hud = csol_hud_resources.verify(safe(Path(ready['game'])) / 'cstrike')
    if not hud['success'] or hud['catalogSha256'] != ready['hud']['catalogSha256']:
        raise ValueError('The authoritative HUD closure changed')
    allowed = {str(GAME / ('cstrike/metahook/plugins/' + name)) for name in
               ('GoldCraft.dll', 'GoldCraft.pdb', 'ParallelDownload.dll', 'ParallelDownload.pdb',
                'CSOLSupport.dll', 'CSOLSupport.pdb')}
    allowed.update(str(GAME / 'cstrike/metahook/gamedata/goldcraft-precache' / name)
                   for name in ('index.json', 'goldcraft-precache-hl-10210.json'))
    files = plan['files']
    if len(files) != 8 or {str(safe(Path(row['destination']))) for row in files} != allowed:
        raise ValueError('The eight-file scope changed')
    for row in files:
        source, target = safe(Path(row['source'])), safe(Path(row['destination']))
        if digest(source) != row['sourceSHA256'] or digest(target) != row['beforeSHA256']:
            raise ValueError('A planned source or destination changed: ' + target.name)
    backup = plan_path.parent / 'client-before'
    backup.mkdir()
    for index, row in enumerate(files):
        copy(Path(row['destination']), backup / str(index))
    applied = []
    try:
        for row in files:
            copy(Path(row['source']), Path(row['destination']))
            applied.append(row)
        result = dict(success=True, files=files, hud=hud, serverPid=ready['pid'],
                      backup=str(backup), clientStarted=False, protectedInstallationWritten=False)
        (plan_path.parent / 'deployment.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')
    except BaseException:
        for index, row in enumerate(files):
            copy(backup / str(index), Path(row['destination']))
        raise
    print(json.dumps(dict(success=True, files=len(applied), hudResources=hud['resources'], clientStarted=False)))


if __name__ == '__main__':
    main()
