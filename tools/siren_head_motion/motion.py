"""Original animation direction for the GoldSrc Siren Head creature. Game units, degrees, 40 fps."""

from math import sin, cos, pi
import numpy as np
import shockwave

FPS = 40
FRAMES = {"walk": 89, "stomp": 97, "flinch": 33, "death": 153, "shockwave": 113}
STRIDE = 44.0
IMPACT = 49


def ramp(x):
    x = max(0.0, min(1.0, x))
    return x * x * x * (10 + x * (-15 + 6 * x))


def curve(frame, keys):
    """Shape-preserving cubic Hermite interpolation with stationary end poses."""
    times = np.asarray([k[0] for k in keys], dtype=float)
    values = np.asarray([k[1:] for k in keys], dtype=float)
    if frame <= times[0]:
        return values[0].copy()
    if frame >= times[-1]:
        return values[-1].copy()
    interval = np.diff(times)
    speed = np.diff(values, axis=0) / interval[:, None]
    slopes = np.zeros_like(values)
    for j in range(1, len(times) - 1):
        left, right = speed[j - 1], speed[j]
        moving = left * right > 0
        a, b = 2 * interval[j] + interval[j - 1], interval[j] + 2 * interval[j - 1]
        slopes[j, moving] = (a + b) / (a / left[moving] + b / right[moving])
    j = int(np.searchsorted(times, frame, side="right") - 1)
    t = (frame - times[j]) / interval[j]
    return (1 - t) ** 2 * ((1 + 2 * t) * values[j] + t * interval[j] * slopes[j]) + t * t * (
        (3 - 2 * t) * values[j + 1] + (t - 1) * interval[j] * slopes[j + 1]
    )


def walk(p):
    # The stance foot travels backward at exactly the entity's nominal speed.
    angle = 2 * pi * p
    c, s = cos(angle), sin(angle)
    pose = dict(
        root=[-0.8 + 0.75 * sin(2 * angle - 0.5), 2.5 * sin(angle + 0.55), 40.8 - 1.7 * cos(2 * angle - 0.2)],
        pelvis=[-3.6 * s, 4.2 - 1.4 * cos(2 * angle), -6.8 * c],
        chest=[3.8 * sin(angle + 0.55), 13 + 2.9 * sin(2 * angle - 0.65), 6.6 * cos(angle - 0.35)],
        head=[
            -13 + 7 * sin(angle - 0.45),
            1.5 - 4.4 * sin(2 * angle - 0.95),
            -8
            + 8 * sin(angle - 0.8)
            + curve(p, [(0, 0), (0.18, 0), (0.31, 9), (0.48, 10), (0.68, -3), (0.87, 0), (1, 0)])[0],
        ],
        shrug=[0.8 + 2.5 * c, -1.8 - 2.0 * c],
        curl=[14 + 3 * s, 20 - 3 * s],
        hand_blend=0,
        corpse=0,
    )
    pose["feet"], pose["hands"], pose["fingers"] = [], [], []
    for index, sign in enumerate((1, -1)):
        phase = (p + 0.5 * index) % 1.0
        stance = 0.595 + 0.016 * index
        air = max(0.0, (phase - stance) / (1 - stance))
        height = (8.1 + 1.75 * index) * sin(pi * air) ** 2 * (1 + 0.23 * cos(pi * air))
        x = 12.3 - STRIDE * phase + STRIDE * ramp(air)
        pitch = curve(
            phase, [(0, -11), (0.115, 0), (0.40, 0), (stance, 26), (0.72, 30), (0.88, -8), (1, -11)]
        )[0]
        y = sign * (8.5 + 0.8 * index + 1.9 * sin(pi * air) ** 2)
        pose["feet"].append([x, y, height, pitch, sign * 5])
        # Wrists swing behind the shoulder beat. The two forearms have different resting shapes.
        q = angle + index * pi
        pose["hands"].append(
            [
                2.5 - 7.5 * cos(q - 0.5) + 2.1 * index,
                sign * (10 + 2.1 * cos(q - 0.23) + index),
                -43.7 + 2.5 * cos(q - 0.5) - 1.9 * index,
            ]
        )
        pose["fingers"].append([0.12 + 0.14 * sin(q - 0.64), sign * 0.10, -1.0])
    return {k: np.array(v, dtype=float) for k, v in pose.items()}


