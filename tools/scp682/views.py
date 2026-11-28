"""Calibrated public renders and triangle visibility for image-based reconstruction."""
from pathlib import Path
from functools import lru_cache
import re
import ctypes
import json
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
WORK = ROOT / 'build/scp682'
REF = WORK / 'reference'
POSE = REF / 'open'
MOTION = REF / 'capture'
CLIPS = ('SCP_862_Run', 'SCP_862_Idle', 'SCP_862_Attack_Right',
         'SCP_862_Attack_Left', 'SCP_862_Death')
INTERVALS = (12, 40, 26, 26, 35)
cv2.setNumThreads(2)


def unit(x):
    return x / np.maximum(1e-12, np.linalg.norm(x, axis=-1, keepdims=True))


def foreground(rgb):
    r, g, b = rgb.astype(np.float32).transpose(2, 0, 1)
    return ((np.maximum(r, b) > 6) & (g < 1.38*np.maximum(r, b)+3)).astype(np.uint8)


@lru_cache(maxsize=18)
def archived_stream(path):
    stream=cv2.VideoCapture(str(path),cv2.CAP_FFMPEG,[cv2.CAP_PROP_N_THREADS,1])
    if not stream.isOpened():
        raise OSError(path)
    return stream


def rgb_file(path):
    if not path.exists():
        frame=re.fullmatch(r'm_(v[0-9]+)_([0-4])_([0-9]+)',path.stem)
        if frame:
            view,clip,at=frame.groups()
            movie=REF/'motion-archive'/f'{path.parent.name}-{view}.mkv'
            if movie.exists():
                stream=archived_stream(movie)
                n=sum(j+1 for j in INTERVALS[:int(clip)])+int(at)
                if int(stream.get(cv2.CAP_PROP_POS_FRAMES))!=n:
                    stream.set(cv2.CAP_PROP_POS_FRAMES,n)
                ok,bgr=stream.read()
                if not ok:
                    raise OSError(f'Cannot read {movie.name} frame {n}')
                return cv2.cvtColor(bgr,cv2.COLOR_BGR2RGB)
        raise FileNotFoundError(path)
    data = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if data is None:
        raise FileNotFoundError(path)
    return cv2.cvtColor(data, cv2.COLOR_BGR2RGB)


def bilinear(array, xy):
    x = np.clip(xy[:, 0], 0, array.shape[1]-1.0001)
    y = np.clip(xy[:, 1], 0, array.shape[0]-1.0001)
    i, j = x.astype(np.int32), y.astype(np.int32)
    u, v = x-i, y-j
    if array.ndim == 3:
        u, v = u[:, None], v[:, None]
    return ((1-u)*(1-v)*array[j, i] + u*(1-v)*array[j, i+1] +
            v*(1-u)*array[j+1, i] + v*u*array[j+1, i+1])


class Camera:
    def __init__(self, record):
        self.width, self.height = record['W'], record['H']
        self.origin = np.asarray(record['pose']['position'])
        forward = unit(np.asarray(record['pose']['target']) - self.origin)
        right = unit(np.cross(forward, [0, 0, 1]))
        self.basis = np.stack([right, np.cross(forward, right), forward])
        self.focal = .5*self.height/np.tan(np.radians(record['fov']*.5))
        self.center = np.array([self.width*.5, self.height*.5], dtype=float)
        if 'probe' in record:
            exact = np.array(record['pixels'])
            error = exact - self.project(record['probe'])[:, :2]
            self.center += error.mean(axis=0)
            if np.max(np.abs(error - error.mean(axis=0))) > .22:
                raise ValueError(f'Camera probe mismatch {error}')

    @classmethod
    def read(cls, path):
        return cls(json.loads(path.read_text()))

    def project(self, points):
        view = (np.asarray(points)-self.origin) @ self.basis.T
        view[..., :2] = self.center + self.focal*view[..., :2]/view[..., 2:]
        return view


def normals(vertices, faces):
    tri = vertices[faces]
    areas = np.cross(tri[:, 1]-tri[:, 0], tri[:, 2]-tri[:, 0])
    summed = np.zeros_like(vertices)
    for corner in faces.T:
        np.add.at(summed, corner, areas)
    return unit(summed)


def buffer(points, faces, width, height):
    if not hasattr(buffer, 'dll'):
        dll = ctypes.CDLL(str(WORK/'raster/Release/raster.dll'))
        ptr = ctypes.c_void_p
        dll.rasterize.argtypes = [ptr, ptr, ctypes.c_int, ctypes.c_int, ctypes.c_int, ptr, ptr, ptr]
        dll.rasterize.restype = None
        buffer.dll = dll
    p = np.ascontiguousarray(points, np.float32)
    f = np.ascontiguousarray(faces, np.int32)
    tri = np.full((height, width), -1, np.int32)
    bary = np.zeros((height, width, 3), np.float32)
    depth = np.full((height, width), 1e7, np.float32)
    buffer.dll.rasterize(p.ctypes.data, f.ctypes.data, len(f), width, height,
                         tri.ctypes.data, bary.ctypes.data, depth.ctypes.data)
    return tri, bary, depth


def shaded(points, faces, uv, mat, textures, cam):
    tri,weights,_=buffer(cam.project(points),faces,cam.width,cam.height)
    ys,xs=np.where(tri>=0)
    ids=tri[ys,xs]
    st=(uv[ids]*weights[ys,xs,:,None]).sum(1)
    image=np.full((cam.height,cam.width,3),[54,55,57],np.uint8)
    for k,page in enumerate(textures):
        indices=np.flatnonzero(mat[ids]==k)
        color=bilinear(page,st[indices]*[page.shape[1],page.shape[0]]-.5)
        image[ys[indices],xs[indices]]=np.clip(color,0,255).astype(np.uint8)
    return image,tri>=0
