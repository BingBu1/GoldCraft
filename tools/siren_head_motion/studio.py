"""GoldSrc skeletal data used by the Siren Head animator."""

from pathlib import Path
import struct
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from cryoffear_studio import Studio


def integer(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def rot(angles):
    x, y, z = np.moveaxis(angles, -1, 0)
    sx, sy, sz = np.sin(x), np.sin(y), np.sin(z)
    cx, cy, cz = np.cos(x), np.cos(y), np.cos(z)
    elements = (
        cy * cz,
        sx * sy * cz - cx * sz,
        cx * sy * cz + sx * sz,
        cy * sz,
        sx * sy * sz + cx * cz,
        cx * sy * sz - sx * cz,
        -sy,
        sx * cy,
        cx * cy,
    )
    return np.stack(elements, axis=-1).reshape(*angles.shape[:-1], 3, 3)


def to_angles(rotation):
    cy = np.hypot(rotation[..., 0, 0], rotation[..., 1, 0])
    regular = cy > 1e-7
    roll = np.where(
        regular,
        np.arctan2(rotation[..., 2, 1], rotation[..., 2, 2]),
        np.arctan2(-rotation[..., 1, 2], rotation[..., 1, 1]),
    )
    yaw = np.where(regular, np.arctan2(rotation[..., 1, 0], rotation[..., 0, 0]), 0.0)
    return np.stack((roll, np.arctan2(-rotation[..., 2, 0], cy), yaw), axis=-1)


def matrices(channels):
    result = np.broadcast_to(np.eye(4), (*channels.shape[:-1], 4, 4)).copy()
    result[..., :3, 3] = channels[..., :3]
    result[..., :3, :3] = rot(channels[..., 3:])
    return result


def accumulate(local, parents):
    world = local.copy()
    for bone, parent in enumerate(parents):
        if parent >= 0:
            world[..., bone, :, :] = world[..., parent, :, :] @ local[..., bone, :, :]
    return world


class Model:
    def __init__(self, path):
        self.studio = studio = Studio.read(path)
        self.b = studio.data
        self.names = [bone["name"] for bone in studio.bones]
        self.parents = [bone["parent"] for bone in studio.bones]
        self.values = np.array([bone["pose"] for bone in studio.bones])
        self.scales = np.array([bone["scale"] for bone in studio.bones])
        self.seq = {}
        for idx in range(studio.i(164)):
            offset = studio.i(168) + idx * 176
            self.seq[studio.string(offset, 32)] = offset

    def raw(self, name):
        index = (self.seq[name] - self.studio.i(168)) // 176
        count = self.studio.i(self.seq[name] + 56)
        values = np.array([self.studio.channels(index, frame, 0) for frame in range(count)])
        return self.values + values * self.scales


class Surface:
    def __init__(self, model):
        meshes = [m for p in model.studio.bodyparts for m in p["models"]]
        self.vertices = np.concatenate([m["vertices"] for m in meshes])
        self.bones = np.concatenate([m["verticesBones"] for m in meshes])
        self.by_bone = [np.flatnonzero(self.bones == j) for j in range(len(model.names))]

    def skin(self, world):
        posed = world[self.bones]
        return np.einsum("vij,vj->vi", posed[:, :3, :3], self.vertices) + posed[:, :3, 3]
