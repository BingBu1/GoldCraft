#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Append one explicitly calibrated attachment to a zero-attachment v10 model.

This is offline resource authoring. It does not infer an original game muzzle,
change geometry/animation, deploy assets, or establish grip/gameplay acceptance.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import tempfile

from cryoffear_studio import Studio, inverse, transform

ROOT = Path(__file__).resolve().parent.parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def ordinary(path):
    path = Path(path).absolute()
    if any(p.is_symlink() or p.is_junction() for p in (path, *path.parents)):
        raise ValueError('Linked input/output path refused')
    return path.resolve()


def author(data, profile):
    if type(profile.get('schema')) is not int or profile['schema'] != 1 or profile.get('status') != 'candidate' or sha(data) != profile.get('modelSha256'):
        raise ValueError('Explicit candidate schema and matching source identity required')
    source = Studio(data)
    if source.attachments:
        raise ValueError('This author only accepts a model with no attachments')
    for key in ('bodypart', 'model', 'sequence', 'frame', 'blend', 'type'):
        if type(profile.get(key)) is not int or profile[key] < 0:
            raise ValueError('Explicit nonnegative integer required: ' + key)
    if profile['type'] > 0x7fffffff:
        raise ValueError('Attachment type exceeds studio integer width')
    name = profile.get('name')
    if not isinstance(name, str) or not name or len(name.encode('ascii')) >= 32 or '\0' in name:
        raise ValueError('Explicit bounded ASCII attachment name required')
    bodypart, model = profile['bodypart'], profile['model']
    if bodypart >= len(source.bodyparts) or model >= len(source.bodyparts[bodypart]['models']):
        raise ValueError('Selected geometry model is absent')
    geometry = source.bodyparts[bodypart]['models'][model]
    vertices = profile.get('landmarkVertices')
    if not isinstance(vertices, list) or not 3 <= len(vertices) <= 64 or any(
            type(v) is not int or not 0 <= v < len(geometry['vertices']) for v in vertices) or len(set(vertices)) != len(vertices):
        raise ValueError('Explicit unique geometry landmarks required')
    vectors = profile.get('vectors')
    if not isinstance(vectors, list) or len(vectors) != 3 or any(
            not isinstance(row, list) or len(row) != 3 or any(
                type(v) not in (float, int) or not math.isfinite(v) for v in row) for row in vectors):
        raise ValueError('Explicit finite attachment vectors required')
    bone = source.bone_index(profile.get('bone'))
    poses = source.sample(profile['sequence'], profile['frame'], profile['blend'])
    points = [transform(poses[geometry['verticesBones'][v]], geometry['vertices'][v]) for v in vertices]
    center = tuple(sum(p[axis] for p in points) / len(points) for axis in range(3))
    origin = transform(inverse(poses[bone]), center)
    attachment = struct.pack('<32sii12f', name.encode('ascii'), profile['type'], bone,
                             *origin, *(v for row in vectors for v in row))
    assert len(attachment) == 88
    output = bytearray(data)
    at = len(output)
    output.extend(attachment)
    if len(output) > 0x7fffffff:
        raise ValueError('Authored model exceeds studio integer width')
    struct.pack_into('<i', output, 72, len(output))
    struct.pack_into('<ii', output, 212, 1, at)
    # The three header words and appended table are the entire permitted delta.
    restored = bytearray(output[:len(data)])
    restored[72:76] = data[72:76]
    restored[212:220] = data[212:220]
    if restored != data:
        raise ValueError('Original payload changed outside attachment metadata')
    checked = Studio(output)
    if len(checked.attachments) != 1 or checked.attachments[0]['bone'] != bone:
        raise ValueError('Authored attachment failed studio validation')
    actual = transform(poses[bone], checked.attachments[0]['origin'])
    error = math.dist(actual, center)
    return bytes(output), dict(bone=profile['bone'], localOrigin=checked.attachments[0]['origin'],
                              modelSpaceCenter=center, floatEncodingError=error,
                              geometryAndAnimationBytesPreserved=True, originalAttachments=0,
                              authoredAttachments=1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'profile', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    source, profile_path, output = map(ordinary, (args.source, args.profile, args.output))
    if output.exists() or output == ROOT / 'build' or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build output directory')
    profile_data, data = profile_path.read_bytes(), source.read_bytes()
    profile = json.loads(profile_data)
    evidence = profile.get('evidence', {})
    proof = ordinary(ROOT / evidence.get('path', ''))
    if not proof.is_file() or not proof.is_relative_to(ROOT) or sha(proof.read_bytes()) != evidence.get('sha256'):
        raise ValueError('Matching workspace calibration evidence required')
    name = profile.get('outputName', '')
    if not name or Path(name).name != name or not name.endswith('.mdl'):
        raise ValueError('Explicit model output basename required')
    authored, details = author(data, profile)
    helper = Path(__file__).with_name('cryoffear_studio.py')
    helper_hash = sha(helper.read_bytes())
    receipt = dict(success=True, candidateOnly=True, source=dict(path=str(source), bytes=len(data), sha256=sha(data)),
                   profile=dict(path=str(profile_path), sha256=sha(profile_data)),
                   artifact=dict(path=str(output / name), bytes=len(authored), sha256=sha(authored)),
                   toolSha256=sha(Path(__file__).read_bytes()), details=details,
                   studioHelperSha256=helper_hash,
                   gripAccepted=False, deployed=False, gameplay=False, sourceCopies=0)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=output.name + '-preparing-', dir=output.parent))
    try:
        (stage / name).write_bytes(authored)
        (stage / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
        if source.read_bytes() != data or profile_path.read_bytes() != profile_data or sha(helper.read_bytes()) != helper_hash or sha(proof.read_bytes()) != evidence['sha256']:
            raise ValueError('Source/profile changed during resource authoring')
        os.rename(stage, output)
    except BaseException:
        shutil.rmtree(stage)
        raise
    print(json.dumps(dict(success=True, artifact=receipt['artifact'], details=details, candidateOnly=True)))


if __name__ == '__main__':
    main()
