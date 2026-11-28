"""GoldSrc single-influence anatomy, fitted jointly across camera observations."""
import numpy as np
from views import WORK


def skin(v, rig, pose):
    bone, center = rig['bone'],rig['center']
    return np.einsum('vij,vj->vi',pose[bone,:,:3],v-center[bone])+pose[bone,:,3]


def bind():
    g=np.load(WORK/'surface.npz')
    v,f=g['v'],g['f']
    tags=[]
    for x,y,z in v:
        if y < -1.1:
            number=int(np.clip((-1.1-y)/.34,0,6))
            tag=f'Tail_{number+1:02}'
        elif y < -.2 and z < .80 and abs(x) > .28:
            tag=('Hind_L' if x<0 else 'Hind_R')+('_Claw' if z<.18 else '_Shin' if z<.49 else '_Thigh')
        elif .15 < y < 1.55 and z < .85 and (abs(x)>.29 or z<.32):
            tag=('Fore_L' if x<0 else 'Fore_R')+('_Claw' if z<.20 else '_Wrist' if z<.46 else '_Arm')
        elif y < -.52:
            tag='Pelvis'
        elif y < -.02:
            tag='Abdomen'
        elif y < .50:
            tag='Chest'
        elif y < .89:
            tag='Shoulders'
        elif y < 1.32:
            tag='Neck'
        elif (y>1.50 and z<(.797+.178*(y-1.5))):
            tag='Mandible'
        else:
            tag='Skull'
        tags.append(tag)
    names=['Pelvis','Abdomen','Chest','Shoulders','Neck','Skull','Mandible']
    names += [f'Tail_{j:02}' for j in range(1,8)]
    parents=[-1,0,1,2,3,4,5,0,7,8,9,10,11,12]
    for side in ['L','R']:
        for limb,sections,base in [('Hind',['Thigh','Shin','Claw'],0),('Fore',['Arm','Wrist','Claw'],3)]:
            for name in sections:
                parents.append(base)
                base=len(names)
                names.append(f'{limb}_{side}_{name}')
    labels=np.array([names.index(t) for t in tags])
    centers=np.stack([v[labels==i].mean(0) for i in range(len(names))])
    edges=np.unique(np.sort(np.concatenate([f[:,[0,1]],f[:,[1,2]],f[:,[0,2]]]),axis=1),axis=0)
    # Match the two rigid pieces at their shared joint, while allowing the surface to bend.
    seams=[]
    for i,parent in enumerate(parents):
        if parent<0:
            continue
        mask=((labels[edges[:,0]]==i)&(labels[edges[:,1]]==parent))|((labels[edges[:,1]]==i)&(labels[edges[:,0]]==parent))
        points=v[edges[mask]].mean(1)
        joint=points.mean(0) if len(points) else (centers[i]+centers[parent])*.5
        if names[i]=='Mandible':
            joint=np.array([.34,1.55,.87])
        seams.append((i,parent,*joint))
    np.savez(WORK/'rig.npz',bone=labels,center=centers,names=names,parents=parents,seams=seams)


def rest_pose(center):
    return np.concatenate([np.tile(np.eye(3),(len(center),1,1)),center[:,:,None]],axis=2)


def fit(v,rig,targets,weights,previous):
    center,bone=rig['center'],rig['bone']
    count=len(center)
    left,right=rig['seams'][:,:2].astype(int).T
    pivots=rig['seams'][:,2:]
    pose=previous.copy()
    old=skin(v,rig,previous)
    masses=np.bincount(bone,minlength=count)
    reg=.09
    def transform(indices, pts):
        return np.einsum('nij,nj->ni',pose[indices,:,:3],pts-center[indices])+pose[indices,:,3]
    for iteration in range(6):
        left_p,right_p=transform(left,pivots),transform(right,pivots)
        previous_guess=skin(v,rig,pose)
        error=np.linalg.norm(previous_guess-targets,axis=1)
        robust=weights*np.minimum(1,(.16 if iteration else .40)/np.maximum(.0001,error))
        w=robust+reg
        joint_w=np.minimum(masses[left],masses[right])*.95
        all_w=np.r_[w,joint_w,joint_w]
        ids=np.r_[bone,left,right]
        src=np.vstack([v,pivots,pivots])-center[ids]
        dest=np.vstack([(targets*robust[:,None]+old*reg)/w[:,None],
                        (left_p+right_p)*.5,(left_p+right_p)*.5])
        total=np.bincount(ids,all_w,minlength=count)
        def grouped(values):
            return np.stack([np.bincount(ids,values[:,k]*all_w,minlength=count) for k in range(values.shape[1])],1)
        a=grouped(src)/total[:,None]
        b=grouped(dest)/total[:,None]
        cov=grouped(((src-a[ids])[:,:,None]*(dest-b[ids])[:,None,:]).reshape(-1,9)).reshape(-1,3,3)
        U,_,Vh=np.linalg.svd(cov)
        correction=np.ones((count,3))
        correction[:,2]=np.linalg.det(Vh)*np.linalg.det(U)
        rot=(Vh.transpose(0,2,1)*correction[:,None,:])@U.transpose(0,2,1)
        pose[:,:,:3]=rot
        pose[:,:,3]=b-np.einsum('nij,nj->ni',rot,a)
    return pose


if __name__=='__main__':
    bind()
