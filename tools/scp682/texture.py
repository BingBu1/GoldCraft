"""Bake two indexed GoldSrc texture pages from visible views at the open-jaw bind pose."""
import cv2
import numpy as np
import xatlas
from PIL import Image
from scipy import ndimage as ndi
from views import Camera, REF, POSE, WORK, rgb_file, bilinear, foreground, unit, buffer

SIZE = 512


def unwrap(vertices, faces):
    used, compact = np.unique(faces.ravel(), return_inverse=True)
    atlas = xatlas.Atlas()
    atlas.add_mesh(vertices[used].astype(np.float32), compact.reshape(-1,3).astype(np.uint32))
    packs = xatlas.PackOptions()
    packs.padding = 3
    packs.resolution = SIZE
    charts = xatlas.ChartOptions()
    charts.max_iterations = 2
    atlas.generate(chart_options=charts, pack_options=packs)
    mapping, index, uv = atlas[0]
    if not np.array_equal(used[mapping[index]], faces):
        raise ValueError('Atlas changed face order')
    return uv[index]


def bake_page(v, f, n, selected, views, number):
    faces = f[selected]
    uv = unwrap(v,faces)
    pixel = np.column_stack([uv.reshape(-1,2)*SIZE-.5,np.ones(uv.size//2)])
    tri, bary, _ = buffer(pixel, np.arange(len(pixel)).reshape(-1,3), SIZE,SIZE)
    ys,xs = np.where(tri>=0)
    indices = faces[tri[ys,xs]]
    weights = bary[ys,xs,:,None]
    points = (v[indices]*weights).sum(1)
    normals = unit((n[indices]*weights).sum(1))
    total = np.zeros((len(points),3))
    strength = np.zeros(len(points))
    for cam, rgb, distance, depth, close in views:
        projected = cam.project(points)
        x,y = np.rint(projected[:,:2]).astype(int).T
        valid = ((x>=1)&(x<cam.width-1)&(y>=1)&(y<cam.height-1))
        if close:
            valid &= points[:,1]>1.1
        ids = np.flatnonzero(valid)
        if not len(ids):
            continue
        xy = projected[ids,:2]
        face_on = (unit(cam.origin-points[ids])*normals[ids]).sum(1)
        residual = projected[ids,2]-depth[y[ids],x[ids]]
        coverage = np.clip((bilinear(distance,xy)-.8)/2.8,0,1)
        visibility = np.clip(1-(residual-.015)/.075,0,1)
        orient = np.clip((face_on+.04)/1.04,0,1)
        score = (orient*visibility*coverage)**17
        if close:
            score *= 28.
        total[ids] += score[:,None]*bilinear(rgb,xy)
        strength[ids] += score
    good = strength > 1e-9
    # Use a sharp side photograph where it directly sees the eye sockets and individual scars.
    # Angular blending keeps the dorsal and ventral surfaces on their own projections.
    color=total/np.maximum(strength[:,None],1e-12)
    for label in ('ring0','ring7'):
        p=REF/f'face/rest_{label}.json'
        cam=Camera.read(p)
        src=rgb_file(p.with_suffix('.png'))
        m=foreground(src)
        distance=cv2.distanceTransform(m,cv2.DIST_L2,3)
        pr=cam.project(points)
        profile=unit(cam.origin-points)
        lateral=(profile*normals).sum(1)
        zbuf=buffer(cam.project(v),f,cam.width,cam.height)[2]
        xx,yy=np.rint(pr[:,:2]).astype(int).T
        inside=(xx>=1)&(xx<cam.width-1)&(yy>=1)&(yy<cam.height-1)
        ids=np.flatnonzero(inside & (points[:,1]>.93))
        sr=pr[ids,:2]
        d=pr[ids,2]-zbuf[yy[ids],xx[ids]]
        gain=np.clip((lateral[ids]-.27)/.42,0,1)*np.clip((points[ids,1]-.93)/.28,0,1)
        gain*=np.clip((bilinear(distance,sr)-.45)/2.2,0,1)*np.clip(1-(d-.025)/.055,0,1)
        color[ids]=(1-gain[:,None])*color[ids]+gain[:,None]*bilinear(src,sr)
        good[ids]|=gain>.02
    atlas = np.zeros((SIZE,SIZE,3),np.uint8)
    atlas[ys[good],xs[good]] = np.clip(color[good],0,255).astype(np.uint8)
    # Only carry colour through a small hidden pocket from adjacent known surface texels.
    # The chart gutter gets the nearest written colour before palette quantization.
    covered = np.zeros((SIZE,SIZE),bool)
    covered[ys[good],xs[good]]=True
    nearest = ndi.distance_transform_edt(~covered,return_distances=False,return_indices=True)
    atlas = atlas[tuple(nearest)]
    detail=atlas.astype(np.float32)-cv2.GaussianBlur(atlas,(0,0),.9).astype(np.float32)
    atlas=np.clip(atlas+.32*detail,0,255).astype(np.uint8)
    image = Image.fromarray(atlas).quantize(colors=256,method=Image.Quantize.MEDIANCUT)
    out=WORK/'compile'
    out.mkdir(exist_ok=True)
    image.save(out/f'skin{number}.bmp')
    print('page',number,'triangles',len(faces),'covered texels',round(good.mean(),5),flush=True)
    return uv


def main():
    data=np.load(WORK/'surface.npz')
    v,f,n=data['v'],data['f'],data['n']
    views=[]
    for root in (POSE,REF/'face'):
        for path in sorted(root.glob('rest_*.json')):
            if 'hold' in path.stem:
                continue
            cam=Camera.read(path)
            rgb=rgb_file(path.with_suffix('.png'))
            flat_dir=REF/('albedo-face' if root!=POSE else 'albedo')
            flat=rgb_file(flat_dir/path.with_suffix('.png').name)
            # Preserve contact shading on top while lifting shadows that would rotate with the belly.
            # The diffuse material is available in the viewer's documented shadeless display mode.
            lit=rgb.astype(np.float32)
            flat=flat.astype(np.float32)
            luminance=lit.mean(2,keepdims=True)
            diffuse=flat.mean(2,keepdims=True)
            fraction=.13+.50*np.clip(1.15-luminance/np.maximum(14,diffuse),0,1)
            rgb=np.clip(lit*(1-fraction)+flat*fraction,0,255).astype(np.uint8)
            dist=cv2.distanceTransform(foreground(rgb),cv2.DIST_L2,3)
            depth=buffer(cam.project(v),f,cam.width,cam.height)[2]
            views.append((cam,rgb,dist,depth,root!=POSE))
    material=(v[f,1].mean(1)>1.0).astype(np.int32)
    uv=np.empty((len(f),3,2))
    for k in (0,1):
        sel=material==k
        uv[sel]=bake_page(v,f,n,sel,views,k)
    np.savez(WORK/'uv.npz',uv=uv,material=material)


if __name__=='__main__':
    main()
