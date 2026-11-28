"""Intersect calibrated silhouettes, including close views where the head is on-screen."""
import cv2
import fast_simplification
import numpy as np
from scipy import ndimage as ndi
from skimage.measure import marching_cubes
from views import Camera, REF, POSE, WORK, rgb_file, bilinear, foreground, normals, buffer


def field(camera_path):
    mask = foreground(rgb_file(camera_path.with_suffix('.png')))
    inside = cv2.distanceTransform(mask, cv2.DIST_L2, 3)
    outside = cv2.distanceTransform(1-mask, cv2.DIST_L2, 3)
    return (inside-outside).astype(np.float32)


def carve():
    spacing = .013
    start = np.array([-1.65, -4.4, -.16])
    end = np.array([1.70, 3.55, 2.22])
    axes = [np.arange(a,b,spacing) for a,b in zip(start,end)]
    dims = tuple(map(len, axes))
    distance = np.full(dims, .7, np.float32)
    second = distance.copy()
    cameras = sorted(p for p in POSE.glob('rest_*.json') if 'hold' not in p.stem)
    closeups = sorted((REF/'face').glob('rest_*.json'))
    for path in cameras + closeups:
        cam = Camera.read(path)
        silhouette = field(path)
        is_head = path.parent != POSE
        for x in range(0, dims[0], 12):
            slab = distance[x:x+12]
            runner = second[x:x+12]
            indices = np.argwhere(runner > -.06)
            if not len(indices):
                continue
            points = start+(indices+[x,0,0])*spacing
            projection = cam.project(points)
            active = np.ones(len(points), dtype=bool)
            if is_head:
                active = ((points[:, 1] > 1.2) & (projection[:, 0] > 5) &
                          (projection[:, 0] < cam.width-6) & (projection[:, 1] > 5) &
                          (projection[:, 1] < cam.height-6))
            if not active.any():
                continue
            ijk = tuple(indices[active].T)
            s = bilinear(silhouette, projection[active, :2])*projection[active, 2]/cam.focal
            runner[ijk] = np.minimum(runner[ijk], np.maximum(slab[ijk], s))
            slab[ijk] = np.minimum(slab[ijk], s)
    smooth = ndi.gaussian_filter(np.clip(second,-.06,.06), .65)
    vertices, faces, _, _ = marching_cubes(smooth, level=-.005, spacing=(spacing,)*3)
    vertices += start
    # marching_cubes with positive interior returns an inward winding on this field.
    volume = np.einsum('ij,ij->i', vertices[faces[:,0]],
                        np.cross(vertices[faces[:,1]],vertices[faces[:,2]])).sum()
    if volume < 0:
        faces = faces[:, ::-1].copy()
    v, f = fast_simplification.simplify(vertices, faces.copy(), target_count=5400, agg=5)
    np.savez(WORK/'surface.npz', v=v, f=f, n=normals(v,f))
    print('surface',len(cameras),len(closeups),len(v),len(f),'extents',v.min(0),v.max(0))
    for path in sorted(POSE.glob('rest_hold*.json')):
        cam = Camera.read(path)
        obs = foreground(rgb_file(path.with_suffix('.png'))).astype(bool)
        result = buffer(cam.project(v), f, cam.width,cam.height)[0]>=0
        print('validation',path.name,(obs&result).sum()/(obs|result).sum())


if __name__ == '__main__':
    carve()
