"""Evaluate the compiled Studio v10 rotations and compressed positional channels between keys."""
import struct
import numpy as np
from scipy.spatial.transform import Rotation
from review import matrices
from views import CLIPS


def translation_blends(studio,seq,n):
    # At the last explicit sample of an RLE span Studio holds the position until
    # the next span. Rotation has a different next-value rule in the client SDK.
    result=np.ones((n-1,len(studio.bones),3),bool)
    seqbase=studio.i(168)+176*seq
    for b in range(len(studio.bones)):
        row=studio.i(seqbase+124)+12*b
        for axis in range(3):
            off=struct.unpack_from('<H',studio.data,row+2*axis)[0]
            if not off: continue
            at=row+off
            frame=0
            while frame<n-1:
                valid,total=studio.data[at:at+2]
                if not total: raise ValueError('Empty animation span')
                last=frame+valid-1
                if 0<=last<n-1: result[last,b,axis]=False
                at+=2+2*valid
                frame+=total
    return result


def sample_local(model,seq,steps):
    raw=model.model.raw(CLIPS[seq])
    base=matrices(raw)
    rotations=Rotation.from_matrix(base[:,:,:3,:3].reshape(-1,3,3)).as_quat().reshape(*raw.shape[:2],4)
    qa,qb=rotations[:-1],rotations[1:].copy()
    dot=(qa*qb).sum(2,keepdims=True)
    qb*=np.where(dot<0,-1,1)
    omega=np.arccos(np.clip(np.abs(dot),0,1))
    den=np.sin(omega)
    blend=translation_blends(model.g,seq,len(raw))
    all_samples=[]
    for j in range(steps):
        w=j/steps
        q=(np.sin((1-w)*omega)*qa+np.sin(w*omega)*qb)/np.maximum(den,1e-12)
        q=np.where(den<1e-5,(1-w)*qa+w*qb,q)
        local=base[:-1].copy()
        local[:,:,:3,:3]=Rotation.from_quat(q.reshape(-1,4)).as_matrix().reshape(*q.shape[:2],3,3)
        local[:,:,:3,3]=raw[:-1,:,:3]+blend*w*(raw[1:,:,:3]-raw[:-1,:,:3])
        all_samples.append(local)
    local=np.stack(all_samples,axis=1).reshape(-1,*base.shape[1:])
    return np.concatenate([local,base[-1:]]),base
