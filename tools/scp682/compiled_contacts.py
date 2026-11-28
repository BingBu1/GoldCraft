"""Polish the last contacts after StudioMDL has rounded and compressed the animation channels."""
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import spsolve
from review import Compiled,AXES,UNITS,accumulate,matrices
from playback import sample_local
from separation import pose_contacts,rows_for_keys,correction_metric
from views import CLIPS


def scene_poses(world):
    out=np.empty((*world.shape[:-2],3,4))
    out[:,:,:,:3]=AXES.T@world[:,:,:3,:3]@AXES
    out[:,:,:,3]=world[:,:,:3,3]@AXES/UNITS
    return out


def shift_smd(path,bone_names,shift):
    text=path.read_text()
    header,body=text.split('skeleton\n')
    # StudioMDL orders bones by the first submodel that uses them. SMD node
    # numbers remain in the source order when editing the animation.
    nodes=header.split('nodes\n',1)[1].split('end\n',1)[0].splitlines()
    columns=[row.split(chr(34)) for row in nodes]
    names=[col[1] for col in columns]
    parents=[int(col[2]) for col in columns]
    if set(names)!=set(bone_names):raise ValueError('Compiled and source skeletons differ')
    shift=shift[:,[bone_names.index(name) for name in names]]
    lines=body.splitlines()
    rows=np.array([[float(x) for x in line.split()[1:]] for line in lines if line[0].isdigit()])
    rows=rows.reshape(len(shift),len(parents),6)
    world=accumulate(matrices(rows),parents)
    target=shift@AXES.T*UNITS
    rows[:,0,:3]+=target[:,0]
    for b,p in enumerate(parents[1:],1):
        rows[:,b,:3]+=np.einsum('fji,fj->fi',world[:,p,:3,:3],target[:,b]-target[:,p])
    data=[header+'skeleton\n']
    for j,frame in enumerate(rows):
        data.append(f'time {j}\n')
        for b,values in enumerate(frame):
            data.append(str(b)+' '+' '.join(f'{x:.7f}' for x in values)+'\n')
    path.write_text(''.join(data)+'end\n')


def polish(stage,compile_model):
    steps=64
    for iteration in range(8):
        model=Compiled(stage/'scp682.mdl')
        parents=model.model.parents
        rest_frame=accumulate(matrices(model.model.values),parents)
        centers=rest_frame[:,:3,3]@AXES/UNITS
        rest=model.surface.skin(rest_frame)@AXES/UNITS
        bind=model.surface.bones
        changed=set()
        for k in range(len(CLIPS)):
            loop=k<2
            local,base=sample_local(model,k,steps)
            posed=scene_poses(accumulate(local,parents))
            frames=len(base)-int(loop)
            rows=[]
            pushes=[]
            for s,pose in enumerate(posed[:-1] if loop else posed):
                a,b,normal,d,_=pose_contacts(rest,model.f,bind,centers,pose,.099,0.,.003)
                if len(d):
                    rows.append(rows_for_keys(a,b,normal,s//steps,(s%steps)/steps,frames,len(centers),loop))
                    pushes.append(d)
            if not rows:continue
            jac=sparse.vstack(rows,format='csr')
            metric=correction_metric(bind,model.f,frames,loop)
            step=spsolve((metric+80.*(jac.T@jac)).tocsc(),80.*(jac.T@np.concatenate(pushes)))
            shift=step.reshape(frames,len(centers),3)
            if loop:shift=np.concatenate([shift,shift[:1]])
            shift_smd(stage/f'clip{k}.smd',model.model.names,.92*shift)
            changed.add(k)
            print('Studio contact pass',iteration,CLIPS[k],len(rows),'subframes',flush=True)
        if not changed:return
        compile_model(stage)
        # StudioMDL's per-bone channel scales are shared across all five clips.
        # Recheck all sequences before returning, even if only one SMD moved.
    raise RuntimeError('Studio contact polish did not converge')
