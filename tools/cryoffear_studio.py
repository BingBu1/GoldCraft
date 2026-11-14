# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, read-only GoldSrc v10 geometry sampling for offline model tools."""
from __future__ import annotations
import hashlib
import math
from pathlib import Path
import struct


def multiply(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0)
             for j in range(4)] for i in range(3)]


def inverse(a):
    return [[a[j][i] if j < 3 else -sum(a[k][i] * a[k][3] for k in range(3))
             for j in range(4)] for i in range(3)]


def transform(a, p, normal=False):
    return tuple(sum(a[i][k] * p[k] for k in range(3)) + (0 if normal else a[i][3]) for i in range(3))


def pose_matrix(p):
    # GoldSrc AngleQuaternion and QuaternionMatrix; rotations are radians.
    sx, sy, sz = (math.sin(v / 2) for v in p[3:])
    cx, cy, cz = (math.cos(v / 2) for v in p[3:])
    x, y, z, w = sx*cy*cz-cx*sy*sz, cx*sy*cz+sx*cy*sz, cx*cy*sz-sx*sy*cz, cx*cy*cz+sx*sy*sz
    return [[1-2*y*y-2*z*z, 2*x*y-2*w*z, 2*x*z+2*w*y, p[0]],
            [2*x*y+2*w*z, 1-2*x*x-2*z*z, 2*y*z-2*w*x, p[1]],
            [2*x*z-2*w*y, 2*y*z+2*w*x, 1-2*x*x-2*y*y, p[2]]]


