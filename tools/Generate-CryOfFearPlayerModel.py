#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake selected CoF V geometry into a static P using explicit calibration.

No angle, scale, grip, material, animation or attachment fallback is supplied.
Unreviewed calibrations require --candidate and produce *.candidate.mdl only.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import tempfile

from cryoffear_studio import Studio, inverse, multiply, transform

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def ordinary(path):
    path = Path(path).absolute()
    for item in (path, *path.parents):
        if item.exists() or item.is_symlink():
            if item.is_symlink() or getattr(item.lstat(), 'st_file_attributes', 0) & 0x400:
                raise ValueError('Reparse points are not permitted')
    return path.resolve()


def rigid_matrix(value):
    if not isinstance(value, list) or len(value) != 3 or any(not isinstance(row, list) or len(row) != 4 for row in value):
        raise ValueError('Explicit 3x4 source-anchor to target-bone matrix is required')
    if any(isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x) for row in value for x in row):
        raise ValueError('Calibration matrix must be finite')
    rotation = [row[:3] for row in value]
    for i in range(3):
        for j in range(3):
            if abs(sum(rotation[i][k]*rotation[j][k] for k in range(3)) - (1 if i == j else 0)) > 1e-5:
                raise ValueError('Calibration must be rigid; scale/shear is not inferred or accepted')
    a, b, c = rotation
    determinant = a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0])
    if abs(determinant - 1) > 1e-5:
        raise ValueError('Mirroring requires separate authored geometry, not a reflected calibration')
    return value


def sample(model, description):
    if description.get('sha256') != model.sha256:
        raise ValueError('Model SHA-256 differs from calibration')
    sequence = description['sequence']
    index, frame, blend = sequence['index'], sequence['frame'], sequence['blend']
    if any(type(x) is not int for x in (index, frame, blend)) or not 0 <= index < model.i(164):
        raise ValueError('An explicit integer sequence/frame/blend is required')
    if model.string(model.i(168) + 176*index, 32) != sequence['label']:
        raise ValueError('Sequence index and label differ')
    return model.sample(index, frame, blend)


class Writer:
    def __init__(self):
        self.data = bytearray(244)
        self.data[:4] = b'IDST'
        self.put(4, 'i', 10)

    def put(self, offset, fmt, *values):
        struct.pack_into('<' + fmt, self.data, offset, *values)

    def append(self, data, alignment=4):
        while len(self.data) % alignment:
            self.data.append(0)
        at = len(self.data)
        self.data += data
        return at

    def reserve(self, count):
        return self.append(bytes(count))

    def name(self, at, size, value):
        raw = value.encode('latin1')
        if len(raw) >= size:
            raise ValueError('Studio name is too long')
        self.data[at:at + size] = raw.ljust(size, bytes([0]))


def triangle_commands(triangles):
    """Keep compatible adjacent strip/fan triangles in one native command."""
    commands, index = bytearray(), 0
    while index < len(triangles):
        primitive = list(triangles[index])
        index += 1
        mode = None
        while index < len(triangles) and len(primitive) < 32767:
            triangle = triangles[index]
            strip_prefix = ((primitive[-1], primitive[-2]) if (len(primitive) - 2) % 2
                            else (primitive[-2], primitive[-1]))
            fan_prefix = (primitive[0], primitive[-1])
            if mode in (None, 'strip') and triangle[:2] == strip_prefix:
                mode = 'strip'
            elif mode in (None, 'fan') and triangle[:2] == fan_prefix:
                mode = 'fan'
            else:
                break
            primitive.append(triangle[2])
            index += 1
        commands += struct.pack('<h', len(primitive) if mode == 'strip' else -len(primitive))
        for corner in primitive:
            commands += struct.pack('<4h', *corner)
    return commands + bytes(2)


