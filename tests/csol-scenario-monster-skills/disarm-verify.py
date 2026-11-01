"""Check observed compiler includes against identities frozen before compilation."""
from pathlib import Path
import argparse
import hashlib
import json
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prepared', type=Path, required=True)
args = parser.parse_args()
out = args.prepared.resolve()
plan = json.loads((out / 'prepared.json').read_text(encoding='utf-8-sig'))
known = {str(Path(x['path']).resolve()).casefold(): x for x in
         [plan['source']] + plan['productionInputs'] + plan['systemInputs']}
seen = {}
for text in (out / 'logs/compile.log').read_text(encoding='utf-8-sig', errors='replace').splitlines():
    match = re.search(r'Note: including file:\s*(.+)$', text)
    if match:
        path = Path(match[1]).resolve()
        seen[str(path).casefold()] = path
missing, drift = [], []
for key, path in seen.items():
    if key not in known:
        missing.append(str(path))
    elif hashlib.sha256(path.read_bytes()).hexdigest() != known[key]['sha256']:
        drift.append(str(path))
result = dict(actualIncludes=len(seen), unrecorded=missing, changed=drift,
              passed=bool(seen) and not missing and not drift)
(out / 'observed-dependencies.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result))
raise SystemExit(0 if result['passed'] else 1)
