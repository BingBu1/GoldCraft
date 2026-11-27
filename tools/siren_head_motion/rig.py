"""Two-link IK with heel and toe pivots, hand contacts and delayed upper-body motion."""

import numpy as np

from studio import Surface, accumulate, matrices, rot, to_angles
from motion import sample
from shockwave import GRIPS, MOUTHS


def unit(v):
    return v / np.linalg.norm(v)


def swing(v, w):
    a, b = unit(v), unit(w)
    cross = np.cross(a, b)
    dot = np.clip(a @ b, -1.0, 1.0)
    if dot < -0.999999:
        axis = unit(np.cross(a, np.eye(3)[np.argmin(abs(a))]))
        return 2 * np.outer(axis, axis) - np.eye(3)
    x, y, z = cross
    skew = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
    return np.eye(3) + skew + skew @ skew / (1 + dot)


def degrees(a):
    return rot(np.radians(a))


class Rig:
    def __init__(self, model):
        self.model = m = model
        self.surface = Surface(m)
        self.local = matrices(m.values)
        self.bind = accumulate(self.local, m.parents)
        self.basis = degrees([0, 0, 90]) @ self.bind[:, :3, :3]
        # Pick the speaker surfaces in neutral heading, then keep positions in the head bone's space.
        head = m.names.index("Bip01 Head")
        head_mesh = self.surface.vertices[self.surface.by_bone[head]]
        head_neutral = head_mesh @ self.basis[head].T

        def nearest(point):
            d = head_neutral - point
            return head_mesh[np.argmin(np.einsum("ij,ij->i", d, d))].copy()

        self.mouths = [nearest(p) for p in MOUTHS]
        self.grips = [nearest(p) for p in GRIPS]
        self.foot_cloud = []
        for foot in (34, 38):
            toe = foot + 1
            ankle = self.surface.vertices[self.surface.by_bone[foot]]
            tip = (
                self.surface.vertices[self.surface.by_bone[toe]] @ self.local[toe, :3, :3].T
                + self.local[toe, :3, 3]
            )
            self.foot_cloud.append(np.concatenate([ankle, tip]) @ self.basis[foot].T)

    def pose(self, name, frame):
        p = sample(name, frame)
        world = self.bind.copy()
        orient = world[:, :3, :3]
        xyz = world[:, :3, 3]
        local = self.local
        parents = self.model.parents

        def origin(j):
            parent = parents[j]
            xyz[j] = xyz[parent] + orient[parent] @ local[j, :3, 3]

        def follow(j):
            origin(j)
            orient[j] = orient[parents[j]] @ local[j, :3, :3]

        def facing(j, angles):
            origin(j)
            orient[j] = degrees(angles) @ self.basis[j]

        def aim(j, child, direction):
            prior = orient[parents[j]] @ local[j, :3, :3]
            orient[j] = swing(prior @ local[child, :3, 3], direction) @ prior

        def limb(a, b, c, target, pole):
            origin(a)
            start = xyz[a]
            ray = target - start
            axis = unit(ray)
            la = np.linalg.norm(local[b, :3, 3])
            lb = np.linalg.norm(local[c, :3, 3])
            length = np.clip(np.linalg.norm(ray), abs(la - lb) + 0.01, la + lb - 0.015)
            side = unit(pole - axis * (pole @ axis))
            along = (la * la - lb * lb + length * length) / (2 * length)
            hinge = start + axis * along + side * np.sqrt(max(0.0, la * la - along * along))
            aim(a, b, hinge - start)
            origin(b)
            aim(b, c, start + length * axis - xyz[b])
            origin(c)

        xyz[0] = p["root"]
        orient[0] = degrees(p["pelvis"]) @ self.basis[0]
        follow(1)
        for j, w in ((2, 0.28), (3, 0.65), (4, 1.0)):
            facing(j, (1 - w) * p["pelvis"] + w * p["chest"])
        facing(5, 0.72 * p["chest"] + 0.28 * p["head"])
        facing(6, p["head"])
        follow(7)
        for j in (30, 31):
            follow(j)

        for side, sign in enumerate((1, -1)):
            thigh = 32 + 4 * side
            calf, foot, toe = thigh + 1, thigh + 2, thigh + 3
            x, y, z, pitch, yaw = p["feet"][side]
            footrot = degrees([0, pitch, yaw])
            flat = degrees([0, 0, yaw])
            cloud = self.foot_cloud[side]
            # Rotate about the heel on landing and the toes when pushing off.
            pivot_x = np.interp(pitch, [-11, 0, 26], [-1.5, 1.0, 6.6])
            pivot = np.array([pivot_x, 0, -5.15])
            ankle = np.array([x, y, z]) + flat @ pivot - footrot @ pivot
            ankle[2] = z - (cloud @ footrot[2]).min()
            corpse = float(p["corpse"])
            knee = np.array([1.0, sign * (0.15 + corpse * 0.65), 0.12 - corpse * 0.46])
            if name == "stomp" and side == 1:
                # Lead with the knee while the heel stays tucked beneath the thigh.
                knee += p["knee_lift"] * np.array([0, -0.45, 0.9])
            limb(thigh, calf, foot, ankle, knee)
            orient[foot] = footrot @ self.basis[foot]
            follow(toe)

            clav = 8 + 11 * side
            upper, fore, hand = clav + 1, clav + 2, clav + 3
            facing(clav, 0.9 * p["chest"] + np.array([sign * p["shrug"][side], 0, 0]))
            origin(upper)
            wrist = xyz[upper] + p["hands"][side]
            blend = float(p["hand_blend"])
            if blend:
                wrist = wrist * (1 - blend) + p["absolute_hands"][side] * blend
            settled = max(0.0, min(1.0, (frame - 84) / 37)) if name == "death" else 0.0
            settled = settled * settled * (3 - 2 * settled)
            pole = np.array(
                [-1.0 + 0.75 * corpse, sign * (0.42 + corpse * 0.82), 0.02 + corpse * 0.68 - 0.82 * settled]
            )
            fingers = p["fingers"][side] * (1 - corpse) + np.array([0.88, sign * 0.26, -0.13]) * corpse
            rest_dir = self.basis[hand] @ local[hand + 3, :3, 3]
            neutral = swing(rest_dir, np.array([0.14, sign * 0.13, -1])) @ self.basis[hand]
            grip = p["grip"][side] if name == "shockwave" else 0.0
            wrist_rot = swing(neutral @ local[hand + 3, :3, 3], fingers) @ neutral
            if grip:
                # Swing via the forward axis: a direct swing from fingertips down to up can
                # reverse the wrist roll when a trumpet passes through the antipodal direction.
                forward = unit(np.array([0.96, -sign * 0.24, 0.02]))
                grip_dir = unit(np.array([0.32, -sign * 0.66, 0.74]))

                def partial_swing(start, end):
                    start, end = unit(start), unit(end)
                    arc = np.arccos(np.clip(start @ end, -0.999999, 0.999999))
                    direction = np.sin((1 - grip) * arc) * start + np.sin(grip * arc) * end
                    return swing(start, direction)

                wrist_rot = (
                    degrees(p["head"] * grip)
                    @ partial_swing(forward, grip_dir)
                    @ partial_swing(fingers, forward)
                    @ wrist_rot
                )
                head_rotation = orient[6] @ self.basis[6].T
                pole = (1 - grip) * pole + grip * np.array([0.18, sign * 1.03, -0.20])
                # Seat the heel of the hand against the flared housing; the fingers wrap above it.
                surface_point = xyz[6] + orient[6] @ self.grips[side]
                surface_normal = head_rotation @ np.array([0, sign, 0])
                pad = local[hand + 3, :3, 3] * 0.62
                contact = surface_point + 1.45 * surface_normal - wrist_rot @ pad
                outside = np.array([5.0, sign * 13.0, 0.0])
                wrist = (1 - grip) * wrist + grip * contact + np.sin(np.pi * grip) * outside

            def hand_chain():
                orient[hand] = wrist_rot
                for j, amount in ((hand + 1, 0.50), (hand + 2, 0.60), (hand + 3, 0.74), (hand + 4, 1.0)):
                    follow(j)
                    # Curl halfway toward the inside of the palm, with greater action at the knuckles.
                    bend = degrees([0, 0, np.clip(p["curl"][side] * amount, -14, 52)])
                    orient[j] = orient[j] @ bend

            limb(upper, fore, hand, wrist, pole)
            hand_chain()
            lowest = 999.0
            for j in range(hand, hand + 5):
                pts = self.surface.vertices[self.surface.by_bone[j]]
                if len(pts):
                    lowest = min(lowest, xyz[j, 2] + (pts @ orient[j][2]).min())
            # Ease the wrist into a planted, curled hand as the torso drops past it.
            gap = 0.48 - lowest
            wrist[2] += (1 - grip) * 0.5 * (gap + np.hypot(gap, 1.15))
            limb(upper, fore, hand, wrist, pole)
            hand_chain()
            for j in range(hand + 5, hand + 8):
                follow(j)
        return world

    def raw(self, world):
        m = world.copy()
        for j, parent in enumerate(self.model.parents):
            if parent >= 0:
                inv = world[:, parent, :3, :3].transpose(0, 2, 1)
                m[:, j, :3, :3] = inv @ world[:, j, :3, :3]
                m[:, j, :3, 3] = np.einsum("fij,fj->fi", inv, world[:, j, :3, 3] - world[:, parent, :3, 3])
        return np.concatenate((m[:, :, :3, 3], to_angles(m[:, :, :3, :3])), axis=-1)
