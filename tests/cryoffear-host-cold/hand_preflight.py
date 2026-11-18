# SPDX-License-Identifier: GPL-3.0-or-later
"""Check complete and donor-omitted variants of a real resource manifest."""
import argparse
import importlib.util
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--resources', type=Path, required=True)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('cof_cold', Path(__file__).with_name('run.py'))
cold = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cold)
rows = json.loads(args.resources.read_text('utf8'))['files']
cold.verify_hand_donors(rows)
donors = {json.loads(Path(row['source']).read_text('utf-8-sig')).get(
    'donor', 'models/csol/hands/v_hegrenade.mdl') for row in rows
    if row['destination'].endswith('_hands.json')}
assert donors, 'Use a real manifest containing hand profiles'
for donor in donors:
    incomplete = [row for row in rows if row['destination'] != donor]
    try:
        cold.verify_hand_donors(incomplete)
    except ValueError as error:
        assert str(error).startswith('Missing declared hand donor: ' + donor)
    else:
        raise AssertionError('Incomplete resource manifest was accepted')
print(json.dumps(dict(success=True, resources=len(rows), donorOmissions=len(donors), runtimeWrites=0)))
