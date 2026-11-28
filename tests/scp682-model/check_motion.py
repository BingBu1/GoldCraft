"""Decode the shipped MDL at Studio subframes and measure motion, floor and self-crossings."""
import hashlib
import json
from collections import Counter
from pathlib import Path
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/scp682'))
from review import Compiled
from views import CLIPS
from export import AXES,UNITS
from studio import accumulate,matrices
from playback import sample_local
from intersections import crossings


def anatomy(points):
    x,y,z=points.T
    # Fixed rest-space labels, independent of the before/after bone assignment.
    region=np.full(len(x),'body',dtype='<U10')
    region[y< -1.15]='tail'
    region[(y>1.27)&(z>.50)]='head'
    region[(y>1.48)&(z>.50)&(z<.797+.178*(y-1.5))]='jaw'
    leg=(z<.46)|((z<.72)&(np.abs(x)>.38))
    for front in [True,False]:
        mask=leg & ((y>0) if front else ((y<0)&(y> -1.15))) & (y<1.65)
        for left in [True,False]:
            region[mask&((x<0) if left else (x>=0))]=('front' if front else 'rear')+('_L' if left else '_R')
    return region


def metrics(path,steps=4):
    model=Compiled(path)
    rest=model.surface.skin(accumulate(matrices(model.model.values),model.model.parents))@AXES/UNITS
    face=model.f
    a,b=np.unique(np.sort(np.r_[face[:,[0,1]],face[:,[1,2]],face[:,[0,2]]],1),axis=0).T
    length=np.linalg.norm(rest[a]-rest[b],axis=1)
    region=anatomy(rest[face].mean(1))
    result={}
    for k,name in enumerate(CLIPS):
        samples,base=sample_local(model,k,steps)
        w=accumulate(samples,model.model.parents)
        primary=base[:,:,:3,:3]
        rel=primary[:-1].transpose(0,1,3,2)@primary[1:]
        angles=np.degrees(np.arccos(np.clip((np.trace(rel,axis1=2,axis2=3)-1)*.5,-1,1)))
        group=Counter()
        counts=[]
        floor=[]
        ratios=[]
        xyz=np.stack([model.surface.skin(p)@AXES/UNITS for p in w])
        for posed in xyz:
            p=crossings(rest,posed,face,.10)
            counts.append(len(p))
            group.update(' / '.join(sorted(key)) for key in region[p].tolist())
            floor.append(float(posed[:,2].min()*UNITS))
            size=np.linalg.norm(posed[a]-posed[b],axis=1)/np.maximum(.001,length)
            ratios.append(int(((size>3)&(length>.015)).sum()))
        frame_points=xyz[::steps]*UNITS
        ac=np.linalg.norm(np.diff(frame_points,n=2,axis=0),axis=2)
        result[name]=dict(samples=len(w),max_local_turn=float(angles.max()),over_90=int((angles>90).sum()),
             floor_min=min(floor),through_floor_samples=int((np.array(floor)<-.5).sum()),
             crossing_pairs=int(sum(counts)),peak_crossings=int(max(counts)),crossing_samples=int(np.count_nonzero(counts)),
             by_region=dict(group.most_common(12)),crossings_per_sample=counts,
             severe_stretch_instances=int(sum(ratios)),acceleration_p99=float(np.quantile(ac,.99)),
             loop_seam_speed_jump_p95=(float(np.quantile(np.linalg.norm(np.diff(frame_points,axis=0)[0]-
                 np.diff(frame_points,axis=0)[-1],axis=1),.95)) if k<2 else None))
        print(name,{key:(round(val,3) if isinstance(val,float) else val) for key,val in result[name].items()
             if key not in ('by_region','crossings_per_sample')},group.most_common(5),flush=True)
    return result


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('model',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--substeps',type=int,default=4)
    parser.add_argument('--release',action='store_true',help='Require the repaired motion and contact limits')
    args=parser.parse_args()
    if args.substeps<1:
        parser.error('substeps must be positive')
    clips=metrics(args.model,args.substeps)
    passed=all(clip['crossing_pairs']==0 and clip['floor_min']>=-.02 and
               clip['max_local_turn']<=60. for clip in clips.values())
    report=dict(model=hashlib.sha256(args.model.read_bytes()).hexdigest(),substeps=args.substeps,
                min_bind_gap=.10,samples=sum(c['samples'] for c in clips.values()),
                passed=bool(passed),clips=clips)
    args.output.write_text(json.dumps(report,indent=2))
    if args.release and not passed:
        raise SystemExit('Compiled model exceeds a motion or contact limit')
