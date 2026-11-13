#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bake explicitly authored CSO hand geometry into a CoF skeleton; never replace its animation."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from cryoffear_studio import Studio, transform

ROOT = Path(__file__).resolve().parents[1]

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def similarity(value):
    if not isinstance(value,list) or len(value)!=12 or any(type(x) not in (int,float) or not math.isfinite(x) for x in value):
        raise ValueError('Each authored transform requires twelve finite coefficients')
    matrix=[list(map(float,value[i:i+4])) for i in (0,4,8)]
    scale=math.sqrt(sum(matrix[0][i]**2 for i in range(3)))
    if not 0 < scale <= 64:
        raise ValueError('Invalid authored geometry scale')
    for a in range(3):
        for b in range(3):
            dot=sum(matrix[a][i]*matrix[b][i] for i in range(3))
            if abs(dot-(scale*scale if a==b else 0))>1e-5*scale*scale:
                raise ValueError('Only rigid rotation and uniform geometry scale are supported')
    a=matrix
    determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0])
    if determinant<=0:
        raise ValueError('Mirrored hand geometry is not an implicit conversion')
    return matrix,scale

def geometry_transform(row):
    mode=row.get("geometry", "uniform")
    if mode=="uniform":
        matrix,scale=similarity(row["transform"])
        return matrix,scale,None
    if mode!="affine":raise ValueError("Unknown authored geometry transform")
    value=row["transform"]
    if not isinstance(value,list) or len(value)!=12 or any(type(x) not in (int,float) or not math.isfinite(x) for x in value):
        raise ValueError("Each authored transform requires twelve finite coefficients")
    a=[list(map(float,value[i:i+4])) for i in (0,4,8)]
    # Cofactors form the inverse transpose; this is geometry authoring only.
    c=[[a[(i+1)%3][(j+1)%3]*a[(i+2)%3][(j+2)%3]-a[(i+1)%3][(j+2)%3]*a[(i+2)%3][(j+1)%3] for j in range(3)] for i in range(3)]
    determinant=sum(a[0][j]*c[0][j] for j in range(3))
    bound=math.sqrt(sum(a[i][j]**2 for i in range(3) for j in range(3)))
    if not 0 < bound <= 64 or not determinant>1e-9*bound**3:
        raise ValueError("Invalid, mirrored or singular authored geometry")
    normal=[[c[i][j]/determinant for j in range(3)]+[0.] for i in range(3)]
    inverse_bound=math.sqrt(sum(normal[i][j]**2 for i in range(3) for j in range(3)))
    if inverse_bound>1024 or bound*inverse_bound>10000:
        raise ValueError("Ill-conditioned authored geometry")
    return a,bound,normal

def mesh_edits(profile, models, target):
    edits=[{"vertices":{},"normals":{},"verticesBones":{},"normalsBones":{},"radius":0.} for _ in models]
    bone_indices={}
    rows=profile.get("meshEdits",[])
    if not isinstance(rows,list) or len(rows)>len(models):raise ValueError("Invalid authored mesh edits")
    seen=set()
    for row in rows:
        if not isinstance(row,dict) or set(row)-{"model","vertices","normals"}:
            raise ValueError("Unknown authored mesh edit field")
        model=row.get("model")
        if type(model) is not int or not 0<=model<len(models) or model in seen:
            raise ValueError("Invalid or duplicate authored mesh model")
        seen.add(model)
        for kind,key in (("vertices","offset"),("normals","direction")):
            values=row.get(kind,[])
            if not isinstance(values,list) or len(values)>len(models[model][kind]):
                raise ValueError("Invalid authored mesh vector list")
            for value in values:
                if not isinstance(value,dict) or not {"index",key}.issubset(value) or set(value)-{"index",key,"bone"}:
                    raise ValueError("Invalid authored mesh vector fields")
                index,vector=value["index"],value[key]
                if type(index) is not int or not 0<=index<len(models[model][kind]) or index in edits[model][kind]:
                    raise ValueError("Invalid or duplicate authored mesh vector index")
                if not isinstance(vector,list) or len(vector)!=3 or any(type(v) not in (int,float) or not math.isfinite(v) for v in vector):
                    raise ValueError("Authored mesh vectors require three finite numbers")
                length=math.hypot(*vector)
                if kind=="vertices":
                    # Authoring limit in target-model units; no gameplay meaning.
                    if length>64:raise ValueError("Authored vertex offset exceeds geometry budget")
                    edits[model]["radius"]=max(edits[model]["radius"],length)
                else:
                    if not 1e-9<length<=64:raise ValueError("Invalid authored normal direction")
                    vector=[v/length for v in vector]
                edits[model][kind][index]=vector
                if "bone" in value:
                    bone=value["bone"]
                    if not isinstance(bone,str) or not bone:
                        raise ValueError("Explicit authored bone requires a target bone name")
                    target_index=bone_indices.get(bone)
                    if target_index is None:
                        target_index=target.bone_index(bone)
                        bone_indices[bone]=target_index
                    if target_index>255:raise ValueError("Explicit authored bone exceeds Studio index range")
                    edits[model][kind+"Bones"][index]=target_index
    return edits

