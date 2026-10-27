"""Check the actual CS schema and reject the Half-Life fallback that lost shooting permission."""
import importlib.util
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('delta', ROOT / 'tools/Prepare-CSOLWeaponDelta.py')
delta = importlib.util.module_from_spec(spec)
spec.loader.exec_module(delta)
source = (ROOT / 'external/ReGameDLL_CS/dist/delta.lst').read_bytes()
result = delta.expand(source)
assert delta.expand(result) == result
# Byte-exact preservation of every field except the three intended widths.
expected = re.sub(rb'(DEFINE_DELTA\(\s*m_iId\s*,\s*DT_INTEGER\s*,\s*)5', rb'\g<1>16', source)
expected = re.sub(rb'(DEFINE_DELTA\(\s*m_iClip\s*,\s*DT_SIGNED\s*\|\s*DT_INTEGER\s*,\s*)10', rb'\g<1>16', expected)
assert result == expected
for invalid in (
    (ROOT / 'sandbox/csol-runtime-test/Server/valve/delta.lst').read_bytes(),
    source.replace(b'iuser3, DT_INTEGER, 8', b'iuser3, DT_INTEGER, 3'),
    source.replace(b'iuser3, DT_INTEGER, 8', b'absent, DT_INTEGER, 8'),
    source.replace(b'DEFINE_DELTA( iuser3, DT_INTEGER, 8', b'// DEFINE_DELTA( iuser3, DT_INTEGER, 8'),
    source.replace(b'iuser3, DT_INTEGER, 8', b'iuser3, DT_SIGNED | DT_INTEGER, 8'),
):
    try:
        delta.expand(invalid)
    except ValueError as error:
        assert 'Counter-Strike' in str(error)
    else:
        raise AssertionError('Accepted missing/truncated permission flags')
print('PASS delta: CS baseline, exact three-field expansion, idempotence, five invalid baselines rejected')
