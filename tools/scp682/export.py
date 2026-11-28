"""Compress sampled skinned surfaces to a single-weight GoldSrc skeleton and compile Studio v10."""
import os
import subprocess
import sys
from pathlib import Path
import numpy as np
from scipy.cluster.vq import kmeans2
from views import *
from foley import EVENTS
from reduce import reduce
from dentition import build as build_teeth
sys.path.append(str(ROOT/'tools/siren_head_motion'))
from studio import to_angles

UNITS=39.37007874
AXES=np.array([[0,1,0],[-1,0,0],[0,0,1]],dtype=float)
ACT=('ACT_RUN','ACT_IDLE','ACT_MELEE_ATTACK1','ACT_MELEE_ATTACK2','ACT_DIESIMPLE')


def absolute(reference,animated,labels):
    nb=labels.max()+1
    poses=np.empty((len(animated),nb,3,4))
    centers=np.stack([reference[labels==b].mean(0) for b in range(nb)])
    for b,c in enumerate(centers):
        select=labels==b
        deform=animated[:,select]
        mean=deform.mean(1)
        cov=np.einsum('vi,fvj->fij',reference[select]-c,deform-mean[:,None,:])
        u,_,vt=np.linalg.svd(cov)
        signs=np.ones((len(deform),3))
        signs[:,2]=np.linalg.det(u)*np.linalg.det(vt)
        poses[:,b,:,:3]=(vt.transpose(0,2,1)*signs[:,None,:])@u.transpose(0,2,1)
        poses[:,b,:,3]=mean
    return centers,poses


