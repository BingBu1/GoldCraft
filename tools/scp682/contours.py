"""Fit both sides of the source/output silhouette discrepancy across calibrated views."""
import sys
import cv2
import numpy as np
from scipy.spatial import cKDTree
from views import *
from refine import SurfaceFit


def accumulate_lin(A,B,indices,weights,jac,delta):
    # A contour point belongs to three triangle vertices at its perspective-correct weights.
    for vertex,coeff in zip(indices.T,weights.T):
        for r in range(3):
            v=jac[:,r]*coeff
            B[:,r]+=np.bincount(vertex,v*delta,minlength=len(B))
            for s in range(r+1):
                A[:,r,s]+=np.bincount(vertex,v*jac[:,s],minlength=len(B))


class ContourFit:
    def __init__(self):
        self.model=SurfaceFit()
        self.f=self.model.f

    def prepare(self,k,j):
        refs=[]
        for i in self.model.ids:
            mask=foreground(rgb_file(MOTION/f'm_v{i}_{k}_{j}.png'))
            # A 1px close removes isolated lighting speckles from the contour correspondence.
            mask=cv2.morphologyEx(mask,cv2.MORPH_CLOSE,np.ones((3,3),np.uint8))
            sdf=cv2.distanceTransform(mask,cv2.DIST_L2,3)-cv2.distanceTransform(1-mask,cv2.DIST_L2,3)
            yy,xx=np.where(mask-cv2.erode(mask,np.ones((3,3),np.uint8)))
            xy=np.column_stack([xx,yy])
            grad=unit(np.stack(np.gradient(sdf)[::-1],axis=2))
            refs.append((self.model.cams[i],xy,grad[yy,xx],cKDTree(xy)))
        return refs

    def update(self,xyz,refs,anchor):
        N=len(xyz)
        A=np.tile(np.eye(3)*1.5,(N,1,1))
        B=(anchor-xyz)*1.0
        for cam,op,og,tree in refs:
            uvz=cam.project(xyz)
            tri,bar,depth=buffer(uvz,self.f,cam.width,cam.height)
            h=(tri>=0).astype(np.uint8)
            yy,xx=np.where(h-cv2.erode(h,np.ones((3,3),np.uint8)))
            mp=np.column_stack([xx,yy])
            d,obs=tree.query(mp)
            dr,model=cKDTree(mp).query(op)
            valid=np.flatnonzero(d<23)[::2]
            rev=np.flatnonzero(dr<30)[::2]
            which=np.r_[valid,model[rev]]
            other=np.r_[obs[valid],rev]
            y,x=yy[which],xx[which]
            faces=self.f[tri[y,x]]
            bary=bar[y,x]
            grad=og[other]
            pixels=op[other]-mp[which]
            dist=(pixels*grad).sum(1)
            influence=np.minimum(1.,10/np.maximum(1,np.linalg.norm(pixels,axis=1)))
            rays=(mp[which]-cam.center)/cam.focal
            jac=grad@cam.basis[:2]-(grad*rays).sum(1)[:,None]*cam.basis[2]
            accumulate_lin(A,B,faces,bary*influence[:,None],jac,dist*depth[y,x]/cam.focal)
        A[:,0,1]=A[:,1,0]
        A[:,0,2]=A[:,2,0]
        A[:,1,2]=A[:,2,1]
        change=np.linalg.solve(A,B[:,:,None])[:,:,0]
        return self.model.arap(xyz,xyz+change*1.19)

    def clip(self,k):
        base=np.load(WORK/f'motion/marked-{k}.npy')
        if k==1:
            base=.82*np.load(WORK/'motion/dense-1.npy')+.18*base
            base[8]=self.model.v
        output=[]
        for j,xyz in enumerate(base):
            refs=self.prepare(k,j)
            pose=xyz.copy()
            for it in range(9):
                pose=self.update(pose,refs,xyz)
            output.append(pose)
            if j%8==0: print('outline',k,j,flush=True)
        if k<2: output[-1]=output[0]
        np.save(WORK/f'motion/outline-{k}.npy',output)


if __name__=='__main__':
    ContourFit().clip(int(sys.argv[1]))