# Frame numbers are at 40 Hz. Curves for different masses use separate timing.
STOMP = {
    "root": [
        (0, 0, 0, 0),
        (9, -2, 1, -2),
        (23, -3, 5, -4.5),
        (35, -1, 6, 1.6),
        (41, 0, 6, 1.8),
        (46, 1.5, 2, -0.7),
        (49, 3, -1, -6),
        (53, 3.4, -2.2, -8),
        (62, 2, -1, -5.8),
        (78, -0.4, 1, -1.2),
        (96, 0, 0, 0),
    ],
    "pelvis": [
        (0, 0, 0, 0),
        (21, -9, -5, -12),
        (38, -11, -8, -16),
        (49, 9, 13, 13),
        (55, 12, 16, 15),
        (73, -2, -1, -3),
        (96, 0, 0, 0),
    ],
    "chest": [
        (0, 0, 0, 0),
        (16, 4, 12, 7),
        (33, -7, -16, -19),
        (42, -7, -18, -21),
        (50, 13, 27, 23),
        (56, 16, 32, 26),
        (68, 3, 11, 5),
        (82, -2, -3, -3),
        (96, 0, 0, 0),
    ],
    "head": [
        (0, 0, 0, 0),
        (12, -14, -8, 27),
        (23, -16, -9, 32),
        (37, 4, 1, 3),
        (45, 7, -13, -13),
        (52, 22, 37, 25),
        (57, 24, 42, 33),
        (66, 12, 19, 16),
        (78, -4, -5, -6),
        (88, 1, 2, 3),
        (96, 0, 0, 0),
    ],
    "shrug": [(0, 0, 0), (25, 8, -4), (41, 10, 5), (50, -7, -8), (67, 2, 1), (96, 0, 0)],
    "left_arm": [
        (0, 0, 0, 0),
        (20, -7, 3, 2),
        (38, -12, 9, 11),
        (46, -10, 8, 10),
        (53, 13, 1, 0),
        (62, 18, 0, 4),
        (79, -3, 1, 0),
        (96, 0, 0, 0),
    ],
    "right_arm": [
        (0, 0, 0, 0),
        (18, 9, -2, 4),
        (37, 17, -7, 15),
        (44, 18, -8, 14),
        (52, -10, -1, 1),
        (61, -15, -2, 4),
        (83, 0, -1, -1),
        (96, 0, 0, 0),
    ],
    "right_foot": [
        (0, 0, 0, 0, 0, 0),
        (13, 0, 0, 0, 4, 0),
        (19, 0, 0, 0, 26, -2),
        (29, 10, -2, 17, 31, -7),
        (38, 19, -2.6, 28, 10, -8),
        (42, 22, -2.8, 28.8, 0, -8),
        (46, 25, -3, 17, -5, -5),
        (48, 26, -3.2, 7, -4, -4),
        (49, 26, -3.2, 0, 0, -4),
        (62, 26, -3.2, 0, 0, -4),
        (69, 25, -3, 0, 18, -3),
        (78, 11, -1.7, 6, 13, -2),
        (89, 0, 0, 0, -2, 0),
        (96, 0, 0, 0, 0, 0),
    ],
    "curl": [(0, 0, 0), (23, 8, 14), (42, 16, 24), (55, -9, -12), (68, 3, 9), (96, 0, 0)],
}
FLINCH = {
    "root": [
        (0, 0, 0, 0),
        (3, -1.7, -1, -1.8),
        (7, -2.8, -2.5, -3.7),
        (12, -2, -1.5, -3),
        (20, 0.8, 1, -0.5),
        (32, 0, 0, 0),
    ],
    "pelvis": [(0, 0, 0, 0), (5, 5, -3, -8), (10, 6, -4, -11), (19, -2, 1, 2), (32, 0, 0, 0)],
    "chest": [
        (0, 0, 0, 0),
        (3, 16, -21, -24),
        (6, 20, -25, -29),
        (11, 11, -10, -13),
        (17, -6, 11, 8),
        (23, -3, 6, 4),
        (32, 0, 0, 0),
    ],
    "head": [
        (0, 0, 0, 0),
        (2, 0, 2, 2),
        (6, 22, -36, -35),
        (9, 31, -38, -44),
        (14, 9, -7, -12),
        (19, -12, 17, 18),
        (24, -4, 7, 9),
        (28, 2, -2, -2),
        (32, 0, 0, 0),
    ],
    "shrug": [(0, 0, 0), (4, 11, -6), (9, 8, -4), (19, -3, 2), (32, 0, 0)],
    "left_arm": [(0, 0, 0, 0), (3, 0, 0, 0), (8, 10, 5, 8), (14, 15, 3, 8), (22, -3, -1, 1), (32, 0, 0, 0)],
    "right_arm": [
        (0, 0, 0, 0),
        (4, 0, 0, 0),
        (10, -8, -5, 4),
        (14, -9, -4, 2),
        (21, 2, 1, -1),
        (32, 0, 0, 0),
    ],
    "curl": [(0, 0, 0), (7, -12, 20), (14, 9, 26), (23, 3, -4), (32, 0, 0)],
}