def compress(v, motions, rig):
    allposes=np.concatenate(motions)
    examples=allposes[::4]
    initial=rig['bone']
    labels=np.zeros(len(v),int)
    rng=np.random.default_rng(682)
    names,parents,owners=[],[],[]
    allocation=np.maximum(1,np.rint(np.sqrt(np.bincount(initial))*.34).astype(int))
    for i,name in enumerate(rig['names']):
        select=initial==i
        group=v[select]
        # Include observed nonrigid deformation in the initial split and retain anatomical locality.
        desc=np.concatenate([group*2.0,(examples[:,select]-examples[:,select].mean(1,keepdims=True)).
                             transpose(1,0,2).reshape(len(group),-1)/np.sqrt(len(examples))],axis=1)
        num=min(allocation[i],max(1,len(group)//12))
        if num>1:
            _,cluster=kmeans2(desc,num,iter=25,minit='++',rng=rng)
            used,counts=np.unique(cluster,return_counts=True)
            order=used[np.argsort(-counts)]
            cluster=np.array([np.flatnonzero(order==j)[0] for j in cluster])
        else:
            cluster=np.zeros(len(group),int)
        base=len(names)
        labels[select]=base+cluster
        for j in range(cluster.max()+1):
            names.append(str(name) if j==0 else f'{name}_{j+1}')
            parents.append((-1 if i==0 else names.index(str(rig['names'][rig['parents'][i]]))) if j==0 else base)
            owners.append(i)
    owners=np.asarray(owners)
    for _ in range(4):
        centers,poses=absolute(v,examples,labels)
        new=labels.copy()
        for i in np.unique(initial):
            verts=np.flatnonzero(initial==i)
            choices=np.flatnonzero(owners==i)
            cost=[]
            for b in choices:
                predicted=np.einsum('fij,vj->fvi',poses[:,b,:,:3],v[verts]-centers[b])+poses[:,b,None,:,3]
                err=((predicted-examples[:,verts])**2).sum(2).mean(0)
                cost.append(err+.014*((v[verts]-centers[b])**2).sum(1))
            target=choices[np.argmin(cost,axis=0)]
            if all((target==b).sum()>=5 for b in choices):
                new[verts]=target
        labels=new
    # Reassign skin near the jaw hinge and folded joints using their motion across clips.
    # Anatomical labels provide the seed. Shared influence is restricted to nearby pieces.
    for _ in range(3):
        centers,sample=absolute(v,examples,labels)
        costs=[]
        for b,c in enumerate(centers):
            dist=((v-c)**2).sum(1)
            pred=np.einsum('fij,vj->fvi',sample[:,b,:,:3],v-c)+sample[:,b,None,:,3]
            residual=((pred-examples)**2).sum(2).mean(0)
            costs.append(residual+.008*dist+np.maximum(0,dist-.47)**2)
        new=np.argmin(costs,axis=0)
        counts=np.bincount(new,minlength=len(centers))
        for b in np.flatnonzero(counts<6):
            new[labels==b]=b
        labels=new
    centers,poses=absolute(v,allposes,labels)
    e=np.einsum('fvij,vj->fvi',poses[:,labels,:,:3],v-centers[labels])+poses[:,:,:,3][:,labels]-allposes
    print('skeleton',len(names),'mean / p95 error in world units',np.mean(np.linalg.norm(e,axis=2)),np.quantile(np.linalg.norm(e,axis=2),.95))
    return names,np.array(parents),labels,centers,np.split(poses,np.cumsum(list(map(len,motions)))[:-1])


def rows(names,parents):
    return 'version 1\nnodes\n'+''.join(f'{i} "{n}" {p}\n' for i,(n,p) in enumerate(zip(names,parents)))+'end\n'


def channels(pose,parents):
    rot=AXES@pose[:,:,:3]@AXES.T
    pos=(pose[:,:,3]@AXES.T)*UNITS
    local_rot=rot.copy()
    local_pos=pos.copy()
    for b,p in enumerate(parents):
        if p>=0:
            local_rot[b]=rot[p].T@rot[b]
            local_pos[b]=rot[p].T@(pos[b]-pos[p])
    return np.column_stack([local_pos,to_angles(local_rot)])


def skeleton(nodes,poses,parents):
    lines=[nodes,'skeleton\n']
    for j,p in enumerate(poses):
        lines.append(f'time {j}\n')
        for b,row in enumerate(channels(p,parents)):
            lines.append(str(b)+' '+' '.join(f'{num:.7f}' for num in row)+'\n')
    return ''.join(lines)+'end\n'


def emit():
    mesh=np.load(WORK/'surface.npz')
    v,f,n=mesh['v'],mesh['f'],mesh['n']
    tex=np.load(WORK/'uv.npz')
    uv,mat=tex['uv'].copy(),tex['material']
    uv[:,:,1]=1-uv[:,:,1]
    rig=dict(np.load(WORK/'rig.npz'))
    data=[np.load(WORK/f'motion/outline-{i}.npy') for i in range(5)]
    names,parents,bones,center,animations=compress(v,data,rig)
    stage=WORK/'compile'
    teeth,teeth_faces,teeth_uv,teeth_bones=build_teeth(v,f,bones,rig['names'][rig['bone']],stage)
    teeth_uv[:,:,1]=1-teeth_uv[:,:,1]
    f,uv,mat=reduce(v,f,uv,mat,bones)
    f=np.vstack([f,teeth_faces+len(v)])
    n=np.vstack([n,normals(teeth,teeth_faces)])
    v=np.vstack([v,teeth])
    bones=np.r_[bones,teeth_bones]
    uv=np.concatenate([uv,teeth_uv])
    mat=np.r_[mat,np.full(len(teeth_faces),2)]
    nodes=rows(names,parents)
    rest=np.concatenate([np.tile(np.eye(3),(len(names),1,1)),center[:,:,None]],axis=2)
    intro=skeleton(nodes,[rest],parents)+'triangles\n'
    xyz=v@AXES.T*UNITS
    norm=n@AXES.T
    stage=WORK/'compile'
    qc=['$modelname "scp682.mdl"','$cd "."','$cdtexture "."','$scale 1','$origin 0 0 0 -90']
    # Each submodel stays below the original per-submodel Studio vertex budget.
    groups=np.digitize(v[f,1].mean(1),[-.25,.95])
    for part in np.unique(groups):
        text=[intro]
        for i in np.flatnonzero(groups==part):
            text.append(('skin0.bmp','skin1.bmp','teeth.bmp')[mat[i]]+'\n')
            for corner,vert in enumerate(f[i]):
                values=(*xyz[vert],*norm[vert],*uv[i,corner])
                text.append(str(bones[vert])+' '+' '.join(f'{x:.7f}' for x in values)+'\n')
        (stage/f'part{part}.smd').write_text(''.join(text)+'end\n')
        qc.append(f'$body part{part} "part{part}"')
    for key in ('Pelvis','Abdomen','Chest','Neck','Skull','Mandible','Fore_L_Arm','Fore_R_Arm',
                'Hind_L_Thigh','Hind_R_Thigh','Tail_02','Tail_05'):
        group=list(rig['names']).index(key)
        local=(mesh['v'][rig['bone']==group]-center[names.index(key)])@AXES.T*UNITS
        lims=np.r_[np.quantile(local,.05,axis=0),np.quantile(local,.95,axis=0)]
        qc.append(f'$hbox {1 if key in ("Skull","Mandible") else 2} "{key}" '+' '.join(f'{q:.3f}' for q in lims))
    mouth=(np.array([.65,2.49,.96])-center[names.index('Skull')])@AXES.T*UNITS
    qc.append('$attachment 0 "Skull" '+' '.join(f'{q:.3f}' for q in mouth))
    for k,name in enumerate(CLIPS):
        (stage/f'clip{k}.smd').write_text(skeleton(nodes,animations[k],parents))
        loop=' loop' if k<2 else ''
        event=' '.join(f'{{ event {code} {frame} "scp682/{sample}.wav" }}' for frame,code,sample in EVENTS[k])
        qc.append(f'$sequence "{name}" "clip{k}" fps 24{loop} {ACT[k]} 1 {event}')
    (stage/'reptile.qc').write_text('\n'.join(qc)+'\n',encoding='ascii')
    compiler=os.environ.get('STUDIOMDL','studiomdl')
    run=subprocess.run([compiler,'reptile.qc'],cwd=stage,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (WORK/'studiomdl.log').write_bytes(run.stdout)
    if run.returncode or not (stage/'scp682.mdl').is_file():
        raise RuntimeError(run.stdout.decode(errors='replace')[-3000:])


if __name__=='__main__':
    emit()
