# SPDX-License-Identifier: GPL-3.0-or-later
"""Check real model payload preservation and reject invalid attachment profiles."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'profile', 'candidate', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or output == ROOT / 'build' or not output.resolve().is_relative_to(ROOT / 'build') or any(
            p.is_symlink() or p.is_junction() for p in (output, *output.parents)):
        raise ValueError('Use a fresh workspace build output')
    spec = importlib.util.spec_from_file_location('attachment_author', ROOT / 'tools/Author-MissingStudioAttachment.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = args.source.read_bytes()
    profile_data = args.profile.read_bytes()
    profile = json.loads(profile_data)
    candidate = args.candidate.read_bytes()
    checks = []

    def check(value, name):
        checks.append(dict(name=name, passed=bool(value)))

    authored, details = module.author(original, profile)
    check(authored == candidate, 'actual author reproduces complete candidate bytes')
    check(len(authored) == len(original) + 88, 'only one native attachment record appended')
    check(struct.unpack_from('<ii', authored, 212) == (1, len(original)), 'real native attachment count and offset')
    check(struct.unpack_from('<i', authored, 72)[0] == len(authored), 'actual model length includes appended record')
    restored = bytearray(authored[:len(original)])
    restored[72:76] = original[72:76]
    restored[212:220] = original[212:220]
    check(restored == original, 'all geometry animation materials bones and source payload preserved')
    check(details['floatEncodingError'] < 1e-4, 'float32 native record retains geometric center')
    from cryoffear_studio import Studio
    native = Studio(candidate)
    check(len(native.attachments) == 1 and native.bones[native.attachments[0]['bone']]['name'] == profile['bone'],
          'real studio reader resolves authored attachment and bone')
    invalid = [('schema', True), ('status', 'verified'), ('modelSha256', '0' * 64),
               ('landmarkVertices', [0, 0, 1]), ('landmarkVertices', [-1, 0, 1]),
               ('landmarkVertices', [0, 1, 0x7fffffff]), ('landmarkVertices', [0, 1]),
               ('bodypart', 0x7fffffff), ('model', 0x7fffffff), ('bone', 'missing-bone'),
               ('name', ''), ('name', 'x' * 32), ('type', True), ('vectors', [[float('inf')] * 3] * 3)]
    for key, value in invalid:
        changed = copy.deepcopy(profile)
        changed[key] = value
        try:
            module.author(original, changed)
            check(False, 'reject invalid ' + key)
        except ValueError:
            check(True, 'reject invalid ' + key)
    changed = copy.deepcopy(profile)
    changed['modelSha256'] = sha(candidate)
    try:
        module.author(candidate, changed)
        check(False, 'existing attachment is never silently replaced')
    except ValueError:
        check(True, 'existing attachment is never silently replaced')
    check(args.source.read_bytes() == original and args.profile.read_bytes() == profile_data,
          'original model and explicit profile remain unchanged')
    receipt = dict(success=all(row['passed'] for row in checks), checks=checks,
                   sourceSha256=sha(original), candidateSha256=sha(candidate),
                   toolSha256=sha((ROOT / 'tools/Author-MissingStudioAttachment.py').read_bytes()),
                   studioHelperSha256=sha((ROOT / 'tools/cryoffear_studio.py').read_bytes()),
                   candidateOnly=True, deployed=False, gripAccepted=False, gameplay=False)
    output.mkdir(parents=True)
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(success=receipt['success'], checks=len(checks), failed=[r['name'] for r in checks if not r['passed']])))
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