# A catch on the front knee, a failed brace, then separate shoulder and horn impacts.
DEATH = {
    "root": [
        (0, -1.16, 1.3, 39.1),
        (8, -2, 0, 36),
        (18, -4, -2, 32),
        (28, -1, 1, 35),
        (43, 1, 2, 24),
        (54, 4, 4, 15),
        (61, 6, 4.6, 12.5),
        (68, 7, 4.9, 14),
        (79, 8, 4.8, 12),
        (89, 10, 5.1, 9.4),
        (95, 12, 5.5, 7.2),
        (102, 12.7, 5.8, 8),
        (114, 13.4, 5.8, 6.9),
        (128, 13.7, 5.8, 6.4),
        (152, 13.7, 5.8, 6.4),
    ],
    "pelvis": [
        (0, 0, 2.8, -6.8),
        (13, 8, -6, -17),
        (30, -7, 9, 3),
        (55, -19, 24, 11),
        (75, -23, 43, 12),
        (95, -28, 74, 15),
        (106, -27, 70, 16),
        (125, -28, 75, 15),
        (152, -28, 75, 15),
    ],
    "chest": [
        (0, 2, 11, 6),
        (8, 13, -10, -14),
        (22, 17, -17, -25),
        (33, -4, 24, 6),
        (52, -15, 42, 15),
        (63, -19, 52, 18),
        (73, -13, 35, 12),
        (82, -15, 47, 14),
        (94, -23, 79, 16),
        (101, -26, 89, 17),
        (109, -21, 82, 15),
        (122, -23, 89, 17),
        (152, -23, 89, 17),
    ],
    "head": [
        (0, -11, 5, -15),
        (8, -21, -13, 13),
        (19, 19, -25, -34),
        (31, 23, 8, -19),
        (49, -5, 39, -3),
        (61, -14, 55, 10),
        (72, -5, 33, -4),
        (85, -12, 47, 4),
        (98, -9, 83, 3),
        (103, -14, 97, 14),
        (111, -9, 84, 6),
        (124, -13, 98, 12),
        (134, -16, 94, 14),
        (144, -15, 96, 13),
        (152, -15, 96, 13),
    ],
    "left_foot": [
        (0, 12.3, 8.5, 0, -11, 5),
        (21, 12.3, 8.5, 0, 0, 5),
        (32, 10.8, 9.2, 0, 9, 6),
        (42, 4.5, 10.1, 1.4, 23, 9),
        (55, -5, 12.3, 2.0, 37, 13),
        (66, -7.5, 14, 0, 40, 14),
        (81, -8, 15.5, 0, 34, 15),
        (96, -9, 17, 0, 29, 16),
        (127, -10, 18.5, 0, 17, 19),
        (152, -10, 18.5, 0, 17, 19),
    ],
    "right_foot": [
        (0, -9.7, -9.3, 0, 10, -5),
        (21, -9.7, -9.3, 0, 17, -6),
        (39, -12, -10, 0, 26, -10),
        (55, -17, -12, 0, 44, -16),
        (75, -21, -14, 0.9, 49, -23),
        (97, -23, -16, 0, 34, -25),
        (122, -24, -16, 0, 30, -25),
        (152, -24, -16, 0, 30, -25),
    ],
    "left_hand": [
        (40, 25, 21, 18),
        (53, 35, 24, 7),
        (64, 35, 24, 6),
        (78, 36, 25, 5),
        (93, 44, 37, 4.5),
        (107, 55, 49, 3.5),
        (126, 60, 54, 3),
        (138, 61, 55, 3),
        (152, 61, 55, 3),
    ],
    "right_hand": [
        (40, 11, -25, 20),
        (58, 20, -24, 13),
        (71, 21, -26, 15),
        (88, 22, -26, 6),
        (102, 15, -26, 3.5),
        (123, 13, -26, 2.8),
        (152, 13, -26, 2.8),
    ],
    "curl": [
        (0, 17, 20),
        (14, 35, 45),
        (39, 62, 50),
        (55, 5, 29),
        (77, -2, 21),
        (108, 11, 25),
        (126, 6, 18),
        (152, 6, 18),
    ],
}


def sample(name, frame):
    if name == "walk":
        return walk(frame / (FRAMES["walk"] - 1))
    pose = walk(0)
    if name == "shockwave":
        return shockwave.apply(pose, frame, curve)
    if name == "death":
        for channel in ("root", "pelvis", "chest", "head", "curl"):
            start = curve(0, DEATH[channel])
            value = curve(frame, DEATH[channel])
            pose[channel] = value + (pose[channel] - start) * (1 - ramp(frame / 12))
        for side, label in enumerate(("left", "right")):
            track = DEATH[label + "_foot"]
            pose["feet"][side] = curve(frame, track) + (pose["feet"][side] - curve(0, track)) * (
                1 - ramp(frame / 18)
            )
        pose["root"][2] += 1.24 * ramp((frame - 84) / 18)
        pose["absolute_hands"] = np.stack([curve(frame, DEATH[side + "_hand"]) for side in ("left", "right")])
        pose["hand_blend"] = ramp((frame - 23) / 31)
        pose["corpse"] = ramp((frame - 21) / 53)
        return pose
    tracks = STOMP if name == "stomp" else FLINCH
    for channel in ("root", "pelvis", "chest", "head", "shrug", "curl"):
        pose[channel] += curve(frame, tracks[channel])
    for side, label in enumerate(("left", "right")):
        pose["hands"][side] += curve(frame, tracks[label + "_arm"])
    if name == "stomp":
        trailing_pitch = pose["feet"][1, 3]
        shift = curve(frame, tracks["right_foot"])
        pose["feet"][1] += shift
        # The descending heel levels as it lands. The trailing foot rolls back to its walk pose.
        pose["feet"][1, 3] -= trailing_pitch * ramp((frame - 16) / 11) * (1 - ramp((frame - 79) / 17))
    return pose
