"""Fit continuous anatomical motion and bake smooth joint influences to GoldSrc rigid bones."""
import numpy as np
from scipy import sparse
from scipy.cluster.vq import kmeans2
from scipy.ndimage import gaussian_filter1d
from scipy.spatial.transform import Rotation
from views import unit
from contact import jaw_components,gape_frame,stitch,plant


def matrices_from_vectors(vectors):
    return Rotation.from_rotvec(vectors.reshape(-1,3)).as_matrix().reshape(*vectors.shape[:-1],3,3)


def vectors_from_matrices(matrices):
    return Rotation.from_matrix(matrices.reshape(-1,3,3)).as_rotvec().reshape(*matrices.shape[:-2],3)


def closest_rotations(cov):
    left,_,right=np.linalg.svd(cov)
    sign=np.ones(left.shape[:-1])
    sign[...,2]=np.linalg.det(left)*np.linalg.det(right)
    return (left*sign[...,None,:])@right


def smooth(track,sigma,loop):
    if loop:
        filtered=gaussian_filter1d(track[:-1],sigma,axis=0,mode='wrap')
        return np.concatenate([filtered,filtered[:1]])
    return gaussian_filter1d(track,sigma,axis=0,mode='nearest')


def skin_layout(v,faces,rig):
    labels=rig['bone'].copy()
    names=list(rig['names'])
    x,y,z=v.T
    # The outstretched toes reach past the hinge in plan view. Height separates
    # them from the jaws, which are at least half a scene unit off the ground.
    for side,sign in [('L',-1),('R',1)]:
        toes=(y>.4)&(z<.20)&(sign*x>0)
        labels[toes]=names.index(f'Fore_{side}_Claw')
    head=(y>1.27)&(z>.5)
    lower=(y>1.50)&(z<.797+.178*(y-1.5))
    labels[head&lower]=names.index('Mandible')
    labels[head&~lower]=names.index('Skull')
    edges=np.unique(np.sort(np.concatenate([faces[:,[0,1]],faces[:,[1,2]],faces[:,[0,2]]]),axis=1),axis=0)
    upper,lower=jaw_components(v,edges)
    labels[upper]=names.index('Skull')
    labels[lower]=names.index('Mandible')
    length=np.linalg.norm(v[edges[:,0]]-v[edges[:,1]],axis=1)
    row,col=np.r_[edges,edges[:,::-1]].T
    adjacency=sparse.csr_matrix((np.tile(1/np.maximum(length,.03),2),(row,col)),shape=(len(v),len(v)))
    degree=np.asarray(adjacency.sum(axis=1)).ravel()
    seed=np.eye(len(names))[labels]
    weights=seed.copy()
    for _ in range(7):
        weights=.40*weights+.60*(adjacency@weights)/degree[:,None]
    # Distal jaws and toes are rigid, including the vertices that carry teeth.
    rigid=((y>1.87)&(z>.50))|((z<.14)&(y>-1.06))
    weights[rigid]=seed[rigid]
    weights[weights<.025]=0
    weights/=weights.sum(axis=1,keepdims=True)
    centers=np.stack([v[labels==i].mean(0) for i in range(len(names))])
    pivot=centers.copy()
    for i,p in enumerate(rig['parents']):
        if p<0: continue
        a,b=labels[edges].T
        shared=edges[((a==i)&(b==p))|((a==p)&(b==i))]
        pivot[i]=v[shared].mean(axis=(0,1)) if len(shared) else (centers[i]+centers[p])*.5
    pivot[names.index('Mandible')]=[.34,1.55,.87]
    return names,labels,weights,centers,pivot


