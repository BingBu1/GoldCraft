"""Bake one-bone GoldSrc vertices against shared skin boundaries and a supporting floor."""
import numpy as np
from scipy import sparse
from scipy.spatial import cKDTree
from scipy.ndimage import gaussian_filter1d
from scipy.spatial.transform import Rotation


def jaw_components(v,edges):
    """Identify the disconnected tips by mesh topology, including the lower jaw's raised teeth."""
    ids=np.flatnonzero((v[:,1]>2.05)&(v[:,2]>.5))
    a,b=edges[np.isin(edges,ids).all(1)].T
    graph=sparse.coo_matrix((np.ones(len(a)),(np.searchsorted(ids,a),np.searchsorted(ids,b))),
                            shape=(len(ids),len(ids)))
    count,component=sparse.csgraph.connected_components(graph,directed=False)
    groups=[ids[component==i] for i in range(count) if np.count_nonzero(component==i)>10]
    if len(groups)!=2:
        raise ValueError('Expected the two separate distal jaw surfaces')
    return sorted(groups,key=lambda patch:v[patch,2].mean(),reverse=True)


def gape_frame(v,samples,labels,names,pivots,skull,smooth_track,loop):
    """Recover the bite plane from the measured 3D separation of the two muzzle tips."""
    upper,lower=names.index('Skull'),names.index('Mandible')
    hinge=pivots[lower]
    upper_tip=(labels==upper)&(v[:,1]>2.17)
    lower_tip=(labels==lower)&(v[:,1]>2.17)
    a=v[upper_tip].mean(0)-hinge
    b=v[lower_tip].mean(0)-hinge
    axis=np.array([1.,-.34,0.])
    axis/=np.linalg.norm(axis)
    offset=smooth_track(samples[:,lower_tip].mean(1)-samples[:,upper_tip].mean(1),.30,loop)
    length=np.linalg.norm(offset,axis=1)
    # |a - Rot(axis, theta) b| is monotonic on the anatomical opening branch.
    axial=(a@axis)*(b@axis)
    u=a@b-axial
    w=a@np.cross(axis,b)
    cosine=((a@a+b@b-length**2)*.5-axial)/np.hypot(u,w)
    theta=np.clip(np.arctan2(w,u)-np.arccos(np.clip(cosine,-1.,1.)),-1.50,.06)
    hinge_rot=Rotation.from_rotvec(theta[:,None]*axis).as_matrix()
    predicted=np.einsum('fij,fj->fi',skull,np.einsum('fij,j->fi',hinge_rot,b)-a)
    # When closed, a few pixels of error at the lips can reverse the apparent
    # bite plane. Transfer the measured roll only while the snout is open.
    p=predicted/np.maximum(1e-8,np.linalg.norm(predicted,axis=1,keepdims=True))
    q=offset/np.maximum(1e-8,length[:,None])
    cross=np.cross(p,q)
    size=np.linalg.norm(cross,axis=1)
    angle=np.arctan2(size,(p*q).sum(1))
    confidence=np.clip((length-.31)/.39,0.,1.)
    step=cross/np.maximum(1e-8,size[:,None])*(confidence*np.minimum(angle,.90))[:,None]
    return Rotation.from_rotvec(smooth_track(step,.40,loop)).as_matrix()@skull,hinge_rot


def boundary_pairs(v,faces,bind,labels,names):
    edges=np.unique(np.sort(faces[:,[0,1,1,2,2,0]].reshape(-1,2),axis=1),axis=0)
    pairs=cKDTree(v).query_pairs(.32,output_type='ndarray')
    a,b=pairs.T
    region=np.zeros(len(names),int)
    for i,name in enumerate(names):
        if name=='Mandible':
            region[i]=1
        elif name.startswith(('Fore_','Hind_')):
            region[i]=2+['Fore_L','Fore_R','Hind_L','Hind_R'].index(name[:6])
    # Across ribs and dorsal spines, a Euclidean neighbourhood includes the two
    # facets of a thin feature. Keep separate, freely moving limbs and jaws apart.
    valid=(region[labels[a]]==region[labels[b]])&(bind[a]!=bind[b])
    pairs=pairs[valid]
    edges=edges[bind[edges[:,0]]!=bind[edges[:,1]]]
    weights=np.r_[np.full(len(edges),1.3),np.full(len(pairs),.15)]
    a,b=np.r_[edges,pairs].T
    distance=np.linalg.norm(v[a]-v[b],axis=1)
    weights/=np.maximum(.045,distance)
    return a,b,weights


def stitch(v,faces,samples,labels,names,bind,centers,rotations,positions,loop,smooth_track):
    """Solve group translations together; shared edges pull both sides of a skin seam."""
    a,b,w=boundary_pairs(v,faces,bind,labels,names)
    na,nb=bind[a],bind[b]
    count=len(centers)
    matrix=sparse.csr_matrix((np.r_[w,-w],(np.r_[na,nb],np.tile(np.arange(len(w)),2))),
                             shape=(count,len(w)))
    guide=smooth_track(samples,.52,loop)
    regs=np.bincount(bind,minlength=count)*7.5+7.
    target=np.stack([np.median(guide[:,bind==i]-
                    np.einsum('fij,vj->fvi',rotations[:,i],v[bind==i]-centers[i]),axis=1)
                    for i in range(count)],axis=1)
    goal=.93*target+.07*positions
    # The two distal jaw patches supply accurate landmarks even during the fast
    # side roll at the strike. A single socket frame protects the bite when closed.
    for name in ('Skull','Mandible'):
        ids=(labels==names.index(name))&(v[:,1]>2.17)
        for group in np.unique(bind[ids]):
            tip=ids&(bind==group)
            point=smooth_track(samples[:,tip].mean(1),.30,loop)-np.einsum('fij,j->fi',rotations[:,group],v[tip].mean(0)-centers[group])
            goal[:,group]=.95*point+.05*goal[:,group]
            regs[group]+=4000.
    # A seam stays closed when its two bones carry its middle point to the same place.
    middle=(v[a]+v[b])*.5
    both=(np.einsum('feij,ej->fei',rotations[:,nb],middle-centers[nb])-
          np.einsum('feij,ej->fei',rotations[:,na],middle-centers[na]))
    lap=np.diag(regs+np.bincount(na,w,count)+np.bincount(nb,w,count))
    np.add.at(lap,(na,nb),-w)
    np.add.at(lap,(nb,na),-w)
    rhs=np.stack([matrix@d for d in both])+regs[None,:,None]*goal
    return np.linalg.solve(lap,rhs)


def plant(v,bind,centers,rot,pos,k,loop,smooth_track):
    """Lift the rolled body on its support, then correct individual ground-contact patches."""
    relative=v-centers[bind]
    xyz=np.einsum('fvij,vj->fvi',rot[:,bind],relative)+pos[:,bind]
    # Death rolls onto the ribs and spines. Keep the rib cage intact as it settles.
    body=(v[:,1]>-1.15)&(v[:,1]<1.55)&(v[:,2]>.78)
    if k==4:
        lift=np.maximum(0.,-xyz[:,body,2].min(1))
        lift=np.maximum(lift,smooth_track(lift,.75,False))
        pos[:,:,2]+=lift[:,None]
        xyz[:,:,2]+=lift[:,None]
    # Each paw and tail joint can touch down at a different height and time.
    low=np.stack([np.maximum(0.,.003-xyz[:,bind==i,2].min(1)) for i in range(len(centers))],axis=1)
    low=np.maximum(low,smooth_track(low,.72,loop))
    pos[:,:,2]+=low
    return pos
