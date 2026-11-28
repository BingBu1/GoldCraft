"""Triangulate visible fiducials on a temporary calibration material in the public viewer."""
import sys
from functools import lru_cache
from itertools import combinations
import cv2
import numpy as np
from scipy.cluster.hierarchy import fclusterdata
from scipy.spatial import cKDTree
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import dijkstra
from skimage.measure import label
from views import *
from refine import SurfaceFit

MARK=REF/'markers'
IDS=[0,1,2,3,4,6,*range(7,17)]


def dots(image):
    y,x=np.where(image[:,:,0]>12)
    if not len(x):
        return {}
    x0,x1=x.min(),x.max()+1
    y0,y1=y.min(),y.max()+1
    rgb=image[y0:y1,x0:x1].astype(np.int16)
    digit=np.rint((rgb-30)/21).astype(np.int16)
    valid=(np.abs(rgb-30-21*digit).max(2)<4)&(digit.min(2)>=0)&(digit.max(2)<10)
    codes=digit[:,:,0]+10*digit[:,:,1]+100*digit[:,:,2]
    labels=label(np.where(valid,codes+1,0),background=0,connectivity=2)
    sy,sx=np.where(labels)
    ids=labels[sy,sx]
    if not len(ids):
        return {}
    area=np.bincount(ids)
    sid=np.flatnonzero(area>=4)
    cx=x0+np.bincount(ids,sx)[sid]/area[sid]
    cy=y0+np.bincount(ids,sy)[sid]/area[sid]
    code=np.rint(np.bincount(ids,codes[sy,sx])[sid]/area[sid]).astype(int)
    grouped={}
    for key,px,py,count in zip(code,cx,cy,area[sid]):
        grouped.setdefault(key,[]).append((px,py,count))
    return {k:sorted(rows,key=lambda r:-r[2])[:7] for k,rows in grouped.items()}


def bind_landmarks():
    mesh=np.load(WORK/'surface.npz')
    v,f=mesh['v'],mesh['f']
    lookup={}
    for path in sorted(MARK.glob('rest_*.json')):
        if 'hold' in path.stem or path.stem=='rest_v5':
            continue
        cam=Camera.read(path)
        tri,bar,_=buffer(cam.project(v),f,cam.width,cam.height)
        for code,rows in dots(rgb_file(path.with_suffix('.png'))).items():
            for x,y,area in rows:
                a,b=round(y),round(x)
                if tri[a,b]<0 or area<6: continue
                pt=bar[a,b]@v[f[tri[a,b]]]
                lookup.setdefault(code,[]).append(pt)
    marks,codes=[],[]
    for code,samples in lookup.items():
        if len(samples)<3: continue
        pts=np.array(samples)
        group=fclusterdata(pts,.12,criterion='distance',method='average')
        for part in np.unique(group):
            sel=pts[group==part]
            if len(sel)>=3:
                marks.append(np.median(sel,axis=0))
                codes.append(code)
    result=np.array(marks)
    np.savez(WORK/'marks.npz',p=result,code=np.array(codes))
    print('landmarks',len(result),'colors',len(set(codes)),flush=True)


