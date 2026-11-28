"""Remove low-error same-bone edges while preserving the photographed UV seams and joints."""
from collections import defaultdict
import numpy as np
from views import unit


def reduce(v,faces,uv,materials,bones,target=3850):
    faces,uv,materials=faces.copy(),uv.copy(),materials.copy()
    for _ in range(35):
        if len(faces)<=target:
            break
        positions=v[faces]
        plane=np.cross(positions[:,1]-positions[:,0],positions[:,2]-positions[:,0])
        areas=np.linalg.norm(plane,axis=1)
        normal=unit(plane)
        plane=np.column_stack([normal,-(normal*positions[:,0]).sum(1)])
        quad=np.zeros((len(v),4,4))
        metrics=plane[:,:,None]*plane[:,None,:]*areas[:,None,None]
        paths=defaultdict(list)
        for j in range(3):
            np.add.at(quad,faces[:,j],metrics)
            for i,idx in enumerate(faces[:,j]):
                paths[idx].append((i,j))
        mapped=np.zeros((len(v),2))
        single=np.zeros(len(v),bool)
        for idx,corners in paths.items():
            fi,cj=np.array(corners).T
            coords=uv[fi,cj]
            single[idx]=np.all(materials[fi]==materials[fi[0]]) and np.all(np.abs(coords-coords[0])<1e-5)
            mapped[idx]=coords[0]
        edges,count=np.unique(np.sort(np.concatenate([faces[:,[0,1]],faces[:,[1,2]],faces[:,[0,2]]]),axis=1),
                              axis=0,return_counts=True)
        a,b=edges.T
        feasible=(count==2)&single[a]&single[b]&(bones[a]==bones[b])
        a,b=a[feasible],b[feasible]
        if not len(a):
            break
        q=quad[a]+quad[b]
        def error(indices):
            p=np.c_[v[indices],np.ones(len(indices))]
            return np.einsum('ei,eij,ej->e',p,q,p)
        ca,cb=error(a),error(b)
        keep=np.where(ca<cb,a,b)
        drop=np.where(ca<cb,b,a)
        distance=((v[a]-v[b])**2).sum(1)
        cost=np.minimum(ca,cb)+.00008*distance
        remap=np.arange(len(v))
        locked=np.zeros(len(v),bool)
        changed=0
        for i in np.argsort(cost):
            src,dst=drop[i],keep[i]
            if locked[src] or locked[dst]:
                continue
            around=np.array([j for j,_ in paths[src]])
            adjacent=faces[around]
            active=~np.any(adjacent==dst,axis=1)
            if not active.any(): continue
            updated=np.where(adjacent[active]==src,dst,adjacent[active])
            patch=v[updated]
            new=np.cross(patch[:,1]-patch[:,0],patch[:,2]-patch[:,0])
            if np.any((unit(new)*normal[around[active]]).sum(1)<.85): continue
            if np.any(np.linalg.norm(new,axis=1)<areas[around[active]]*.24): continue
            remap[src]=dst
            locked[adjacent.ravel()]=True
            locked[faces[[j for j,_ in paths[dst]]].ravel()]=True
            changed+=1
            if len(faces)-2*changed<=target: break
        if changed==0:
            break
        out=remap[faces]
        changed_corner=out!=faces
        uv[changed_corner]=mapped[out[changed_corner]]
        mask=(out[:,0]!=out[:,1])&(out[:,1]!=out[:,2])&(out[:,0]!=out[:,2])
        faces,uv,materials=out[mask],uv[mask],materials[mask]
    return faces,uv,materials
