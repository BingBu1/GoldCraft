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
# Byte-exact preservation outside weapon IDs, clips and ammo counts. Stock
# ammo slots and the active-weapon reserve both feed client prediction.
expected = re.sub(rb'(DEFINE_DELTA\(\s*m_iId\s*,\s*DT_INTEGER\s*,\s*)5', rb'\g<1>16', source)
expected = re.sub(rb'(DEFINE_DELTA\(\s*m_iClip\s*,\s*DT_SIGNED\s*\|\s*DT_INTEGER\s*,\s*)10', rb'\g<1>16', expected)
expected = re.sub(rb'(DEFINE_DELTA\(\s*ammo_(?:nails|shells|cells|rockets)\s*,\s*DT_SIGNED\s*\|\s*DT_INTEGER\s*,\s*)10', rb'\g<1>16', expected)
expected = re.sub(rb'(DEFINE_DELTA\(\s*(?:vuser[23]\[[012]\]|vuser4\[1\])\s*,\s*DT_FLOAT\s*,\s*)9', rb'\g<1>15', expected)
assert result == expected
# Already wider private schemas must not be narrowed.
wider = result.replace(b'vuser4[1], DT_FLOAT, 15', b'vuser4[1], DT_FLOAT, 20')
assert delta.expand(wider) == wider
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
for name in (b'ammo_nails', b'ammo_shells', b'ammo_cells', b'ammo_rockets',
             b'vuser2[0]', b'vuser2[1]', b'vuser2[2]', b'vuser3[0]',
             b'vuser3[1]', b'vuser3[2]', b'vuser4[1]'):
    row = re.search(rb'DEFINE_DELTA\(\s*' + re.escape(name) + rb'[^\r\n]+', source)[0]
    for invalid in (source.replace(name, b'absent_' + name),
                    source.replace(row, b'// ' + row),
                    source.replace(row, b'/* ' + row + b' */')):
        try:
            delta.expand(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Accepted missing/commented prediction ammo field: ' + name.decode())
    commented = b'/* ' + row + b' */\n'
    assert delta.expand(commented + source) == commented + expected
print('PASS delta: exact 14-field expansion, idempotence, wider schemas and 38 invalid baselines')
