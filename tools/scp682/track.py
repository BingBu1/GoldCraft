"""Fit the reconstructed skeleton to consecutive visible frames and rendered feedback."""
import sys
import cv2
import numpy as np
from views import *
from rig import fit,skin,rest_pose,bind

TRAIN=(0,1,2,3,4,6)  # The sixth camera is reserved for independent animation scoring.


def gray(rgb, mask):
    base=cv2.cvtColor(rgb,cv2.COLOR_RGB2GRAY)
    return np.where(mask,np.clip(base.astype(float)*1.30+8,0,200),216).astype(np.uint8)


class Tracker:
    def __init__(self):
        g=np.load(WORK/'surface.npz')
        tex=np.load(WORK/'uv.npz')
        self.v,self.f,self.uv,self.mat=g['v'],g['f'],tex['uv'],tex['material']
        self.rig=dict(np.load(WORK/'rig.npz'))
        self.cams={i:Camera.read(MOTION/f'v{i}.json') for i in TRAIN}
        self.pages=np.stack([rgb_file(WORK/f'compile/skin{i}.bmp') for i in (0,1)])
        self.flow=cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
        self.flow.setFinestScale(1)
        self.flow.setVariationalRefinementIterations(1)

    def frame(self,k,j):
        return {i:self.source(MOTION/f'm_v{i}_{k}_{j}.png') for i in TRAIN}

    @staticmethod
    def source(path):
        rgb=rgb_file(path)
        mask=foreground(rgb)
        return gray(rgb,mask),mask

    def render(self,tri,bary):
        ys,xs=np.where(tri>=0)
        faces=tri[ys,xs]
        uv=(self.uv[faces]*bary[ys,xs,:,None]).sum(1)
        uv=np.clip((uv*512).astype(int),0,511)
        image=np.full((*tri.shape,3),216,np.uint8)
        image[ys,xs]=self.pages[self.mat[faces],uv[:,1],uv[:,0]]
        return gray(image,tri>=0)

    def align(self,pose,source,dest,passes=1):
        for turn in range(passes):
            xyz=skin(self.v,self.rig,pose)
            mat=np.tile(np.eye(3)*.045,(len(xyz),1,1))
            vec=np.zeros_like(xyz)
            support=np.zeros(len(xyz))
            for i,cam in self.cams.items():
                uvz=cam.project(xyz)
                tri,bary,depth=buffer(uvz,self.f,cam.width,cam.height)
                target,mask=dest[i]
                if source is not None and turn==0:
                    base=source[i][0]
                else:
                    base=self.render(tri,bary)
                yy,xx=np.where((tri>=0)|(mask>0)|(base!=216))
                x0,x1=max(0,xx.min()-28),min(cam.width,xx.max()+29)
                y0,y1=max(0,yy.min()-28),min(cam.height,yy.max()+29)
                flow=self.flow.calc(np.ascontiguousarray(base[y0:y1,x0:x1]),
                                    np.ascontiguousarray(target[y0:y1,x0:x1]),None)
                x,y=np.rint(uvz[:,:2]).astype(int).T
                x=np.clip(x,0,cam.width-1)
                y=np.clip(y,0,cam.height-1)
                # A small local dilation keeps silhouette vertices on the visible layer.
                local=cv2.erode(depth,np.ones((3,3),np.uint8))
                residual=np.abs(uvz[:,2]-local[y,x])
                wt=np.clip(1-(residual-.045)/.12,0,1)
                ids=np.flatnonzero(wt>.02)
                if not len(ids):
                    continue
                displacement=bilinear(flow,uvz[ids,:2]-[x0,y0])
                scaled=displacement*(uvz[ids,2]/cam.focal)[:,None]
                rays=(uvz[ids,:2]-cam.center)/cam.focal
                J=cam.basis[:2][None,:,:]-rays[:,:,None]*cam.basis[2]
                jt=J.transpose(0,2,1)*wt[ids,None,None]
                mat[ids]+=jt@J
                vec[ids]+=np.einsum('nij,nj->ni',jt,scaled)
                support[ids]+=wt[ids]
            delta=np.linalg.solve(mat,vec[:,:,None])[:,:,0]
            pose=fit(self.v,self.rig,xyz+delta,np.minimum(1,support*.8),pose)
        return pose


def main():
    bind()
    tr=Tracker()
    bindpose=rest_pose(tr.rig['center'])
    motion=WORK/'motion'
    motion.mkdir(exist_ok=True)
    for k in (1,0,2,3,4):
        end=INTERVALS[k]
        poses=np.empty((end+1,*bindpose.shape))
        if k==1:
            poses[8]=bindpose
            ranges=[range(7,-1,-1),range(9,end+1)]
        else:
            prev=np.load(motion/'1.npy')[0]
            poses[0]=tr.align(prev,tr.frame(1,0),tr.frame(k,0),3)
            ranges=[range(1,end+1)]
        for walk in ranges:
            initial=walk.start-walk.step
            prev=tr.frame(k,initial)
            for j in walk:
                dest=tr.frame(k,j)
                poses[j]=tr.align(poses[j-walk.step],prev,dest,2)
                prev=dest
                if j%8==0: print('fit',k,j,flush=True)
        if k in (0,1):
            # Published loop durations include the repeated boundary pose.
            poses[-1]=poses[0]
        np.save(motion/f'{k}.npy',poses)
        print('complete',k,flush=True)


if __name__=='__main__':
    main()
