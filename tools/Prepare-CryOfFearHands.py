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
        matrix,scale=similarity(row['transform']);bindings[index]=(to,matrix,scale)
    hand_models=groups[0]['models']
    used={i for model in hand_models for kind in ('vertices','normals') for i in model[kind+'Bones']}
    if not used.issubset(bindings):raise ValueError('Every weighted hand bone needs an explicit authored transform')
    selected=sorted({donor.skins[0][mesh['skinref']] for model in hand_models for mesh in model['meshes']})
    writer=Writer(target.data);texture_map={};textures=[]
    # Keep original target bytes and absolute sequence/attachment offsets intact.
    # The generated donor references only the appended hand mesh/texture tables.
    for index in selected:
        source=donor.textures[index];at=donor.i(184)+index*80
        descriptor=bytearray(donor.data[at:at+80]);pixels=donor.i(at+76);count=source['width']*source['height']+768
        donor.span(pixels,count,1,64*1024*1024)
        struct.pack_into('<i',descriptor,76,writer.append(donor.data[pixels:pixels+count]))
        texture_map[index]=len(textures);textures.append(descriptor)
    texture_pixels=struct.unpack_from('<i',textures[0],76)[0]
    texture_table=writer.append(b''.join(textures))
    skin_table=writer.append(struct.pack('<'+'h'*len(textures),*range(len(textures))))
    models=[];vertices_total=0;triangles_total=0
    for gender,model in enumerate(hand_models):
        descriptor=bytearray(112);name=('cso_hand_male' if gender==0 else 'cso_hand_female').encode()
        descriptor[:len(name)]=name
        for kind,n_at,bone_at,vector_at in [('vertices',80,84,88),('normals',92,96,100)]:
            positions=[];indices=[]
            for index,point in zip(model[kind+'Bones'],model[kind]):
                to,matrix,scale=bindings[index];p=transform(matrix,point,normal=kind=='normals')
                if kind=='normals':p=tuple(v/scale for v in p)
                if any(not math.isfinite(v) or abs(v)>1e6 for v in p):raise ValueError('Invalid authored geometry')
                positions.append(p);indices.append(to)
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
        struct.pack_into('<f',descriptor,68,model['radius']*max(v[2] for v in bindings.values()))
        models.append(descriptor)
    model_table=writer.append(b''.join(models));group=bytearray(76);group[:5]=b'hands'
    struct.pack_into('<iii',group,64,2,1,model_table);group_table=writer.append(group)
    for offset,value in [(180,len(textures)),(184,texture_table),(188,texture_pixels),(192,len(textures)),(196,1),(200,skin_table),(204,1),(208,group_table),(72,len(writer.data))]:writer.integer(offset,value)
    result=bytes(writer.data);check=Studio(result)
    if result[244:len(target.data)]!=target.data[244:]:raise ValueError('Original target payload changed')
    for offset in (140,144,148,152,164,168,172,176,212,216):
        if check.i(offset)!=target.i(offset):raise ValueError('Target animation/skeleton/attachment identity changed')
    return result,dict(targetSha256=target.sha256,donorSha256=donor.sha256,profileSha256=digest(profile_path.read_bytes()),
        outputSha256=digest(result),vertices=vertices_total,triangles=triangles_total,mappedBones=len(used),
        originalPayloadPreserved=True,skeletonAnimationPreserved=True,visualAccepted=False,gameplayAccepted=False)

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