def compact_weights(v,weights,names,parents):
    # GoldSrc stores a single bone per vertex. A torso patch must be local on
    # the surface, even when the same blend of two controls occurs on both flanks.
    owner=weights.argmax(1)
    bind=np.empty(len(v),int)
    representatives=[]
    final_names=[]
    final_parents=[]
    base=[]
    principal={'Skull','Mandible'}
    for i,name in enumerate(names):
        ids=np.flatnonzero(owner==i)
        count=max(1,min(8,round(len(ids)/39)))
        if name in principal or 'Claw' in name:
            fixed=(weights[ids,i]>.992)
            if name in principal:
                fixed|=v[ids,1]>1.87
            else:
                fixed|=v[ids,2]<.14
        else:
            fixed=np.zeros(len(ids),bool)
        free=np.flatnonzero(~fixed)
        group=np.zeros(len(ids),int)
        if len(free):
            count=min(count-int(fixed.any()),max(1,len(free)//10))
            if count>1:
                points=ids[free]
                features=np.column_stack([v[points]*1.55,weights[points]*.68])
                _,assignment=kmeans2(features,count,iter=40,minit='++',rng=np.random.default_rng(682+i))
                group[free]=assignment+int(fixed.any())
            else:
                group[free]=int(fixed.any())
        here=len(final_names)
        base.append(here)
        for index,j in enumerate(np.unique(group)):
            vertices=ids[group==j]
            bind[vertices]=len(final_names)
            final_names.append(str(name) if index==0 else f'{name}_{index+1}')
            parent=parents[i]
            final_parents.append((int(base[parent]) if parent>=0 else -1) if index==0 else here)
            representatives.append(weights[vertices].mean(0))
    center=np.stack([v[bind==i].mean(0) for i in range(len(final_names))])
    return final_names,np.array(final_parents),bind,center,np.stack(representatives)


def independent_targets(v,animated,labels,centers):
    rotations=[]
    translations=[]
    for b,center in enumerate(centers):
        points=v[labels==b]
        target=animated[:,labels==b]
        w=np.ones(target.shape[:2])
        for _ in range(3):
            amount=w.sum(1)[:,None]
            a=(points[None,:,:]*w[:,:,None]).sum(1)/amount
            t=(target*w[:,:,None]).sum(1)/amount
            cov=np.einsum('fvi,fvj,fv->fij',target-t[:,None,:],points-a[:,None,:],w)
            rot=closest_rotations(cov)
            fitted=np.einsum('fij,fvj->fvi',rot,points-a[:,None,:])+t[:,None,:]
            residual=np.linalg.norm(fitted-target,axis=2)
            threshold=np.maximum(.042,np.median(residual,axis=1)*1.7)
            w=np.minimum(1.,(threshold[:,None]/np.maximum(.001,residual))**2)
        rotations.append(rot)
        translations.append(t+np.einsum('fij,fj->fi',rot,center-a))
    return np.stack(rotations,1),np.stack(translations,1)


def chain_pose(rotations,root,anchors,parents):
    positions=np.empty((*rotations.shape[:2],3))
    positions[:,0]=root
    for i,p in enumerate(parents[1:],start=1):
        positions[:,i]=positions[:,p]+np.einsum('fij,j->fi',rotations[:,p],anchors[i]-anchors[p])
    return positions


def constrain_local(local,names):
    vectors=vectors_from_matrices(local)
    for i,name in enumerate(names[1:],start=1):
        if name=='Mandible':
            axis=unit(np.array([1.,-.34,0.]))
            angle=np.clip(vectors[:,i]@axis,-1.02,.048)
            vectors[:,i]=angle[:,None]*axis
            continue
        if name.startswith('Tail'):
            limit=.59
        elif '_' in name:
            limit=1.55
        else:
            limit=.86
        size=np.linalg.norm(vectors[:,i],axis=1)
        vectors[:,i]*=np.minimum(1.,limit/np.maximum(size,.001))[:,None]
    return matrices_from_vectors(vectors)


def forward(local,parents):
    global_rot=local.copy()
    for b,p in enumerate(parents[1:],start=1):
        global_rot[:,b]=global_rot[:,p]@local[:,b]
    return global_rot


def skew(v):
    x,y,z=np.moveaxis(v,-1,0)
    zeros=np.zeros_like(x)
    return np.stack([zeros,-z,y,z,zeros,-x,-y,x,zeros],axis=-1).reshape(*v.shape[:-1],3,3)


def fit_chain(v,animated,labels,centers,anchors,names,parents,k):
    frames,bones=len(animated),len(names)
    target,observed=independent_targets(v,animated,labels,centers)
    loop=k in (0,1)
    local=target.copy()
    local[:,1:]=target[:,parents[1:]].swapaxes(-1,-2)@target[:,1:]
    sigma=(.65,.85,.68,.68,1.02)[k]
    local=constrain_local(closest_rotations(smooth(local,sigma,loop)),names)
    root=smooth(observed[:,0],sigma,loop)
    root0=root.copy()
    initial=local.copy()
    # Four independent observation points per body segment constrain both position and
    # orientation. Solve fixed bone lengths and shared joint pivots at every keyframe.
    probes=np.tile(np.eye(3),(bones,1,1))
    for b in range(bones):
        covariance=np.cov(v[labels==b].T)
        values,axes=np.linalg.eigh(covariance)
        probes[b]=(axes*np.maximum(.13,np.sqrt(values))[None,:]).T
    offsets=np.concatenate([np.zeros((bones,1,3)),probes,-probes],axis=1)
    observed_points=observed[:,:,None,:]+np.einsum('fbij,bpj->fbpi',target,offsets)
    observed_points=smooth(observed_points,sigma,loop)
    vertices=(np.arange(bones)[:,None]+np.zeros((bones,7),int)).ravel()
    branch=np.eye(bones,dtype=bool)
    for b,p in enumerate(parents[1:],start=1):
        branch[b]|=branch[p]
    size=(bones+1)*3
    for _ in range(6):
        rotation=forward(local,parents)
        pivots=chain_pose(rotation,root,anchors,parents)
        markers=pivots[:,:,None,:]+np.einsum('fbij,bpj->fbpi',rotation,offsets+(centers-anchors)[:,None,:])
        residual=(observed_points-markers).reshape(frames,-1,3)
        flat=markers.reshape(frames,-1,3)
        jac=np.zeros((frames,bones*7,3,size))
        jac[:,:,:,:3]=np.eye(3)
        for b in range(bones):
            ids=branch[vertices,b]
            jac[:,ids,:,3+3*b:6+3*b]=-skew(flat[:,ids]-pivots[:,b,None,:])
        jac=jac.reshape(frames,-1,size)
        mat=jac.swapaxes(1,2)@jac+np.eye(size)*.31
        vec=(jac.swapaxes(1,2)@residual.reshape(frames,-1,1)).reshape(frames,size)
        # Prefer the measured twist where a nearly straight bone chain admits multiple fits.
        correction=vectors_from_matrices(initial@local.swapaxes(-1,-2))
        correction[:,1:]=np.einsum('fbij,fbj->fbi',rotation[:,parents[1:]],correction[:,1:])
        vec[:,3:]+=.23*correction.reshape(frames,-1)
        vec[:,:3]+=.6*(root0-root)
        delta=np.linalg.solve(mat,vec[:,:,None])[:,:,0]
        root+=.75*delta[:,:3]
        step=delta[:,3:].reshape(frames,bones,3)
        step[:,1:]=np.einsum('fbji,fbj->fbi',rotation[:,parents[1:]],step[:,1:])
        norm=np.linalg.norm(step,axis=2,keepdims=True)
        step*=np.minimum(.72,.20/np.maximum(norm,1e-7))
        local=constrain_local(matrices_from_vectors(step)@local,names)
    local=closest_rotations(smooth(local,.49,loop))
    body=forward(local,parents)
    joints=chain_pose(body,root,anchors,parents)
    for i,name in enumerate(names):
        if name in ('Pelvis','Abdomen','Chest','Shoulders','Neck','Skull'):
            guide=closest_rotations(smooth(target[:,i],.57,loop))
            body[:,i]=closest_rotations(.35*body[:,i]+.65*guide)
    jaw=names.index('Mandible')
    skull=parents[jaw]
    body[:,skull],hinge_rot=gape_frame(v,animated,labels,names,anchors,body[:,skull],smooth,loop)
    body[:,jaw]=body[:,skull]@hinge_rot
    return body,joints


def follow_surface(v,samples,bind,centers,rotation,lock,loop):
    """Use well-constrained surface observations without fitting arbitrary tiny-patch twists."""
    guide=smooth(samples,.48,loop)
    out=rotation.copy()
    for i,center in enumerate(centers):
        ids=bind==i
        if lock[i] or ids.sum()<5:
            continue
        local=v[ids]-center
        observed=guide[:,ids]
        cov=np.einsum('fvi,vj->fij',observed-observed.mean(1,keepdims=True),local)/ids.sum()
        extent=np.mean(np.sum(local*local,axis=1))
        # A near-collinear patch can constrain a direction, but not the roll about it.
        # The prior supplies the unobserved rotation with the anatomical control's frame.
        fitted=closest_rotations(cov+rotation[:,i]*(.002+.18*extent))
        d=vectors_from_matrices(rotation[:,i].swapaxes(-1,-2)@fitted)
        d=smooth(d,.65,loop)
        d*=np.minimum(1.,.43/np.maximum(.001,np.linalg.norm(d,axis=1,keepdims=True)))
        out[:,i]=rotation[:,i]@matrices_from_vectors(d*.57)
    return out


def bake(v,faces,motions,rig):
    names,labels,weights,centers,anchors=skin_layout(v,faces,rig)
    output_names,parents,bind,output_centers,mix=compact_weights(v,weights,names,rig['parents'])
    clips=[]
    for k,samples in enumerate(motions):
        rotations,joints=fit_chain(v,samples,labels,centers,anchors,names,rig['parents'],k)
        # Fit a single rigid transform to the blended control frame at each helper's center.
        rot=closest_rotations(np.einsum('mb,fbij->fmij',mix,rotations))
        locked=np.sum(mix[:,[names.index('Mandible'),names.index('Skull')]],axis=1)>.78
        rot=follow_surface(v,samples,bind,output_centers,rot,locked,k<2)
        offset=output_centers[:,None,:]-anchors[None,:,:]
        pos=np.einsum('mb,fbij,mbj->fmi',mix,rotations,offset)+np.einsum('mb,fbi->fmi',mix,joints)
        pos=stitch(v,faces,samples,labels,names,bind,output_centers,rot,pos,k<2,smooth)
        pos=plant(v,bind,output_centers,rot,pos,k,k<2,smooth)
        poses=np.concatenate([rot,pos[:,:,:,None]],axis=3)
        if k in (0,1): poses[-1]=poses[0]
        clips.append(poses)
    return output_names,parents,bind,output_centers,clips,labels
