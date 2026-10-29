"""Independent CSV expectations; no production parser or test executable used."""
import csv
import hashlib
import io
import json
import struct
import sys
from pathlib import Path

source, previous_plain, output = map(Path, sys.argv[1:4])
encoded = source.read_bytes()
assert encoded and len(encoded) % 8 == 0
plain = bytearray()
mask = 0xffffffff
key = (0xb38520fd, 0x5f30e75f, 0xe2eb67e0, 0xfc67a521)
for left, right in struct.iter_unpack('<II', encoded):
    total = (0x9e3779b9 * 32) & mask
    for _ in range(32):
        right = (right - (((left << 4) + key[2]) ^ (left + total) ^ ((left >> 5) + key[3]))) & mask
        left = (left - (((right << 4) + key[0]) ^ (right + total) ^ ((right >> 5) + key[1]))) & mask
        total = (total - 0x9e3779b9) & mask
    plain += struct.pack('<II', left, right)
plain = bytes(plain).rstrip(b'\0')
assert plain == previous_plain.read_bytes().rstrip(b'\0')
table = list(csv.reader(io.StringIO(plain.decode('cp949'))))
expected = []
for record, row in enumerate(table):
    if len(row) > 1 and row[1] == '9999':
        expected.append((record, int(row[0]), int(row[2]), int(row[3]), int(row[4]), int(row[5]), int(row[7])))
assert len(expected) == 20
assert {x[-1] for x in expected} == {43,193,194,195,196,197,199,200,201,202,203,204}
output.parent.mkdir(parents=True, exist_ok=True)
assert not output.exists()
output.write_text(''.join('\t'.join(map(str, row))+'\n' for row in expected), encoding='utf8')
def identity(path):
    data=path.read_bytes()
    return dict(path=str(path.resolve()),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
output.with_suffix('.json').write_text(json.dumps(dict(source=identity(source),previousDecoded=identity(previous_plain),
    fixture=identity(output),generator=identity(Path(__file__)),events=len(expected),distinctIDs=12,
    scope='independent Python TEA+csv, full decrypted bytes match previously preserved decoded resource'),indent=2,ensure_ascii=False)+'\n',encoding='utf8')
print(json.dumps(identity(output),ensure_ascii=False))