class Writer:
    def __init__(self, source):self.data=bytearray(source)
    def append(self, value):
        self.data.extend(bytes(-len(self.data)%4));offset=len(self.data)
        if len(value)>64*1024*1024-len(self.data):raise ValueError('Authored model exceeds resource budget')
        self.data.extend(value);return offset
    def integer(self,offset,value):struct.pack_into('<i',self.data,offset,value)

def prepare(target_path: Path, donor_path: Path, profile_path: Path):
    target,donor=Studio.read(target_path),Studio.read(donor_path)
    profile=json.loads(profile_path.read_text('utf-8-sig'))
    if profile.get('version')!=1 or profile.get('targetSha256')!=target.sha256 or profile.get('donorSha256')!=donor.sha256:
        raise ValueError('Explicit profile identities do not match the actual models')
    groups=[g for g in donor.bodyparts if g['name']=='hands']
    if len(groups)!=1 or len(groups[0]['models'])!=2 or len(donor.skins)!=1:
        raise ValueError('Expected one paired male/female hand group with invariant skins')
    bindings={}
    for row in profile['bones']:
        index=donor.bone_index(row['donor']);to=target.bone_index(row['target'])
        if index in bindings:raise ValueError('Duplicate authored donor bone')
        matrix,scale,normal=geometry_transform(row);bindings[index]=(to,matrix,scale,normal)
    hand_models=groups[0]['models']
    used={i for model in hand_models for kind in ('vertices','normals') for i in model[kind+'Bones']}
    if not used.issubset(bindings):raise ValueError('Every weighted hand bone needs an explicit authored transform')
    edits=mesh_edits(profile,hand_models,target)
    selected=sorted({donor.skins[0][mesh['skinref']] for model in hand_models for mesh in model['meshes']})
    writer=Writer(target.data);texture_map={};textures=[];texels=[]
    # Keep original target bytes and absolute sequence/attachment offsets intact.
    # The generated donor references only the appended hand mesh/texture tables.
    for index in selected:
        source=donor.textures[index];at=donor.i(184)+index*80
        descriptor=bytearray(donor.data[at:at+80]);pixels=donor.i(at+76);count=source['width']*source['height']+768
        donor.span(pixels,count,1,64*1024*1024)
        texels.append(donor.data[pixels:pixels+count])
        struct.pack_into('<i',descriptor,76,0)
        texture_map[index]=len(textures);textures.append(descriptor)
    texture_table=writer.append(b''.join(textures))
    skin_table=writer.append(struct.pack('<'+'h'*len(textures),*range(len(textures))))
    models=[];vertices_total=0;triangles_total=0
    for gender,model in enumerate(hand_models):
        descriptor=bytearray(112);name=('cso_hand_male' if gender==0 else 'cso_hand_female').encode()
        descriptor[:len(name)]=name
        for kind,n_at,bone_at,vector_at in [('vertices',80,84,88),('normals',92,96,100)]:
            positions=[];indices=[]
            for vertex,(index,point) in enumerate(zip(model[kind+'Bones'],model[kind])):
                to,matrix,scale,normal=bindings[index]
                p=transform(normal if kind=='normals' and normal else matrix,point,normal=kind=='normals')
                if kind=='normals':
                    if normal:
                        length=math.hypot(*p)
                        if not length>0:raise ValueError('Invalid authored normal')
                        factor=math.hypot(*point)/length;p=tuple(v*factor for v in p)
                    else:p=tuple(v/scale for v in p)
                if change:=edits[gender][kind].get(vertex):
                    if kind=='vertices':p=tuple(a+b for a,b in zip(p,change))
                    else:
                        length=math.hypot(*p);p=tuple(v*length for v in change)
                if any(not math.isfinite(v) or abs(v)>1e6 for v in p):raise ValueError('Invalid authored geometry')
                positions.append(p);indices.append(edits[gender][kind+'Bones'].get(vertex,to))
            struct.pack_into('<iii',descriptor,n_at,len(indices),writer.append(bytes(indices)),writer.append(b''.join(struct.pack('<3f',*p) for p in positions)))
            if kind=='vertices':vertices_total+=len(indices)
        meshes=[]
        for mesh in model['meshes']:
            commands=bytearray()
            # Studio has already decoded original winding/UVs. Independent
            # triangles preserve their exact ordered vertex/normal/UV indices.
            for triangle in mesh['triangles']:
                commands.extend(struct.pack('<h',3))
                for corner in triangle:commands.extend(struct.pack('<4h',*corner))
            commands.extend(bytes(2));triangles_total+=len(mesh['triangles'])
            material=texture_map[donor.skins[0][mesh['skinref']]]
            meshes.append(struct.pack('<5i',len(mesh['triangles']),writer.append(commands),material,mesh['normalCount'],0))
        struct.pack_into('<ii',descriptor,72,len(meshes),writer.append(b''.join(meshes)))
        struct.pack_into('<f',descriptor,68,model['radius']*max(v[2] for v in bindings.values())+edits[gender]['radius'])
        models.append(descriptor)
    # This is an offline geometry donor, never an event producer. Keep the
    # original bytes and animation channels, but point to event-free descriptors
    # so dependency collection cannot request the target weapon's old sounds.
    sequence_table=bytearray(target.data[target.i(168):target.i(168)+target.i(164)*176])
    for sequence in range(target.i(164)):
        struct.pack_into('<ii',sequence_table,sequence*176+48,0,0)
    donor_sequences=writer.append(sequence_table)
    model_table=writer.append(b''.join(models));group=bytearray(76);group[:5]=b'hands'
    struct.pack_into('<iii',group,64,2,1,model_table);group_table=writer.append(group)
    # Native GoldSrc retains the complete non-pixel prefix and expands palettes
    # from one tightly packed tail. Every descriptor must precede this boundary.
    texture_pixels=writer.append(b''.join(texels));cursor=texture_pixels
    for i,pixels in enumerate(texels):
        writer.integer(texture_table+i*80+76,cursor);cursor+=len(pixels)
    for offset,value in [(180,len(textures)),(184,texture_table),(188,texture_pixels),(192,len(textures)),(196,1),(200,skin_table),(204,1),(208,group_table),(168,donor_sequences),(72,len(writer.data))]:writer.integer(offset,value)
    result=bytes(writer.data);check=Studio(result)
    if result[244:len(target.data)]!=target.data[244:]:raise ValueError('Original target payload changed')
    for offset in (140,144,148,152,164,172,176,212,216):
        if check.i(offset)!=target.i(offset):raise ValueError('Target animation/skeleton/attachment identity changed')
    return result,dict(targetSha256=target.sha256,donorSha256=donor.sha256,profileSha256=digest(profile_path.read_bytes()),
        outputSha256=digest(result),vertices=vertices_total,triangles=triangles_total,mappedBones=len(used),
        originalPayloadPreserved=True,skeletonAnimationPreserved=True,geometryDonorEventsDetached=True,
        explicitBoneAssignments=sum(len(edit[kind+'Bones']) for edit in edits for kind in ('vertices','normals')),
        visualAccepted=False,gameplayAccepted=False)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('target','donor','profile','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();output=args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build') or any(p.is_symlink() or p.is_junction() for p in (output,*output.parents)):
        raise ValueError('Use a fresh model-resource folder inside workspace build')
    result,receipt=prepare(args.target,args.donor,args.profile);output.mkdir(parents=True)
    (output/'v_cof_hands.mdl').write_bytes(result)
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf8');print(json.dumps(receipt))

if __name__=='__main__':main()