class Studio:
    def __init__(self, data, *, opaque_controllers=False):
        self.data = bytes(data)
        if len(data) < 244 or data[:4] != b'IDST' or self.i(4) != 10 or self.i(72) != len(data):
            raise ValueError('A complete studio v10 model is required')
        self.sha256 = hashlib.sha256(data).hexdigest()
        if type(opaque_controllers) is not bool:
            raise ValueError('Opaque controller access must be explicit')
        if self.i(148) and not opaque_controllers:
            raise ValueError('Bone controllers require an explicit sampled controller state')
        self.controllers = []
        self.span(self.i(152), self.i(148), 24, 8)
        for i in range(self.i(148)):
            at = self.i(152) + i*24
            self.controllers.append(dict(bone=self.i(at), type=self.i(at + 4),
                                         bounds=self.f(at + 8, 2), rest=self.i(at + 16),
                                         index=self.i(at + 20), raw=self.data[at:at + 24]))
        self.bones = []
        self.span(self.i(144), self.i(140), 112, 128)
        if self.i(140) < 1:
            raise ValueError('Missing skeleton')
        for i in range(self.i(140)):
            at = self.i(144) + 112*i
            parent = self.i(at + 32)
            if parent < -1 or parent >= i:
                raise ValueError('Invalid skeleton hierarchy')
            controls = struct.unpack_from('<6i', self.data, at + 40)
            if any(c < -1 or c >= len(self.controllers) for c in controls):
                raise ValueError('Invalid bone controller reference')
            self.bones.append(dict(name=self.string(at, 32), parent=parent, controllers=controls,
                                   pose=self.f(at + 64, 6), scale=self.f(at + 88, 6),
                                   raw=self.data[at:at + 112]))
        if len({b['name'] for b in self.bones}) != len(self.bones):
            raise ValueError('Ambiguous bone names')
        if any(c['bone'] < 0 or c['bone'] >= len(self.bones) for c in self.controllers):
            raise ValueError('Invalid controller bone')
        self.span(self.i(168), self.i(164), 176, 2048)
        self.textures = []
        self.span(self.i(184), self.i(180), 80, 512)
        for i in range(self.i(180)):
            at = self.i(184) + 80*i
            flags, w, h, pixels = struct.unpack_from('<4i', self.data, at + 64)
            if not 0 < w <= 8192 or not 0 < h <= 8192:
                raise ValueError('Invalid texture dimensions')
            self.span(pixels, w*h + 768, 1, 8192*8192 + 768)
            raw = self.data[pixels:pixels + w*h + 768]
            self.textures.append(dict(name=self.string(at, 64), flags=flags, width=w, height=h,
                                      pixels=raw, sha256=hashlib.sha256(raw).hexdigest()))
        refs, families = self.i(192), self.i(196)
        self.span(self.i(200), refs*families, 2, 65536)
        if not refs or not families or not self.textures:
            raise ValueError('Embedded textures and skin families are required')
        self.skins = [list(struct.unpack_from('<' + 'H'*refs, self.data, self.i(200) + 2*refs*i))
                      for i in range(families)]
        if any(t >= len(self.textures) for row in self.skins for t in row):
            raise ValueError('Invalid skin texture index')
        self.bodyparts = []
        self.span(self.i(208), self.i(204), 76, 256)
        for bi in range(self.i(204)):
            body = self.i(208) + 76*bi
            model_count, model_at = self.i(body + 64), self.i(body + 72)
            self.span(model_at, model_count, 112, 256)
            models = []
            for mi in range(model_count):
                at = model_at + 112*mi
                model = dict(name=self.string(at, 64), radius=self.f(at + 68, 1)[0])
                for kind, n_at, bone_at, vector_at in (('vertices', 80, 84, 88), ('normals', 92, 96, 100)):
                    n, bone, vector = self.i(at + n_at), self.i(at + bone_at), self.i(at + vector_at)
                    self.span(bone, n, 1, 32767)
                    self.span(vector, n, 12, 32767)
                    indices = list(self.data[bone:bone + n])
                    if any(index >= len(self.bones) for index in indices):
                        raise ValueError('Invalid geometry bone index')
                    model[kind] = [self.f(vector + 12*j, 3) for j in range(n)]
                    model[kind + 'Bones'] = indices
                self.span(self.i(at + 76), self.i(at + 72), 20, 1024)
                model['meshes'] = []
                for mesh_index in range(self.i(at + 72)):
                    mesh = self.i(at + 76) + 20*mesh_index
                    skinref = self.i(mesh + 8)
                    if not 0 <= skinref < refs:
                        raise ValueError('Invalid mesh skin reference')
                    cursor, triangles = self.i(mesh + 4), []
                    while True:
                        self.span(cursor, 1, 2)
                        count = struct.unpack_from('<h', self.data, cursor)[0]
                        cursor += 2
                        if not count:
                            break
                        if abs(count) < 3:
                            raise ValueError('Invalid triangle primitive')
                        self.span(cursor, abs(count), 8, 32767)
                        corners = [struct.unpack_from('<4h', self.data, cursor + 8*k) for k in range(abs(count))]
                        cursor += abs(count)*8
                        if any(not 0 <= c[0] < len(model['vertices']) or not 0 <= c[1] < len(model['normals']) for c in corners):
                            raise ValueError('Invalid triangle vertex or normal index')
                        for k in range(len(corners) - 2):
                            ids = (0, k + 1, k + 2) if count < 0 else ((k, k + 1, k + 2) if k % 2 == 0 else (k + 1, k, k + 2))
                            triangles.append(tuple(corners[index] for index in ids))
                    if len(triangles) != self.i(mesh):
                        raise ValueError('Triangle count differs from studio descriptor')
                    model['meshes'].append(dict(skinref=skinref, triangles=triangles, normalCount=self.i(mesh + 12)))
                models.append(model)
            self.bodyparts.append(dict(name=self.string(body, 64), models=models))
        self.attachments = []
        self.span(self.i(216), self.i(212), 88, 4)
        for i in range(self.i(212)):
            at = self.i(216) + 88*i
            bone = self.i(at + 36)
            if not 0 <= bone < len(self.bones):
                raise ValueError('Invalid attachment bone')
            self.attachments.append(dict(name=self.string(at, 32), type=self.i(at + 32), bone=bone,
                                         origin=self.f(at + 40, 3), vectors=[self.f(at + 52 + 12*j, 3) for j in range(3)]))

    @classmethod
    def read(cls, path, *, opaque_controllers=False):
        return cls(Path(path).read_bytes(), opaque_controllers=opaque_controllers)

    def span(self, offset, count, size, maximum=65536):
        if count < 0 or count > maximum or offset < 0 or offset > len(self.data) or count*size > len(self.data) - offset:
            raise ValueError('Studio table outside the model')

    def i(self, offset):
        self.span(offset, 1, 4)
        return struct.unpack_from('<i', self.data, offset)[0]

    def f(self, offset, count):
        self.span(offset, count, 4)
        values = struct.unpack_from('<' + 'f'*count, self.data, offset)
        if not all(math.isfinite(x) for x in values):
            raise ValueError('Nonfinite studio coordinate')
        return values

    def string(self, offset, count):
        self.span(offset, count, 1)
        return self.data[offset:offset + count].split(bytes([0]))[0].decode('latin1')

    def bone_index(self, name):
        matches = [i for i, bone in enumerate(self.bones) if bone['name'] == name]
        if len(matches) != 1:
            raise ValueError('Missing or ambiguous bone: ' + name)
        return matches[0]

    def channels(self, sequence, frame, blend):
        if not 0 <= sequence < self.i(164):
            raise ValueError('Invalid sequence index')
        at = self.i(168) + 176*sequence
        frames, blends = self.i(at + 56), self.i(at + 120)
        if not 0 < frames <= 65536 or not 0 < blends <= 16 or not 0 <= frame < frames or not 0 <= blend < blends:
            raise ValueError('Invalid animation frame or blend')
        if self.i(at + 156):
            raise ValueError('External sequence groups are not an implicit input')
        animation = self.i(at + 124)
        self.span(animation, blends*len(self.bones), 12, 2048)
        values = []
        for i in range(len(self.bones)):
            table = animation + 12*(blend*len(self.bones) + i)
            row = []
            for axis in range(6):
                offset = struct.unpack_from('<H', self.data, table + axis*2)[0]
                value = 0
                if offset:
                    cursor, remaining = table + offset, frame
                    while True:
                        self.span(cursor, 2, 1)
                        valid, total = self.data[cursor:cursor + 2]
                        if not 0 < valid <= total:
                            raise ValueError('Invalid animation RLE')
                        self.span(cursor + 2, valid, 2, 255)
                        if remaining < total:
                            value = struct.unpack_from('<h', self.data, cursor + 2 + 2*min(remaining, valid - 1))[0]
                            break
                        remaining -= total
                        cursor += 2*(valid + 1)
                row.append(value)
            values.append(row)
        return values

    def uncontrolled_scope(self, bones):
        """Prove that selected geometry and all its ancestors need no controls."""
        required = set()
        for index in bones:
            if type(index) is not int or not 0 <= index < len(self.bones):
                raise ValueError('Invalid sampled bone scope')
            while index >= 0 and index not in required:
                if any(c >= 0 for c in self.bones[index]['controllers']):
                    raise ValueError('Sampled geometry depends on a bone controller')
                required.add(index)
                index = self.bones[index]['parent']
        if not required:
            raise ValueError('Empty sampled bone scope')
        return frozenset(required)

    def sample(self, sequence, frame, blend, *, bone_scope=None):
        if self.controllers and bone_scope is None:
            raise ValueError('Bone controllers require an explicit sampled controller state or independent bone scope')
        required = None if bone_scope is None else self.uncontrolled_scope(bone_scope)
        channels = self.channels(sequence, frame, blend)
        poses = []
        for i, (bone, values) in enumerate(zip(self.bones, channels)):
            if required is not None and i not in required:
                poses.append(None)
                continue
            local = pose_matrix([a + b*c for a, b, c in zip(bone['pose'], bone['scale'], values)])
            poses.append(multiply(poses[bone['parent']], local) if bone['parent'] >= 0 else local)
        return poses
