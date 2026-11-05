# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify actual trace-DLL includes against the pinned engine build manifest."""
from pathlib import Path
import argparse, hashlib, json, re

parser = argparse.ArgumentParser()
parser.add_argument('engine', type=Path)
parser.add_argument('--baseline', type=Path, required=True)
args = parser.parse_args()
output, baseline = args.engine.resolve(), args.baseline.resolve()

def read(path):
    return json.loads(path.read_text('utf-8-sig'))

def identity(path):
    path = path.resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())

plan = read(output / 'plan.json')
source_manifest = baseline / 'source-manifest.json'
external_manifest = baseline / 'external-inputs.json'
rows = ([row['staged'] for row in read(source_manifest)['files']]
        + read(external_manifest)['files'] + plan['inputs'])
known = {str(Path(row['path']).resolve()).casefold(): row for row in rows}
log = output / '00.log'
paths = sorted(set(Path(line.strip()).resolve() for line in re.findall(
    r'Note: including file:\s*(.+)', log.read_text('utf-8', errors='replace'))))
assert paths, 'missing /showIncludes trace'
actual, unknown, changed = [], [], []
for path in paths:
    row = identity(path)
    actual.append(row)
    expected = known.get(str(path).casefold())
    if not expected:
        unknown.append(str(path))
    elif row['sha256'] != expected['sha256']:
        changed.append(str(path))
input_drift = [row['path'] for row in plan['inputs']
               if identity(Path(row['path']))['sha256'] != row['sha256']]
result = dict(success=not (unknown or changed or input_drift), includes=actual,
              unknown=unknown, changed=changed, inputDrift=input_drift,
              inputs=[identity(path) for path in
                      [source_manifest, external_manifest, output / 'plan.json', log]],
              artifacts=[identity(output / name) for name in
                         ['trace.dll', 'trace.pdb', 'engine.obj']])
(output / 'include-audit.json').write_text(json.dumps(result, indent=2) + '\n', 'utf-8')
print(json.dumps(dict(success=result['success'], includes=len(actual),
                      unknown=unknown, changed=changed, inputDrift=input_drift)))
raise SystemExit(0 if result['success'] else 1)
