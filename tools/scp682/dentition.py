"""Reconstruct fine interlocking teeth from calibrated left and right jaw profiles."""
import cv2
import numpy as np
from PIL import Image
from scipy.ndimage import minimum_filter1d,gaussian_filter1d
from scipy.signal import find_peaks
from scipy.spatial import cKDTree
from views import *

SIDES=(('ring0',474,608,1),('ring7',309,422,-1))


def profile(mask,xs):
    rows=[]
    for x in xs:
        holes=np.flatnonzero(mask[270:401,x]==0)+270
        runs=np.split(holes,np.flatnonzero(np.diff(holes)>1)+1)
        gaps=[r for r in runs if len(r) and r[0]>270 and r[-1]<400]
        if not gaps:
            rows.append((np.nan,np.nan))
            continue
        gap=max(gaps,key=len)
        rows.append((gap[0]-1,gap[-1]+1))
    result=np.array(rows)
    for row in result.T:
        good=np.isfinite(row)
        row[:]=np.interp(xs,xs[good],row[good])
    return result


def build(v,f,bones,labels,stage):
    out_v,out_f,out_uv,out_b=[],[],[],[]
    texture=np.zeros((256,512,3),np.uint8)
    counts=[]
    for side,(name,first,last,forward) in enumerate(SIDES):
        path=REF/f'face/rest_{name}.json'
        cam=Camera.read(path)
        rgb=rgb_file(path.with_suffix('.png'))
        mask=foreground(rgb)
        depth=buffer(cam.project(v),f,cam.width,cam.height)[2]
        xs=np.arange(first,last+1)
        edges=profile(mask,xs)
        crop_x=425 if side==0 else 240
        texture[:,side*256:(side+1)*256]=rgb[178:434,crop_x:crop_x+256]
        for jaw,polarity in ((0,1),(1,-1)):
            heights=edges[:,jaw]*polarity
            baseline=gaussian_filter1d(minimum_filter1d(heights,19),2)
            prominence=heights-baseline
            peaks,_=find_peaks(prominence,distance=7,prominence=3.5,height=4.5)
            count=0
            eligible=(v[:,1]>1.30)&((labels=='Mandible') if jaw else (labels=='Skull'))
            local=np.flatnonzero(eligible)
            nearest=cKDTree(v[local])
            for i in peaks:
                tip_x=xs[i]
                tip_y=heights[i]*polarity
                root_x=tip_x-2.5*forward
                root_y=(baseline[i]-5)*polarity
                px,py=round(root_x),round(root_y)
                patch=depth[py-3:py+4,px-3:px+4]
                finite=patch[patch<100]
                if not len(finite):
                    continue
                z=np.quantile(finite,.30)+.016
                def unproject(x,y,d):
                    return cam.origin+(cam.basis.T@np.r_[(np.array([x,y])-cam.center)/cam.focal,1])*d
                root=unproject(root_x,root_y,z)
                tip=unproject(tip_x,tip_y,z+.002)
                if not 1.4<root[1]<2.75:
                    continue
                _,found=nearest.query(root)
                b=bones[local[found]]
                base=len(out_v)
                radius=np.clip((tip_y-root_y)*polarity*.28,2.35,6)*z/cam.focal
                for height,scale in ((0.,1.),(.49,.62)):
                    c=root*(1-height)+tip*height
                    for j in range(5):
                        angle=2*np.pi*j/5
                        out_v.append(c+radius*scale*(np.cos(angle)*cam.basis[0]+.76*np.sin(angle)*cam.basis[2]))
                out_v.append(tip)
                out_b.extend([b]*11)
                uv=[]
                for j,point in enumerate(out_v[base:]):
                    p=cam.project([point])[0,:2]
                    # Sample slightly within the filmed tooth, away from the green pixel fringe.
                    p=(p+np.array([root_x,root_y])*.19)/1.19
                    p=p-[crop_x,178]+[256*side,0]
                    uv.append(np.clip(p/[512,256],[0,0],[.999,.999]))
                for j in range(5):
                    k=(j+1)%5
                    for tri in ((j,k,k+5),(j,k+5,j+5),(j+5,k+5,10)):
                        idx=list(tri)
                        coords=np.asarray([out_v[base+t] for t in idx])
                        center=coords.mean(0)
                        direction=tip-root
                        axis=root+direction*np.dot(center-root,direction)/np.dot(direction,direction)
                        if np.dot(np.cross(coords[1]-coords[0],coords[2]-coords[0]),center-axis)<0:
                            idx.reverse()
                        out_f.append([base+t for t in idx])
                        out_uv.append([uv[t] for t in idx])
                count+=1
            counts.append(count)
    # Extend valid photographic colors across the background before quantizing. UV filtering at
    # the sharp enamel tips can otherwise introduce a green outline in GoldSrc.
    from scipy import ndimage as ndi
    good=foreground(texture).astype(bool)
    near=ndi.distance_transform_edt(~good,return_distances=False,return_indices=True)
    texture=texture[tuple(near)]
    Image.fromarray(texture).quantize(colors=256,method=Image.Quantize.MEDIANCUT).save(stage/'teeth.bmp')
    print('profile teeth per upper/lower side',counts,flush=True)
    return tuple(map(np.asarray,(out_v,out_f,out_uv,out_b)))
