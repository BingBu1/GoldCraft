#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real hand authoring tool against actual paired hand geometry."""
import argparse
import copy
import importlib.util
import json
import math
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from cryoffear_studio import Studio,transform

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();output=args.output.absolute()
    assert not output.exists() and output.resolve().is_relative_to(ROOT/'build')
    output.mkdir(parents=True)
    spec=importlib.util.spec_from_file_location('author',ROOT/'tools/Prepare-CryOfFearHands.py')
    author=importlib.util.module_from_spec(spec);spec.loader.exec_module(author)
    controls=[Path(__file__).resolve(),ROOT/'tools/Prepare-CryOfFearHands.py',ROOT/'tools/cryoffear_studio.py',args.model.resolve()]
    before={str(p):author.digest(p.read_bytes()) for p in controls}
    source=Studio.read(args.model);hands=next(g for g in source.bodyparts if g['name']=='hands')
    used=sorted({b for model in hands['models'] for kind in ('vertices','normals') for b in model[kind+'Bones']})
    profile=dict(version=1,targetSha256=source.sha256,donorSha256=source.sha256,bones=[dict(donor=source.bones[b]['name'],target=source.bones[b]['name'],transform=[1,0,0,0,0,1,0,0,0,0,1,0]) for b in used])
    path=output/'profile.json';path.write_text(json.dumps(profile),'utf8');checks=0
    data,receipt=author.prepare(args.model,args.model,path);result=Studio(data)
    assert result.i(164)==source.i(164) and result.i(168)!=source.i(168)
    for sequence in range(result.i(164)):
        at=result.i(168)+176*sequence
        assert result.i(at+48)==0 and result.i(at+52)==0
        assert result.i(at+124)==source.i(source.i(168)+176*sequence+124)
    assert data[244:len(source.data)]==source.data[244:];checks+=1
    for original,adapted in zip(hands['models'],result.bodyparts[0]['models']):
        for kind in ('vertices','normals'):
            assert original[kind]==adapted[kind] and original[kind+'Bones']==adapted[kind+'Bones'];checks+=1
        for a,b in zip(original['meshes'],adapted['meshes']):
            assert a['triangles']==b['triangles'];checks+=1
            assert source.textures[source.skins[0][a['skinref']]]['sha256']==result.textures[result.skins[0][b['skinref']]]['sha256'];checks+=1
    frames=0
    for sequence in range(source.i(164)):
        at=source.i(168)+176*sequence
        for blend in range(source.i(at+120)):
            for frame in range(source.i(at+56)):
                assert source.channels(sequence,frame,blend)==result.channels(sequence,frame,blend);frames+=1
    checks+=1
    matrix=[0,-2,0,3,2,0,0,-5,0,0,2,7]
    moved=copy.deepcopy(profile)
    for row in moved['bones']:row['transform']=matrix
    path.write_text(json.dumps(moved),'utf8');altered,_=author.prepare(args.model,args.model,path);mapped=Studio(altered)
    for original,adapted in zip(hands['models'],mapped.bodyparts[0]['models']):
        for kind in ('vertices','normals'):
            for a,b in zip(original[kind],adapted[kind]):
                expected=transform([matrix[i:i+4] for i in (0,4,8)],a,kind=='normals')
                if kind=='normals':expected=tuple(x/2 for x in expected)
                assert all(abs(x-y)<=1e-5 for x,y in zip(expected,b));checks+=1
    def rejects(label,value):
        nonlocal checks
        path.write_text(json.dumps(value),'utf8')
        try:author.prepare(args.model,args.model,path)
        except ValueError:checks+=1;return
        raise AssertionError('Accepted '+label)
    bad=copy.deepcopy(profile);bad['targetSha256']='0'*64;rejects('wrong target',bad)
    bad=copy.deepcopy(profile);bad['donorSha256']='0'*64;rejects('wrong donor',bad)
    bad=copy.deepcopy(profile);bad['bones'].pop();rejects('unmapped geometry',bad)
    bad=copy.deepcopy(profile);bad['bones'].append(bad['bones'][0]);rejects('duplicate binding',bad)
    for label,matrix in [('shear',[1,.1,0,0,0,1,0,0,0,0,1,0]),('reflection',[-1,0,0,0,0,1,0,0,0,0,1,0]),('nonuniform',[2,0,0,0,0,1,0,0,0,0,1,0])]:
        bad=copy.deepcopy(profile);bad['bones'][0]['transform']=matrix;rejects(label,bad)
    # Explicit nonuniform authoring preserves normal direction and length under
    # the inverse transpose. Uniform/default profiles still reject this matrix.
    affine=copy.deepcopy(profile)
    for row in affine["bones"]:
        row["geometry"]="affine";row["transform"]=[2,0,0,3,0,3,0,-5,0,0,4,7]
    path.write_text(json.dumps(affine),"utf8");affine_bytes,_=author.prepare(args.model,args.model,path);affine_model=Studio(affine_bytes)
    for original,adapted in zip(hands["models"],affine_model.bodyparts[0]["models"]):
        for kind in ("vertices","normals"):
            for a,b in zip(original[kind],adapted[kind]):
                if kind=="vertices":expected=(a[0]*2+3,a[1]*3-5,a[2]*4+7)
                else:
                    scaled=(a[0]/2,a[1]/3,a[2]/4);factor=math.hypot(*a)/math.hypot(*scaled)
                    expected=tuple(x*factor for x in scaled)
                assert all(abs(x-y)<=1e-5 for x,y in zip(expected,b));checks+=1
    # Independent normal/tangent orthogonality check for a rotated stretch.
    rotated=copy.deepcopy(affine)
    for row in rotated["bones"]:row["transform"]=[0,-3,0,3,2,0,0,-5,0,0,4,7]
    path.write_text(json.dumps(rotated),"utf8");rotated_bytes,_=author.prepare(args.model,args.model,path);rotated_model=Studio(rotated_bytes)
    for original,adapted in zip(hands["models"],rotated_model.bodyparts[0]["models"]):
        for a,b in zip(original["normals"],adapted["normals"]):
            scaled=(-a[1]/3,a[0]/2,a[2]/4);factor=math.hypot(*a)/math.hypot(*scaled)
            assert all(abs(x*factor-y)<=1e-5 for x,y in zip(scaled,b));checks+=1
            tangent=(a[1],-a[0],0);moved=(3*a[0],2*a[1],0)
            assert abs(sum(x*y for x,y in zip(moved,b)))<1e-5;checks+=1
    bad=copy.deepcopy(affine);bad["bones"][0]["transform"][0]=0;rejects("singular affine",bad)
    bad=copy.deepcopy(affine);bad["bones"][0]["transform"][0]=-2;rejects("mirrored affine",bad)
    bad=copy.deepcopy(affine);bad["bones"][0]["transform"][0]=1e-8;rejects("ill-conditioned affine",bad)
    bad=copy.deepcopy(profile);bad["bones"][0]["geometry"]="unknown";rejects("unknown geometry mode",bad)
    # Explicit geometry edits act in the authored target-bone space. They never
    # change the donor topology, the original rig or its animation channels.
    edited=copy.deepcopy(affine)
    edited["meshEdits"]=[dict(model=0,vertices=[dict(index=0,offset=[1,-2,3])],
                              normals=[dict(index=0,direction=[0,2,0])]),
                          dict(model=1,vertices=[dict(index=0,offset=[-3,2,-1])])]
    path.write_text(json.dumps(edited),"utf8");edited_bytes,_=author.prepare(args.model,args.model,path);edited_model=Studio(edited_bytes)
    assert edited_bytes[244:len(source.data)]==source.data[244:];checks+=1
    for sex,offset in enumerate(((1,-2,3),(-3,2,-1))):
        base=affine_model.bodyparts[0]["models"][sex];part=edited_model.bodyparts[0]["models"][sex]
        for i,(a,b) in enumerate(zip(base["vertices"],part["vertices"])):
            want=tuple(x+y for x,y in zip(a,offset)) if i==0 else a
            assert all(abs(x-y)<=1e-5 for x,y in zip(want,b));checks+=1
        for i,(a,b) in enumerate(zip(base["normals"],part["normals"])):
            want=(0,math.hypot(*a),0) if sex==0 and i==0 else a
            assert all(abs(x-y)<=1e-5 for x,y in zip(want,b));checks+=1
        assert part['verticesBones']==base['verticesBones'] and part['normalsBones']==base['normalsBones'];checks+=1
        assert [m['triangles'] for m in part['meshes']]==[m['triangles'] for m in base['meshes']];checks+=1
    for sequence in range(source.i(164)):
        at=source.i(168)+176*sequence
        for blend in range(source.i(at+120)):
            for frame in range(source.i(at+56)):
                assert source.channels(sequence,frame,blend)==edited_model.channels(sequence,frame,blend);checks+=1
    for label,rows in [
        ("bad mesh list",{}),("unknown mesh field",[dict(model=0,typo=[])]),
        ("missing model",[dict(vertices=[])]),("bool model",[dict(model=True)]),
        ("negative model",[dict(model=-1)]),("large model",[dict(model=2)]),
        ("duplicate model",[dict(model=0),dict(model=0)]),
        ("bad vertex list",[dict(model=0,vertices={})]),
        ("bad vector fields",[dict(model=0,vertices=[dict(index=0,direction=[1,0,0])])]),
        ("negative vertex",[dict(model=0,vertices=[dict(index=-1,offset=[0,0,0])])]),
        ("large vertex",[dict(model=0,vertices=[dict(index=999999,offset=[0,0,0])])]),
        ("bool vertex",[dict(model=0,vertices=[dict(index=False,offset=[0,0,0])])]),
        ("duplicate vertex",[dict(model=0,vertices=[dict(index=0,offset=[0,0,0])]*2)]),
        ("bad vector size",[dict(model=0,vertices=[dict(index=0,offset=[1,2])])]),
        ("bool component",[dict(model=0,vertices=[dict(index=0,offset=[False,0,0])])]),
        ("nan component",[dict(model=0,vertices=[dict(index=0,offset=[float('nan'),0,0])])]),
        ("unbounded offset",[dict(model=0,vertices=[dict(index=0,offset=[65,0,0])])]),
        ("zero direction",[dict(model=0,normals=[dict(index=0,direction=[0,0,0])])]),
        ("tiny direction",[dict(model=0,normals=[dict(index=0,direction=[1e-12,0,0])])]),
        ("huge direction",[dict(model=0,normals=[dict(index=0,direction=[65,0,0])])]),
        ("duplicate normal",[dict(model=0,normals=[dict(index=0,direction=[1,0,0])]*2)])]:
        bad=copy.deepcopy(profile);bad['meshEdits']=rows;rejects(label,bad)
    assert author.digest(args.model.read_bytes())==source.sha256
    after={str(p):author.digest(p.read_bytes()) for p in controls}
    assert before==after
    (output/'identity-donor.mdl').write_bytes(data)
    (output/'receipt.json').write_text(json.dumps(dict(success=True,checks=checks,before=before,after=after,inputsUnchanged=before==after,originalAnimationFrames=frames,sourceSha256=source.sha256,authoring=receipt,visualAccepted=False,gameplayAccepted=False),indent=2),'utf8')
    print(json.dumps(dict(success=True,checks=checks,frames=frames)))

if __name__=='__main__':main()