def generate(source, template, calibration, *, candidate=False):
    if type(calibration.get('schema')) is not int or calibration['schema'] != 1 or calibration.get('attachments') != 'all':
        raise ValueError('Schema 1 and explicit preservation of all attachments are required')
    status = calibration.get('status')
    if status not in ('candidate', 'verified') or (status != 'verified' and not candidate):
        raise ValueError('Unverified calibration can only generate a candidate')
    if not calibration.get('evidence', {}).get('sha256') or not calibration['evidence'].get('path'):
        raise ValueError('A hash-pinned calibration evidence reference is required')
    proof = ordinary(calibration['evidence']['path'])
    if not proof.is_file() or digest(proof.read_bytes()) != calibration['evidence']['sha256']:
        raise ValueError('Calibration evidence identity differs')
    source_poses = sample(source, calibration['source'])
    template_poses = sample(template, calibration['target'])
    source_anchor = source.bone_index(calibration['source']['bone'])
    target = template.bone_index(calibration['target']['bone'])
    hand = template.bone_index(calibration['target']['handBone'])
    if calibration['target']['handBone'] != 'Bip01 R Hand' or calibration['target']['bone'] != 'SHOTGUN' or template.bones[target]['parent'] != hand:
        raise ValueError('Target must be a direct child of the supported original hand bone')
    matrix = rigid_matrix(calibration['sourceAnchorToTargetBone'])
    selected_bones = calibration.get('sourceVertexBones')
    if not isinstance(selected_bones, list) or not selected_bones or len(set(selected_bones)) != len(selected_bones):
        raise ValueError('Explicit unique source vertex-bone allowlist is required')
    allowed_bones = {source.bone_index(name) for name in selected_bones}
    allow = calibration.get('textures')
    if not isinstance(allow, list) or not allow:
        raise ValueError('A hash-pinned texture allowlist is required')
    wanted = {(row['name'], row['sha256']) for row in allow}
    if len(wanted) != len(allow):
        raise ValueError('Duplicate texture allowlist entry')
    found = {(t['name'], t['sha256']) for t in source.textures}
    if not wanted.issubset(found):
        raise ValueError('Texture identity differs from allowlist')
    allowed_textures = {i for i, t in enumerate(source.textures) if (t['name'], t['sha256']) in wanted}
    transforms = [multiply(matrix, multiply(inverse(source_poses[source_anchor]), pose)) for pose in source_poses]
    bodies, skinrefs, used_textures = [], set(), set()
    selected_triangles = excluded_triangles = 0
    for body_index, body in enumerate(source.bodyparts):
        models = []
        body_triangles = 0
        for model in body['models']:
            vertices, normals, vb, nb, meshes = [], [], {}, {}, []
            for mesh in model['meshes']:
                # Native studio lighting consumes per-mesh normal spans.
                # Keep those spans contiguous, even if two source materials
                # referenced the same source normal index.
                nb = {}
                normal_start = len(normals)
                textures = [row[mesh['skinref']] for row in source.skins]
                included = [index in allowed_textures for index in textures]
                if any(included) and not all(included):
                    raise ValueError('A selected mesh has an unapproved alternate skin')
                triangles = []
                for triangle in mesh['triangles']:
                    if not all(included) or any(model['verticesBones'][c[0]] not in allowed_bones for c in triangle):
                        excluded_triangles += 1
                        continue
                    corners = []
                    for v, n, s, t in triangle:
                        if v not in vb:
                            vb[v] = len(vertices)
                            vertices.append(transform(transforms[model['verticesBones'][v]], model['vertices'][v]))
                        if n not in nb:
                            nb[n] = len(normals)
                            normals.append(transform(transforms[model['normalsBones'][n]], model['normals'][n], normal=True))
                        corners.append((vb[v], nb[n], s, t))
                    triangles.append(tuple(corners))
                if triangles:
                    meshes.append(dict(skinref=mesh['skinref'], triangles=triangles, normalCount=len(normals) - normal_start))
                    skinrefs.add(mesh['skinref'])
                    used_textures.update(textures)
                    selected_triangles += len(triangles)
                    body_triangles += len(triangles)
            models.append(dict(name=model['name'], vertices=vertices, normals=normals, meshes=meshes))
        if body_triangles:
            bodies.append(dict(name=body['name'], sourceBody=body_index, models=models))
    if not selected_triangles:
        raise ValueError('No approved weapon triangles remain; no empty substitute is generated')
    skinrefs, used_textures = sorted(skinrefs), sorted(used_textures)
    skinmap = {old: new for new, old in enumerate(skinrefs)}
    texturemap = {old: new for new, old in enumerate(used_textures)}
    output = Writer()
    output.name(8, 64, 'cof_generated_player_weapon')
    bones_at = output.append(b''.join(b['raw'] for b in template.bones))
    output.put(140, 'ii', len(template.bones), bones_at)
    # Freeze the exact sampled template channels, preserving its authored
    # Hand->weapon local transform without Euler reconstruction or guessing.
    animation = bytearray(12*len(template.bones))
    target_sequence = calibration['target']['sequence']
    channels = template.channels(target_sequence['index'], target_sequence['frame'], target_sequence['blend'])
    for i, values in enumerate(channels):
        for axis, value in enumerate(values):
            if value:
                struct.pack_into('<H', animation, 12*i + 2*axis, len(animation) - 12*i)
                animation += struct.pack('<BBh', 1, 1, value)
    animation_at = output.append(animation)
    sequence_at = output.reserve(176)
    output.name(sequence_at, 32, 'idle')
    output.put(sequence_at + 32, 'f', 1)
    output.put(sequence_at + 36, 'i', 1)
    output.put(sequence_at + 56, 'i', 1)
    output.put(sequence_at + 120, 'ii', 1, animation_at)
    output.put(164, 'ii', 1, sequence_at)
    group = output.reserve(104)
    output.name(group, 32, 'default')
    output.put(172, 'ii', 1, group)
    texture_at = output.reserve(80*len(used_textures))
    output.put(180, 'ii', len(used_textures), texture_at)
    first_pixels = None
    for index, original in enumerate(used_textures):
        texture = source.textures[original]
        at = texture_at + 80*index
        output.name(at, 64, texture['name'])
        pixels = output.append(texture['pixels'])
        if first_pixels is None:
            first_pixels = pixels
        output.put(at + 64, '4i', texture['flags'], texture['width'], texture['height'], pixels)
    output.put(188, 'i', first_pixels)
    skins = b''.join(struct.pack('<H', texturemap[row[old]]) for row in source.skins for old in skinrefs)
    output.put(192, 'iii', len(skinrefs), len(source.skins), output.append(skins))
    body_at = output.reserve(76*len(bodies))
    output.put(204, 'ii', len(bodies), body_at)
    all_vertices = []
    base = 1
    for index, body in enumerate(bodies):
        at = body_at + 76*index
        models_at = output.reserve(112*len(body['models']))
        output.name(at, 64, body['name'])
        output.put(at + 64, 'iii', len(body['models']), base, models_at)
        base *= len(body['models'])
        for j, model in enumerate(body['models']):
            record = models_at + 112*j
            output.name(record, 64, model['name'])
            vertices = model['vertices']
            normals = model['normals']
            all_vertices.extend(vertices)
            radius = max((math.sqrt(sum(x*x for x in transform(template_poses[target], vertex))) for vertex in vertices), default=0)
            output.put(record + 68, 'f', radius)
            vertex_bones = output.append(bytes([target])*len(vertices))
            vertex_at = output.append(b''.join(struct.pack('<3f', *v) for v in vertices))
            normal_bones = output.append(bytes([target])*len(normals))
            normal_at = output.append(b''.join(struct.pack('<3f', *v) for v in normals))
            mesh_at = output.reserve(20*len(model['meshes']))
            output.put(record + 72, '8i', len(model['meshes']), mesh_at, len(vertices), vertex_bones,
                       vertex_at, len(normals), normal_bones, normal_at)
            for k, mesh in enumerate(model['meshes']):
                commands = triangle_commands(mesh['triangles'])
                commands_at = output.append(commands)
                output.put(mesh_at + 20*k, '5i', len(mesh['triangles']), commands_at,
                           skinmap[mesh['skinref']], mesh['normalCount'], 0)
    minimum = [min(v[i] for v in all_vertices) for i in range(3)]
    maximum = [max(v[i] for v in all_vertices) for i in range(3)]
    hitbox_at = output.append(struct.pack('<ii6f', target, 0, *minimum, *maximum))
    output.put(156, 'ii', 1, hitbox_at)
    world_vertices = [transform(template_poses[target], v) for v in all_vertices]
    world_min = [min(v[i] for v in world_vertices) for i in range(3)]
    world_max = [max(v[i] for v in world_vertices) for i in range(3)]
    output.put(88, '12f', *world_min, *world_max, *world_min, *world_max)
    output.put(sequence_at + 96, '6f', *world_min, *world_max)
    attachment_at = output.reserve(88*len(source.attachments))
    output.put(212, 'ii', len(source.attachments), attachment_at)
    for i, attachment in enumerate(source.attachments):
        at = attachment_at + 88*i
        output.name(at, 32, attachment['name'])
        output.put(at + 32, 'ii', attachment['type'], target)
        t = transforms[attachment['bone']]
        output.put(at + 40, '3f', *transform(t, attachment['origin']))
        for j, vector in enumerate(attachment['vectors']):
            output.put(at + 52 + 12*j, '3f', *transform(t, vector, normal=True))
    output.put(72, 'i', len(output.data))
    data = bytes(output.data)
    reread = Studio(data)
    if len(reread.attachments) != len(source.attachments) or len(reread.textures) != len(used_textures):
        raise ValueError('Generated model failed structural verification')
    receipt = dict(schema=1, candidate=bool(candidate or status != 'verified'), deliverable=bool(not candidate and status == 'verified'),
                   sourceSha256=source.sha256, templateSha256=template.sha256,
                   outputSha256=digest(data), outputBytes=len(data), selectedTriangles=selected_triangles,
                   excludedTriangles=excluded_triangles, bodyMap=[body['sourceBody'] for body in bodies],
                   textures=[dict(name=source.textures[i]['name'], sha256=source.textures[i]['sha256']) for i in used_textures],
                   attachments=len(source.attachments), calibration=calibration,
                   boundary='Static sampled geometry only. Hand grip, character action choice, gameplay, deployment and FPS require separate acceptance.')
    return data, receipt


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        raise ValueError('Use a new candidate/output path; existing files are preserved')
    fd, temporary = tempfile.mkstemp(prefix='.cof-model-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
        # On the supported Windows host rename fails if another writer has
        # created the destination since the preflight; preserve that file.
        os.rename(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-model', type=Path, required=True)
    parser.add_argument('--template-model', type=Path, required=True)
    parser.add_argument('--calibration', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--candidate', action='store_true')
    args = parser.parse_args()
    output, report = ordinary(args.output), ordinary(args.report)
    build = ordinary(ROOT / 'build')
    if not output.is_relative_to(build) or not report.is_relative_to(build) or output == report:
        raise ValueError('Model and receipt must use separate files under build/')
    if output.suffix.lower() != '.mdl' or report.suffix.lower() != '.json' or output.exists() or report.exists():
        raise ValueError('Use new MDL and JSON output paths')
    if args.candidate and not output.name.endswith('.candidate.mdl'):
        raise ValueError('Unreviewed artifacts must have the .candidate.mdl suffix')
    source = Studio.read(ordinary(args.source_model))
    template = Studio.read(ordinary(args.template_model))
    calibration = json.loads(ordinary(args.calibration).read_text(encoding='utf-8-sig'))
    data, receipt = generate(source, template, calibration, candidate=args.candidate)
    atomic_write(output, data)
    atomic_write(report, json.dumps(receipt, ensure_ascii=False, indent=2).encode('utf-8'))
    print(json.dumps({key: receipt[key] for key in ('candidate', 'deliverable', 'selectedTriangles', 'outputSha256')}))


if __name__ == '__main__':
    main()
