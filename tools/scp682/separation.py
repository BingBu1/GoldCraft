"""Resolve nonlocal skin contacts at Studio subframes while keeping adjacent keys continuous."""
import numpy as np
from scipy import sparse
from scipy.spatial.transform import Rotation
from scipy.sparse.linalg import spsolve
from intersections import crossings
from views import unit


def interpolate(keys,parents,steps):
    rotation,position=keys[:,:,:,:3],keys[:,:,:,3]
    local=rotation.copy()
    local_p=position.copy()
    inverse=rotation[:,parents[1:]].swapaxes(-1,-2)
    local[:,1:]=inverse@rotation[:,1:]
    local_p[:,1:]=np.einsum('fbij,fbj->fbi',inverse,position[:,1:]-position[:,parents[1:]])
    delta=local[:-1].swapaxes(-1,-2)@local[1:]
    vectors=Rotation.from_matrix(delta.reshape(-1,3,3)).as_rotvec().reshape(*delta.shape[:2],3)
    result=[]
    for j in range(steps):
        w=j/steps
        increment=Rotation.from_rotvec((vectors*w).reshape(-1,3)).as_matrix().reshape(delta.shape)
        r=local[:-1]@increment
        p=(1-w)*local_p[:-1]+w*local_p[1:]
        for b,par in enumerate(parents[1:],1):
            p[:,b]=p[:,par]+np.einsum('fij,fj->fi',r[:,par],p[:,b])
            r[:,b]=r[:,par]@r[:,b]
        result.append(np.concatenate([r,p[:,:,:,None]],axis=3))
    return np.concatenate([np.stack(result,1).reshape(-1,*keys.shape[1:]),keys[-1:]])


def separate_axes(v,xyz,faces,pairs,bind,rot,margin):
    """Choose a separating plane and build its currently violated vertex inequalities."""
    a,b=faces[pairs[:,0]],faces[pairs[:,1]]
    p,q=xyz[a],xyz[b]
    e=np.roll(p,-1,axis=1)-p
    f=np.roll(q,-1,axis=1)-q
    dr=v[b].mean(1)-v[a].mean(1)
    common=(rot[bind[a]].mean(1)+rot[bind[b]].mean(1))*.5
    direction=unit(np.einsum('pij,pj->pi',common,dr))
    axes=np.concatenate([np.cross(e[:,0],e[:,1])[:,None],np.cross(f[:,0],f[:,1])[:,None],
                         np.cross(e[:,:,None],f[:,None,:]).reshape(-1,9,3),
                         direction[:,None],(q.mean(1)-p.mean(1))[:,None]],axis=1)
    axes=unit(axes)
    axes=np.concatenate([axes,-axes],axis=1)
    distances=q[:,None,:,:]-p[:,:,None,:]
    gaps=np.einsum('pijv,pnv->pijn',distances,axes)
    same=bind[a][:,:,None]==bind[b][:,None,:]
    impossible=np.any(same[:,:,:,None]&(gaps<.0005),axis=(1,2))
    merit=gaps.min(axis=(1,2))+.009*np.einsum('pni,pi->pn',axes,direction)
    merit-=impossible*3.+(np.linalg.norm(axes,axis=2)<.8)*5.
    idx=merit.argmax(axis=1)
    chosen=axes[np.arange(len(a)),idx]
    gap=np.einsum('pijv,pv->pij',distances,chosen)
    # Small working clearance protects the rounded and reduced runtime surface.
    i,j,k=np.where((gap<margin)&~same)
    return bind[a[i,j]],bind[b[i,k]],chosen[i],margin-gap[i,j,k]


def correction_metric(bind,faces,frames,loop):
    bones=int(bind.max()+1)
    edges=bind[faces[:,[0,1,0,2,1,2]].reshape(-1,2)]
    edges=np.sort(edges[edges[:,0]!=edges[:,1]],axis=1)
    pairs,frequency=np.unique(edges,axis=0,return_counts=True)
    a,b=pairs.T
    stiffness=.22*np.sqrt(frequency)
    space=np.diag(np.ones(bones)+np.bincount(a,stiffness,bones)+np.bincount(b,stiffness,bones))
    np.add.at(space,(a,b),-stiffness)
    np.add.at(space,(b,a),-stiffness)
    d=np.diff(np.eye(frames),axis=0)
    if loop:
        d=np.vstack([d,np.eye(frames)[0]-np.eye(frames)[-1]])
    time=d.T@d
    return sparse.kron(sparse.kronsum(space,4.*time+1.5*time@time),sparse.eye(3),format='csc')


def pose_contacts(v,f,bind,centers,pose,minimum,floor=.003,margin=.014):
    r,p=pose[:,:,:3],pose[:,:,3]
    xyz=np.einsum('vij,vj->vi',r[bind],v-centers[bind])+p[bind]
    pairs=crossings(v,xyz,f,minimum)
    if len(pairs):
        a,b,n,depth=separate_axes(v,xyz,f,pairs,bind,r,margin)
    else:
        a,b,n,depth=np.empty(0,int),np.empty(0,int),np.empty((0,3)),np.empty(0)
    heights=np.full(len(centers),np.inf)
    np.minimum.at(heights,bind,xyz[:,2])
    ground=np.flatnonzero(heights<floor)
    a=np.r_[a,np.full(len(ground),-1,int)]
    b=np.r_[b,ground]
    n=np.vstack([n,np.tile([0.,0.,1.],(len(ground),1))])
    return a,b,n,np.r_[depth,.005-heights[ground]],len(pairs)


