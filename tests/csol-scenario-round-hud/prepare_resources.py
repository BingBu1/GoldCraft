"""Prepare independent expectations from unpacked common resources; never compile.

Uses the existing independent TEA reference plus Python CSV, and matches only
the selected, single-line localization entries. Production parsers are exercised
later by main.cpp. No game resource is copied into the source tree.
"""
import argparse
import csv
import hashlib
import importlib.util
import io
import json
import re
import struct
import sys
from pathlib import Path


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def prepare(common, output):
    if output.exists():
        raise ValueError('Use a new output directory; preserve earlier evidence.')
    reference = Path(__file__).resolve().parents[1] / 'csol-scenario-message-table/prepare_expected.py'
    spec = importlib.util.spec_from_file_location('independent_message_reference', reference)
    module = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True
    spec.loader.exec_module(module)
    message_path = common / 'maps/ZBS_message.cso'
    wave_path = common / 'maps/zbs_wave_zs_deadend.cso'
    localization_path = common / 'resource/cso_chn.txt'
    rows = list(csv.reader(io.StringIO(module.decode(message_path.read_bytes()).decode('cp949'))))
    waves = list(csv.reader(io.StringIO(module.decode(wave_path.read_bytes()).decode('cp949'))))
    by_id = {int(row[0]) & 65535: row for row in rows[1:] if row and row[0]}
    events = [(index, int(row[7]) & 65535) for index, row in enumerate(waves)
              if len(row) > 7 and row[1] == '9999']
    ids = sorted({number for _, number in events})
    assert len(events) == 20 and ids == [43, 193, 194, 195, 196, 197, 199, 200, 201, 202, 203, 204]
    selected = sorted(set(ids + [13, 179, 181, 867]))
    localized = localization_path.read_text('utf-16')
    output.mkdir(parents=True)
    blob = bytearray()

    def word(value):
        blob.extend(struct.pack('<I', value))

    def string(value):
        raw = value.encode('utf-16le')
        word(len(raw))
        blob.extend(raw)

    word(0x31474c44)  # DLG1
    word(len(events))
    for record, number in events:
        word(record)
        word(number)
    word(len(selected))
    selected_report = []
    assets = []
    for number in selected:
        row = by_id[number]
        assert len(row) == 15
        key = row[1][1:] if row[1].startswith('#') else ''
        values = re.findall(r'^\s*"' + re.escape(key) + r'"\s*"(.*)"\s*$', localized, re.M) if key else []
        if key:
            assert len(values) == 1, (key, len(values))
            # Selected actual strings have no escape syntax. Do not grow a
            # second localization parser that could duplicate production bugs.
            assert '\\' not in values[0] and '"' not in values[0]
        value = values[0] if values else ''
        image = row[2].replace('\\', '/') + '.tga' if row[2] else ''
        sound = row[3].replace('\\', '/')
        word(number)
        for field in (row[1], key, value, image, sound):
            string(field)
        word(int(row[10] or '0'))
        selected_report.append(dict(id=number, key=key, image=image, sound=sound))
        for relative in ([image] if image else []) + (['sound/' + sound] if sound else []):
            asset = common / relative
            assert asset.is_file(), relative
            assets.append(identity(asset))
    expected = output / 'expected.bin'
    expected.write_bytes(blob)
    receipt = dict(scope='expectation preparation only; production C++ not compiled or run',
                   source=[identity(path) for path in (message_path, wave_path, localization_path)],
                   tools=[identity(Path(__file__)), identity(reference)],
                   expected=identity(expected), events=len(events), selected=selected_report,
                   selectedAssets=list({row['path']: row for row in assets}.values()))
    (output / 'resources.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2) + '\n', 'utf-8')
    print(json.dumps(dict(events=len(events), messages=len(selected), expected=identity(expected))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.common, args.output)
