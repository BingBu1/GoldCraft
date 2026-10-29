"""Prepare independent resource expectations; never imports production code.

The 15-column schema/defaults and last-wins key semantics come from the saved
original client loader/getter/map disassembly. Python CSV and decimal parsers
are independent of C++ ReadCsv/from_chars. This preparation does not execute
the production parser or claim live engine/client behavior.
"""
import argparse
import csv
import hashlib
import io
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MASK = 0xffffffff
KEY = (0xb38520fd, 0x5f30e75f, 0xe2eb67e0, 0xfc67a521)
DELTA = 0x9e3779b9


def identity(path):
    value = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(value), sha256=hashlib.sha256(value).hexdigest())


def decode(data):
    if not data or len(data) % 8:
        raise ValueError('unaligned resource')
    result = bytearray()
    for a, b in struct.iter_unpack('<II', data):
        total = DELTA * 32 & MASK
        for _ in range(32):
            b = (b - (((a << 4) + KEY[2]) ^ (a + total) ^ ((a >> 5) + KEY[3]))) & MASK
            a = (a - (((b << 4) + KEY[0]) ^ (b + total) ^ ((b >> 5) + KEY[1]))) & MASK
            total = (total - DELTA) & MASK
        result.extend(struct.pack('<II', a, b))
    return bytes(result).rstrip(b'\0')


def encode(data):
    data += b'\0' * ((-len(data)) % 8)
    if not data:
        data = b'\0' * 8
    result = bytearray()
    for a, b in struct.iter_unpack('<II', data):
        total = 0
        for _ in range(32):
            total = (total + DELTA) & MASK
            a = (a + (((b << 4) + KEY[0]) ^ (b + total) ^ ((b >> 5) + KEY[1]))) & MASK
            b = (b + (((a << 4) + KEY[2]) ^ (a + total) ^ ((a >> 5) + KEY[3]))) & MASK
        result.extend(struct.pack('<II', a, b))
    return bytes(result)


def integer(text, default=0):
    if text == '':
        return default
    prefix = re.match(r'[ \t\r\n\v\f]*([+-]?[0-9]+)', text)
    result = int(prefix[1]) if prefix else 0
    if not -2147483648 <= result <= 2147483647:
        raise ValueError('int32 outside established contract')
    return result


def float_bits(text, default):
    if text == '':
        number = default
    else:
        prefix = re.match(r'[ \t\r\n\v\f]*([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)', text)
        number = float(prefix[1]) if prefix else 0.0
    return struct.unpack('<I', struct.pack('<f', number))[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    ref = ROOT/'analysis/csol-model-20261017/scenario/event-dispatch/message-resources.json'
    original = json.loads(ref.read_text(encoding='utf8'))
    source = identity(args.source)
    assert source['sha256'] == original['source']['sha256'], 'common source advanced from original evidence'
    plain = decode(args.source.read_bytes())
    assert hashlib.sha256(plain).hexdigest() == original['decodedSha256']
    # latin1 maps each byte to one code point; neither Korean resource bytes
    # nor whitespace are transcoded. csv.reader alone handles field quoting.
    text = plain.decode('latin1')
    offset = 3 if plain.startswith(b'\xef\xbb\xbf') else 0
    text = text[offset:]
    physical_lines = [m[0] for m in re.finditer(r'.*?(?:\r\n|\r|\n|$)', text, re.DOTALL) if m[0]]
    offsets = [offset]
    for line in physical_lines:
        offsets.append(offsets[-1] + len(line))
    reader = csv.reader(io.StringIO(text, newline=''), strict=True)
    previous_line = 0
    records = []
    for cells in reader:
        records.append((offsets[previous_line], cells or ['']))
        previous_line = reader.line_num
    assert len(records)-1 == 4409
    rows, index = [], {}
    for record, (_, cells) in enumerate(records[1:], 1):
        if not any(cells):
            continue
        if not cells[0]:
            raise ValueError('empty ID outside original contract')
        fields = cells + [''] * max(0, 15-len(cells))
        key = integer(fields[0]) & 0xffff
        row = dict(record=record, id=key, strings=fields[1:4],
            color=[integer(fields[i], default) & 255 for i, default in zip((4, 5, 6), (255, 180, 30))],
            durations=[float_bits(fields[i], default) for i, default in zip((7, 8, 9), (.5, 3., .5))],
            integers=[integer(fields[10], 0), integer(fields[11], -1)],
            optional=[float_bits(fields[i], -1.) for i in (12, 13, 14)])
        index[key] = len(rows)
        rows.append(row)
    output = bytearray()
    def u32(value):
        output.extend(struct.pack('<I', value & MASK))
    def string(value):
        data = value.encode('latin1')
        u32(len(data)); output.extend(data)
    for value in (0x3147534d, len(records), len(rows), len(index)):
        u32(value)
    for at, cells in records:
        u32(at); u32(len(cells))
        for cell in cells:
            string(cell)
    for row in rows:
        u32(row['record']); u32(row['id'])
        for value in row['strings']:
            string(value)
        for name in ('color', 'durations', 'integers', 'optional'):
            for value in row[name]:
                u32(value)
    for key, value in sorted(index.items()):
        u32(key); u32(value)
    expected_path = args.output/'messages-expected.bin'
    plain_path = args.output/'ZBS_message.decoded.csv'
    expected_path.write_bytes(output); plain_path.write_bytes(plain)
    fixtures = dict(valid=b'id,text\n43,#TEST\n', replacement=b'id,text\n99,replacement\n',
        empty=b'', embeddedNul=b'id,text\n43,a\0b\n', invalidField=b'id,text\n2147483648,bad\n',
        invalidCsv=b'id,text\n43,"unterminated')
    header = '// SPDX-License-Identifier: GPL-3.0-or-later\n// Independently encoded synthetic tables; see prepare_expected.py.\n#pragma once\n#include <string_view>\nnamespace fixtures {\n'
    for name, value in fixtures.items():
        header += f'inline constexpr std::string_view {name} = "{encode(value).hex()}";\n'
    fixture_path = Path(__file__).with_name('fixtures.hpp')
    fixture_path.write_text(header + '}\n', encoding='utf8')
    refs = [ref, ROOT/'analysis/csol-model-20261017/scenario/event-dispatch/client-message-table-loader.asm',
        ROOT/'analysis/csol-model-20261017/scenario/event-dispatch/client-table-integer.asm',
        ROOT/'analysis/csol-model-20261017/scenario/event-dispatch/client-table-float.asm',
        ROOT/'analysis/csol-model-20261017/scenario/runtime-followup/message-map-get-or-insert.asm']
    report = dict(scope='Independent expectation preparation only; no C++ parser, original binary, engine or client execution',
        source=source, records=len(records), rows=len(rows), keys=len(index), low16Duplicates=len(rows)-len(index),
        fields=sum(len(cells) for _,cells in records), schemaColumns=15, encoding='raw bytes via latin1 roundtrip',
        decoded=identity(plain_path), expected=identity(expected_path), synthetic=identity(fixture_path),
        originalReferences=[identity(p) for p in refs], generator=identity(Path(__file__)))
    (args.output/'expected-manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps({key:report[key] for key in ('records','rows','keys','low16Duplicates','fields')},ensure_ascii=False))


if __name__ == '__main__':
    main()