class MarkerFit:
    def __init__(self):
        self.fit=SurfaceFit()
        self.v,self.f=self.fit.v,self.fit.f
        data=np.load(WORK/'marks.npz')
        self.rest,self.codes=data['p'],data['code']
        self.cams=[Camera.read(MARK/f'v{i}.json') for i in IDS]
        self.orig=np.stack([c.origin for c in self.cams])
        self.center=np.stack([c.center for c in self.cams])
        self.basis=np.stack([c.basis for c in self.cams])
        self.focal=np.array([c.focal for c in self.cams])
        self.kdt=cKDTree(self.v)
        self.dv,self.iv=self.kdt.query(self.rest,k=5)
        self.mw=1/np.maximum(.008,self.dv)**2
        self.mw/=self.mw.sum(1,keepdims=True)
        edges=np.unique(np.sort(np.c_[self.fit.a,self.fit.b],axis=1),axis=0)
        a,b=edges.T
        graph=coo_matrix((np.linalg.norm(self.v[a]-self.v[b],axis=1),(a,b)),(len(self.v),)*2)
        dist=dijkstra(graph,indices=self.iv[:,0],directed=False).T+self.dv[:,0]
        # One disconnected tiny tooth gets Euclidean neighbors from its own end of the jaw.
        missing=~np.isfinite(dist).any(axis=1)
        dist[missing]=np.linalg.norm(self.v[missing,None,:]-self.rest[None,:,:],axis=2)
        self.nearest=np.argsort(dist,axis=1)[:,:34]
        self.distance=np.take_along_axis(dist,self.nearest,axis=1)
        self.pairs=np.array([p for p in combinations(range(len(self.cams)),2)
                             if abs(self.basis[p[0],2]@self.basis[p[1],2])<.86])

    def predictions(self,xyz):
        return (xyz[self.iv]*self.mw[:,:,None]).sum(1)

    def detect(self,k,j):
        points=np.full((len(self.rest),len(IDS),7,2),1e5,np.float32)
        for t,i in enumerate(IDS):
            groups=dots(rgb_file(MARK/f'm_v{i}_{k}_{j}.png'))
            for n,code in enumerate(self.codes):
                rows=groups.get(code)
                if rows: points[n,t,:len(rows)]=np.array(rows)[:,:2]
        return points

    def rays(self,guess,measured):
        view=np.einsum('vij,mvj->mvi',self.basis,guess[:,None,:]-self.orig)
        pixels=self.center+view[:,:,:2]*self.focal[None,:,None]/view[:,:,2:]
        delta=measured-pixels[:,:,None,:]
        ids=np.argmin((delta**2).sum(3),axis=2)
        take=np.take_along_axis(measured,ids[:,:,None,None],axis=2)[:,:,0]
        valid=take[:,:,0]<10000
        xy=(take-self.center)/self.focal[:,None]
        rays=unit(np.einsum('vji,mvj->mvi',self.basis,np.concatenate([xy,np.ones((*xy.shape[:2],1))],axis=2)))
        return rays,valid

    def recover(self,guess,det):
        rays,valid=self.rays(guess,det)
        a,b=self.pairs.T
        r,s=rays[:,a],rays[:,b]
        cross=np.cross(r,s)
        den=np.maximum(1e-6,(cross**2).sum(2))
        d=self.orig[b]-self.orig[a]
        t=(np.cross(d,s)*cross).sum(2)/den
        u=(np.cross(d,r)*cross).sum(2)/den
        candidates=(self.orig[a]+self.orig[b]+t[:,:,None]*r+u[:,:,None]*s)*.5
        distances=np.zeros(candidates.shape[:2])
        for i in range(len(self.cams)):
            line=candidates-self.orig[i]
            offset=np.linalg.norm(np.cross(line,rays[:,i,None,:]),axis=2)
            distances+=valid[:,i,None]*np.exp(-(offset/.043)**2)
        difference=np.linalg.norm(candidates-guess[:,None,:],axis=2)
        merit=distances-1.15*np.minimum(4,difference)
        merit[~(valid[:,a]&valid[:,b])]=-100
        best=np.argmax(merit,axis=1)
        seed=candidates[np.arange(len(guess)),best]
        seed=np.where((merit.max(1)>1.25)[:,None],seed,guess)
        result=seed
        for _ in range(2):
            rays,valid=self.rays(result,det)
            line=result[:,None,:]-self.orig
            err=np.linalg.norm(np.cross(line,rays),axis=2)
            weight=np.exp(-(err/.060)**2)*valid
            basis=np.eye(3)-rays[:,:,:,None]*rays[:,:,None,:]
            A=(weight[:,:,None,None]*basis).sum(1)+np.eye(3)*.03
            vec=np.einsum('mvij,vj,mv->mi',basis,self.orig,weight)+result*.03
            result=np.linalg.solve(A,vec[:,:,None])[:,:,0]
        confidence=np.clip((weight.sum(1)-1.5)/3,0,1)
        confidence*=np.minimum(1,1.5/np.maximum(.01,np.linalg.norm(result-guess,axis=1)))
        return result,confidence

    def pose(self,marks,confidence,base):
        n=self.nearest
        w=confidence[n]/(.065+self.distance)**3.1
        strength=w.sum(1)
        poor=strength<.001
        w=w/np.maximum(.001,strength[:,None])
        source=self.rest[n]
        target=marks[n]
        orig=(source*w[:,:,None]).sum(1)
        dest=(target*w[:,:,None]).sum(1)
        cov=np.einsum('vki,vkj,vk->vij',source-orig[:,None,:],target-dest[:,None,:],w)
        u,s,vt=np.linalg.svd(cov)
        sign=np.ones((len(base),3))
        sign[:,2]=np.linalg.det(u)*np.linalg.det(vt)
        rot=(vt.transpose(0,2,1)*sign[:,None,:])@u.transpose(0,2,1)
        fitted=np.einsum('vij,vj->vi',rot,self.v-orig)+dest
        fitted[poor]=base[poor]
        return fitted

    def clip(self,k):
        dense=np.load(WORK/f'motion/dense-{k}.npy')
        all_xyz=[]
        previous=None
        previous2=None
        scores=[]
        for j,base in enumerate(dense):
            pred=self.predictions(base)
            if previous is None:
                guess=pred
            else:
                extrap=previous if previous2 is None else previous+.60*(previous-previous2)
                guess=.80*extrap+.20*pred
            det=self.detect(k,j)
            marks,confidence=self.recover(guess,det)
            xyz=self.pose(marks,confidence,base)
            # A second association resolves symmetric or occluded copies of the same dot.
            rigid_guess=self.predictions(xyz)
            marks,confidence=self.recover(rigid_guess,det)
            xyz=self.pose(marks,confidence,base)
            previous2,previous=previous,np.where((confidence>.25)[:,None],marks,self.predictions(xyz))
            obs=self.fit.observe(k,j)
            anchor=xyz.copy()
            for _ in range(2):
                xyz=self.fit.step(xyz,obs,0)*.83+anchor*.17
            all_xyz.append(xyz)
            scores.append(float(np.mean(confidence>.25)))
            if j%8==0: print('markers',k,j,round(scores[-1],3),flush=True)
        if k<2: all_xyz[-1]=all_xyz[0]
        np.save(WORK/f'motion/marked-{k}.npy',all_xyz)
        print('mean reliable',round(np.mean(scores),3),flush=True)


if __name__=='__main__':
    if sys.argv[1]=='bind':
        bind_landmarks()
    else:
        MarkerFit().clip(int(sys.argv[1]))