def rows_for_keys(a,b,n,frame,phase,frames,bones,loop):
    following=(frame+1)%frames if loop else min(frame+1,frames-1)
    frame=frame%frames if loop else min(frame,frames-1)
    rr,cc,dd=[],[],[]
    for key,weight in ((frame,1-phase),(following,phase)):
        if weight==0:continue
        for indices,sign in ((a,-1),(b,1)):
            valid=np.flatnonzero(indices>=0)
            rr.extend(np.repeat(valid,3))
            cc.extend(((key*bones+indices[valid,None])*3+np.arange(3)).ravel())
            dd.extend((n[valid]*(sign*weight)).ravel())
    return sparse.csr_matrix((dd,(rr,cc)),shape=(len(a),frames*bones*3))


def relax_seams(v,f,bind,centers,keys,loop,penalty):
    """Restore cross-joint edge lengths before preventing intersections of larger patches."""
    frames=len(keys)-int(loop)
    bones=len(centers)
    edges=np.unique(np.sort(f[:,[0,1,0,2,1,2]].reshape(-1,2),axis=1),axis=0)
    a,b=edges[bind[edges[:,0]]!=bind[edges[:,1]]].T
    rest=np.linalg.norm(v[b]-v[a],axis=1)
    offsets=v-centers[bind]
    for _ in range(4):
        rows=[]
        targets=[]
        for j,pose in enumerate(keys[:frames]):
            points=np.einsum('vij,vj->vi',pose[bind,:,:3],offsets)+pose[bind,:,3]
            delta=points[b]-points[a]
            length=np.linalg.norm(delta,axis=1)
            stretch=length-1.70*rest-.020
            mask=stretch>0.
            if mask.any():
                rows.append(rows_for_keys(bind[b[mask]],bind[a[mask]],unit(delta[mask]),
                                          j,0.,frames,bones,loop))
                targets.append(stretch[mask])
        if not rows:break
        jac=sparse.vstack(rows,format='csr')
        delta=spsolve((penalty+2.6*(jac.T@jac)).tocsc(),2.6*(jac.T@np.concatenate(targets)))
        keys[:frames,:,:,3]+=.60*delta.reshape(frames,bones,3)
        if loop:
            keys[-1]=keys[0]


def elbow_clearance(keys,names,centers,side):
    """Keep the bracing upper arm outside the rib cage during a cross-body strike."""
    from scipy.ndimage import gaussian_filter1d
    prefix=f'Fore_{side}_'
    sign=1. if side=='R' else -1.
    arms=np.array([i for i,name in enumerate(names) if name.startswith(prefix+'Arm')])
    wrists=np.array([i for i,name in enumerate(names) if name.startswith(prefix+'Wrist')])
    claws=np.array([i for i,name in enumerate(names) if name.startswith(prefix+'Claw')])
    torso=np.array([i for i,name in enumerate(names) if name.startswith(('Chest','Shoulders'))])
    nearby=torso[np.linalg.norm(centers[torso]-centers[arms].mean(0),axis=1).argmin()]
    direction=keys[:,nearby,:,0]*sign
    position=keys[:,:,:,3]
    lateral=np.einsum('fi,fi->f',position[:,arms].mean(1)-position[:,nearby],direction)
    rest=sign*(centers[arms,0].mean()-centers[nearby,0])
    lift=gaussian_filter1d(np.maximum(0.,.092+rest-lateral),.8,mode='nearest')
    for indices,scale in ((arms,.70),(wrists,1.15),(claws,1.0)):
        position[:,indices]+=scale*(lift[:,None]*direction)[:,None,:]


def resolve_clip(v,f,bind,centers,keys,parents,k):
    loop=k<2
    frames=len(keys)-int(loop)
    bones=len(centers)
    penalty=correction_metric(bind,f,frames,loop)
    relax_seams(v,f,bind,centers,keys,loop,penalty)
    steps=4
    for cycle in range(75):
        samples=interpolate(keys,parents,steps)
        constraints=[]
        pushes=[]
        contacts=0
        worst=0.
        for s,pose in enumerate(samples[:-1] if loop else samples):
            a,b,n,d,hits=pose_contacts(v,f,bind,centers,pose,.092)
            contacts+=hits
            if len(d):
                constraints.append(rows_for_keys(a,b,n,s//steps,(s%steps)/steps,frames,bones,loop))
                pushes.append(d)
                worst=max(worst,d.max())
        if cycle%4==0 or (not constraints):
            print('skin contacts',k,cycle,contacts,'clearance correction',round(worst,3),flush=True)
        if not constraints:
            if steps==32:break
            steps=16 if steps==4 else 32
            continue
        jac=sparse.vstack(constraints,format='csr')
        need=np.concatenate(pushes)
        # Penalized unilateral constraints. Recompute directions after each step;
        # solve all contacting frames together so isolated contacts never pop.
        increment=np.zeros(frames*bones*3)
        for _ in range(3):
            active=jac@increment<need
            if not active.any(): break
            selected=jac[active]
            mat=penalty+180.*(selected.T@selected)
            increment=spsolve(mat.tocsc(),180.*(selected.T@need[active]))
        shift=increment.reshape(frames,bones,3)
        length=np.linalg.norm(shift,axis=2,keepdims=True)
        shift*=np.minimum(.90,.075/np.maximum(.001,length))
        keys[:frames,:,:,3]+=shift
        if loop:
            keys[-1]=keys[0]


def resolve(v,f,bind,centers,clips,parents,names):
    for k,keys in enumerate(clips):
        if k in (2,3):
            elbow_clearance(keys,names,centers,'R' if k==3 else 'L')
        resolve_clip(v,f,bind,centers,keys,parents,k)
