"""Multi-view contour and optical-flow fitting with as-rigid-as-possible surface regularization."""
import sys
import numpy as np
import cv2
from scipy import sparse
from scipy.sparse.linalg import factorized
from track import Tracker, gray
from views import *


class SurfaceFit:
    def __init__(self):
        self.track = Tracker()
        self.v,self.f=self.track.v,self.track.f
        self.ids=[0,1,2,3,4,6,*range(7,17)]
        self.cams={i:Camera.read(MOTION/f'v{i}.json') for i in self.ids}
        edges=np.unique(np.sort(np.concatenate([self.f[:,[0,1]],self.f[:,[1,2]],self.f[:,[2,0]]]),axis=1),axis=0)
        self.a,self.b=np.concatenate([edges,edges[:,::-1]]).T
        self.edge=self.v[self.a]-self.v[self.b]
        self.w=np.clip(.1/np.maximum(.012,np.linalg.norm(self.edge,axis=1)),.25,3)**1.4
        self.N=len(self.v)
        adj=sparse.csr_matrix((self.w,(self.a,self.b)),shape=(self.N,self.N))
        deg=np.asarray(adj.sum(axis=1)).ravel()
        self.data=.90
        self.solve=factorized((sparse.diags(deg+self.data)-adj).tocsc())
        self.bonew=np.eye(len(self.track.rig['names']))[self.track.rig['bone']]
        for _ in range(4):
            self.bonew=(self.bonew+(adj@self.bonew)/deg[:,None])*.5

    def lbs(self,pose):
        relative=self.v[:,None,:]-self.track.rig['center']
        world=np.einsum('bij,vbj->vbi',pose[:,:,:3],relative)+pose[None,:,:,3]
        return (world*self.bonew[:,:,None]).sum(1)

    def arap(self,xyz,target):
        rest=self.edge
        for _ in range(3):
            deformed=xyz[self.a]-xyz[self.b]
            outer=(rest[:,:,None]*deformed[:,None,:]*self.w[:,None,None]).reshape(-1,9)
            cov=np.stack([np.bincount(self.a,outer[:,i],minlength=self.N) for i in range(9)],1).reshape(-1,3,3)
            u,s,vt=np.linalg.svd(cov)
            sign=np.ones((self.N,3))
            sign[:,2]=np.linalg.det(u)*np.linalg.det(vt)
            rot=(vt.transpose(0,2,1)*sign[:,None,:])@u.transpose(0,2,1)
            step=np.einsum('nij,nj->ni',(rot[self.a]+rot[self.b])*.5,rest)*self.w[:,None]
            rhs=np.stack([np.bincount(self.a,step[:,i],minlength=self.N) for i in range(3)],1)
            xyz=self.solve(rhs+target*self.data)
        return xyz

    def observe(self,k,j):
        frames={}
        for i in self.ids:
            rgb=rgb_file(MOTION/f'm_v{i}_{k}_{j}.png')
            mask=foreground(rgb)
            d=cv2.distanceTransform(mask,cv2.DIST_L2,3)-cv2.distanceTransform(1-mask,cv2.DIST_L2,3)
            grad=np.stack(np.gradient(d)[::-1],axis=2)
            frames[i]=gray(rgb,mask),mask,d,grad
        return frames

    def step(self,xyz,observed,iteration):
        A=np.tile(np.eye(3)*.12,(self.N,1,1))
        b=np.zeros_like(xyz)
        core_views=(0,1,2,3,4,6)
        for i,cam in self.cams.items():
            uvz=cam.project(xyz)
            uv=uvz[:,:2]
            tri,bar,depth=buffer(uvz,self.f,cam.width,cam.height)
            x,y=np.rint(uv).astype(int).T
            x=np.clip(x,0,cam.width-1)
            y=np.clip(y,0,cam.height-1)
            obs,mask,sdf,grad=observed[i]
            # Exterior violations constrain every vertex. A visible contour also supplies
            # an inward correction wherever the current shell stops short of the outline.
            dist=bilinear(sdf,uv)
            model_mask=(tri>=0).astype(np.uint8)
            inset=cv2.distanceTransform(model_mask,cv2.DIST_L2,3)[y,x]
            near=cv2.erode(depth,np.ones((3,3),np.uint8))[y,x]
            visible=np.abs(uvz[:,2]-near)<.13
            contour=(inset<3.2)&visible
            w=((dist < -.7) | (contour & (dist < 33))).astype(float)
            normal=unit(bilinear(grad,uv))
            rays=(uv-cam.center)/cam.focal
            jac=cam.basis[:2][None,:,:]-rays[:,:,None]*cam.basis[2]
            direction=np.einsum('vi,vij->vj',normal,jac)
            w *= 1.7
            A+=w[:,None,None]*direction[:,:,None]*direction[:,None,:]
            b+=w[:,None]*direction*(-np.clip(dist,-30,30)*uvz[:,2]/cam.focal)[:,None]
            if i not in core_views:
                continue
            now=self.track.render(tri,bar)
            yy,xx=np.where((mask>0)|(model_mask>0))
            xl,xr=max(0,xx.min()-28),min(cam.width,xx.max()+29)
            yt,yb=max(0,yy.min()-28),min(cam.height,yy.max()+29)
            flow=self.track.flow.calc(np.ascontiguousarray(now[yt:yb,xl:xr]),np.ascontiguousarray(obs[yt:yb,xl:xr]),None)
            delta=bilinear(flow,uv-[xl,yt])*(uvz[:,2]/cam.focal)[:,None]
            mag=np.maximum(.001,np.linalg.norm(delta,axis=1))
            pwt=visible*.7*np.minimum(1,.21/mag)
            jt=jac.transpose(0,2,1)*pwt[:,None,None]
            A+=jt@jac
            b+=np.einsum('vij,vj->vi',jt,delta)
        delta=np.linalg.solve(A,b[:,:,None])[:,:,0]
        return self.arap(xyz,xyz+delta)

    def clip(self,k):
        poses=np.load(WORK/f'motion/{k}.npy')
        starts=[self.lbs(p) for p in poses]
        results=np.empty((len(poses),*self.v.shape))
        for j,base in enumerate(starts):
            obs=self.observe(k,j)
            if j:
                xyz=results[j-1]+(base-starts[j-1])
                xyz=.8*xyz+.2*base
            else:
                xyz=base
            xyz=self.arap(xyz,xyz)
            for it in range(6 if j==0 else 4):
                xyz=self.step(xyz,obs,it)
            results[j]=xyz
            if j%8==0: print(k,j,flush=True)
        if k<2:
            results[-1]=results[0]
        np.save(WORK/f'motion/dense-{k}.npy',results)


if __name__=='__main__':
    fit=SurfaceFit()
    for val in sys.argv[1:] or [0,1,2,3,4]:
        fit.clip(int(val))
