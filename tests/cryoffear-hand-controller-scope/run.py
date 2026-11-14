#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify real Revolver hand authoring without inventing a cylinder pose."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import types

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cryoffear_studio import Studio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('target', 'donor', 'profile', 'output'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--legacy-target', type=Path, action='append', default=[])
    args = parser.parse_args()
    output = args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT / 'build') or any(
            p.is_symlink() or p.is_junction() for p in (output, *output.parents)):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    inputs = [Path(__file__).resolve(), ROOT / 'tools/cryoffear_studio.py',
              ROOT / 'tools/Prepare-CryOfFearHands.py', args.target.resolve(),
              args.donor.resolve(), args.profile.resolve(), *[p.resolve() for p in args.legacy_target]]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = {str(p): sha(p) for p in inputs}
    spec = importlib.util.spec_from_file_location('cof_author', inputs[2])
    author = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(author)
    checks = 0

    def check(value):
        nonlocal checks
        checks += 1
        if not value:
            raise AssertionError('Controller scope check ' + str(checks))

    def rejects(call):
        nonlocal checks
        try:
            call()
        except ValueError:
            checks += 1
            return
        raise AssertionError('Accepted unsupported controller state')

    raw = args.target.read_bytes()
    check(hashlib.sha256(raw).hexdigest() ==
          '27ea82a1bf22e9538a57b1f71180b0d58685b02ed8245420400951051af6fe1b')
    rejects(lambda: Studio(raw))
    target = Studio(raw, opaque_controllers=True)
    check(len(target.controllers) == 1 and target.controllers[0]['bone'] == 43)
    check(target.bones[43]['name'] == 'rev_cylinder')
    part = next(m for g in target.bodyparts for m in g['models'] if m['name'] == 'ref_hands')
    used = set(part['verticesBones']) | set(part['normalsBones'])
    scope = target.uncontrolled_scope(used)
    check(scope == frozenset(range(41)))
    rejects(lambda: target.sample(0, 0, 0))
    rejects(lambda: target.sample(0, 0, 0, bone_scope={43}))
    for invalid in ((), (-1,), (len(target.bones),), (True,), ('4',)):
        rejects(lambda invalid=invalid: target.uncontrolled_scope(invalid))
    for mutate in range(7):
        broken = bytearray(raw)
        if mutate == 0:
            struct.pack_into('<i', broken, 148, -1)
        elif mutate == 1:
            struct.pack_into('<i', broken, 148, 9)
        elif mutate == 2:
            struct.pack_into('<i', broken, 152, len(raw) - 1)
        elif mutate == 3:
            struct.pack_into('<i', broken, target.i(144) + 4*112 + 40, 1)
        elif mutate == 4:
            struct.pack_into('<i', broken, target.i(152), len(target.bones))
        elif mutate == 5:
            struct.pack_into('<f', broken, target.i(152) + 8, float('nan'))
        else:
            struct.pack_into('<i', broken, target.i(144) + 4*112 + 40, -2)
        rejects(lambda broken=broken: Studio(broken, opaque_controllers=True))
    # The engine consumes each bone's controller references. Controller-table
    # metadata alone must not certify a hand branch as unaffected.
    controlled = bytearray(raw)
    struct.pack_into('<i', controlled, target.i(144) + 4*112 + 40, 0)
    other = Studio(controlled, opaque_controllers=True)
    rejects(lambda: other.uncontrolled_scope(used))
    check(other.uncontrolled_scope({1}) == frozenset({0, 1}))
    child = bytearray(raw)
    struct.pack_into('<i', child, target.i(144) + 44*112 + 32, 43)
    descendant = Studio(child, opaque_controllers=True)
    rejects(lambda: descendant.uncontrolled_scope({44}))

    profile = json.loads(args.profile.read_text('utf-8-sig'))
    profile['targetSha256'] = target.sha256
    path = output / 'profile.json'
    path.write_text(json.dumps(profile), 'utf8')
    data, authored = author.prepare(args.target, args.donor, path)
    (output / 'v_cof_hands.mdl').write_bytes(data)
    result = Studio(data, opaque_controllers=True)
    rejects(lambda: Studio(data))
    check(data[244:len(raw)] == raw[244:])
    check(result.controllers == target.controllers and result.bones == target.bones)
    check(authored['opaqueControllerCount'] == 1 and authored['controllerIndependentGeometry'])
    for offset in (140, 144, 148, 152, 164, 172, 176, 212, 216):
        check(result.i(offset) == target.i(offset))
    for model in result.bodyparts[0]['models']:
        check(set(model['verticesBones']).issubset(scope))
        check(set(model['normalsBones']).issubset(scope))
    frames = 0
    for sequence in range(target.i(164)):
        at = target.i(168) + sequence*176
        for blend in range(target.i(at + 120)):
            for frame in range(target.i(at + 56)):
                check(target.channels(sequence, frame, blend) == result.channels(sequence, frame, blend))
                a = target.sample(sequence, frame, blend, bone_scope=used)
                b = result.sample(sequence, frame, blend, bone_scope=used)
                check(a == b and a[43] is None and all(a[i] is not None for i in scope))
                frames += 1
    for mesh_edit in (False, True):
        bad = copy.deepcopy(profile)
        if mesh_edit:
            bad['meshEdits'] = [dict(model=0, vertices=[dict(index=0, offset=[0, 0, 0], bone='rev_cylinder')])]
        else:
            bad['bones'][0]['target'] = 'rev_cylinder'
        path.write_text(json.dumps(bad), 'utf8')
        rejects(lambda: author.prepare(args.target, args.donor, path))
    path.write_text(json.dumps(profile), 'utf8')
    # Historical author source is interpreted in memory. No source checkout or
    # snapshot is created under build. Its no-controller outputs stay exact.
    old = types.ModuleType('previous_cof_author')
    old.__file__ = str(inputs[2])
    legacy_source = subprocess.check_output(['git', 'show', '2a44402:tools/Prepare-CryOfFearHands.py'], cwd=ROOT)
    exec(compile(legacy_source, old.__file__, 'exec'), old.__dict__)
    legacy = []
    for model in args.legacy_target:
        source = Studio.read(model)
        legacy_profile = copy.deepcopy(profile)
        legacy_profile['targetSha256'] = source.sha256
        path.write_text(json.dumps(legacy_profile), 'utf8')
        prior, _ = old.prepare(model, args.donor, path)
        current, _ = author.prepare(model, args.donor, path)
        check(prior == current)
        legacy.append(dict(targetSha256=source.sha256, outputSha256=hashlib.sha256(current).hexdigest()))
    path.write_text(json.dumps(profile), 'utf8')
    after = {str(p): sha(p) for p in inputs}
    check(before == after)
    receipt = dict(success=True, checks=checks, frames=frames, inputs=before, inputsUnchanged=True,
                   authored=authored, legacyByteCompatibility=legacy, sourceCopies=0,
                   boundary='Actual model, raw controllers, all animation channels and independent hand poses; '
                            'no cylinder-state simulation, resource deployment, visual or gameplay acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(success=True, checks=checks, frames=frames, outputSha256=authored['outputSha256'])))


if __name__ == '__main__':
    main()
