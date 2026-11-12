#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare current frustum/visibility to unchanged original client instructions."""
import argparse, itertools, json, math, random, struct, subprocess, sys
from pathlib import Path
import capstone
from unicorn.x86_const import *
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'tests/csol-teddy-blackhole-smoke'))
import originals as O

def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def data(path):
 v=json.loads(path.read_text('utf8'));return v.get('structuredContent',v)
def run(exe,mode,rows):
 p=subprocess.run([str(exe),mode],input='\n'.join(' '.join(map(str,row)) for row in rows)+'\n',
                  capture_output=True,text=True,cwd=ROOT,timeout=60)
 if p.returncode:raise AssertionError(mode+': '+p.stderr)
 actual=[json.loads(line) for line in p.stdout.splitlines()]
 if len(actual)!=len(rows):raise AssertionError('Output count '+mode)
 return actual

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for key in ('executable','client','evidence','frustum-evidence','output'):ap.add_argument('--'+key,type=Path,required=True)
 a=ap.parse_args();out=a.output.resolve()
 if out.exists():raise FileExistsError(out)
 D=a.frustum_evidence.resolve(strict=True);exe=a.executable.resolve(strict=True);client=a.client.resolve(strict=True)
 Ring,_,inputs,_=O.make_originals(ROOT,client,a.evidence.resolve(strict=True))
 code=client.read_bytes();pe=struct.unpack_from('<I',code,60)[0]
 count,optional=struct.unpack_from('<H',code,pe+6)[0],struct.unpack_from('<H',code,pe+20)[0]
 sections=[struct.unpack_from('<IIII',code,pe+24+optional+i*40+8) for i in range(count)]
 def raw(address,size):
  rva=address-0x1900000
  for _,va,length,offset in sections:
   if va<=rva and rva+size<=va+length:return code[offset+rva-va:offset+rva-va+size]
  raise AssertionError('Outside file-backed original code')
 capture_paths=[D/'writer-ida.json',D/'visibility-ida.json',D/'raw-ida.json']
 for row in data(capture_paths[2])['result']:
  expected=bytes(int(v,16) for v in row['data'].split())
  if raw(int(row['addr'],16),len(expected))!=expected:raise AssertionError('IDA raw byte mismatch')
 allowed={};cs=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
 for path,lo,hi,total in [(capture_paths[0],0x1de1790,0x1de2061,463),(capture_paths[1],0x1de2790,0x1de2a88,201)]:
  ids=data(path)['asm']['lines'];decoded=list(cs.disasm(raw(lo,hi-lo),lo))
  if [int(i['addr'],16) for i in ids]!=[i.address for i in decoded] or len(decoded)!=total:raise AssertionError('Incomplete IDA starts')
  allowed.update({i.address:i.size for i in decoded})
 class Original(Ring):
  def __init__(self):
   super().__init__();self.allowed.update(allowed);self.projection=[];self.model=[];self.queries=[]
   self.word(0x3ae8ef4,0x680000);self.word(0x68003c,0x680400)
  def hook(self,u,address,size,context):
   if address==0x680400:
    sp=u.reg_read(UC_X86_REG_ESP);name,dest=struct.unpack('<II',u.mem_read(sp+4,8))
    if name not in (0xba7,0xba6):raise AssertionError('Unexpected matrix enum')
    self.queries.append(name);u.mem_write(dest,struct.pack('<16f',*(self.projection if name==0xba7 else self.model)));self.ret();return
   if address==0x2d5b8d5:self.ret();return # Explicit security-cookie boundary only.
   super().hook(u,address,size,context)
  def call_at(self,address,object_address):
   self.u.mem_write(0x67f000,struct.pack('<I',0x500000));self.u.reg_write(UC_X86_REG_ESP,0x67f000);self.u.reg_write(UC_X86_REG_ECX,object_address)
   self.u.emu_start(address,0x500000,count=10000)
   if self.u.reg_read(UC_X86_REG_EIP)!=0x500000 or self.u.reg_read(UC_X86_REG_ESP)!=0x67f004:raise AssertionError('Original ABI return')
 q=Original();identity=[float(i%5==0) for i in range(16)];matrices=[(identity,identity),([0.]*16,identity)]
 for fov,aspect,translation in itertools.product((55.,90.,110.),(.75,1.,1.7777778),((0.,0.,0.),(10.,-20.,50.))):
  cot=1./math.tan(math.radians(fov)/2);near,far=4.,4096.
  projection=list(map(f32,[cot/aspect,0,0,0,0,cot,0,0,0,0,-(far+near)/(far-near),-1,0,0,-2*far*near/(far-near),0]))
  model=list(identity);model[12:15]=translation;matrices.append((projection,model))
 rng=random.Random(91112)
 for _ in range(64):matrices.append(([f32(rng.randint(-128,128)/32.) for _ in range(16)],[f32(rng.randint(-128,128)/32.) for _ in range(16)]))
 matrix_rows=[];matrix_expected=[];planes=[]
 for projection,model in matrices:
  q.projection=projection;q.model=model;q.queries=[];q.call_at(0x1de1790,0x680800)
  if q.queries!=[0xba7,0xba6]:raise AssertionError('Original matrix capture order')
  raw_planes=bytes(q.u.mem_read(0x680800,96));values=list(struct.unpack('<24f',raw_planes))
  planes.append(values);matrix_rows.append([bits(v) for v in projection+model]);matrix_expected.append(list(struct.unpack('<24I',raw_planes)))
 actual=run(exe,'--frustum-oracle',matrix_rows)
 matrix_diffs=[dict(case=i,input=matrix_rows[i],original=x,actual=y) for i,(x,y) in enumerate(zip(matrix_expected,actual)) if x!=y]
 # Isolate exact drawFlags1C six-plane predicate; cache is not due, so the
 # original PVS/time reader remains a clock boundary, not a replaced branch.
 query_rows=[];query_expected=[]
 unit=planes[0]
 for origin,size,pvs in itertools.product(((0.,0.,0.),(1.,0.,0.),(-1.,0.,0.),(0.,1.,0.),(0.,0.,1.),
     (1.0000001192092896,0.,0.),(-1.0000001192092896,0.,0.),(10.,20.,30.)),(0.,5.,50.,180.),(0,1)):
  query_rows.append([*[bits(v) for v in origin],bits(size),*[bits(v) for v in unit],pvs])
 for i in range(512):
  matrix_planes=planes[2+i%(len(planes)-2)]
  origin=[f32(rng.randint(-400,400)/8.) for _ in range(3)]
  size=f32(rng.choice((0.,.125,5.,50.,180.,511.)))
  query_rows.append([*[bits(v) for v in origin],bits(size),*[bits(v) for v in matrix_planes],i%2])
 # Degenerate generated planes are still original data; do not insert epsilon.
 for values in (planes[1],[0.]*24):
  query_rows.append([bits(0.),bits(0.),bits(0.),bits(5.),*[bits(v) for v in values],1])
 for row in query_rows:
  values=[struct.unpack('<f',struct.pack('<I',v))[0] for v in row[:-1]];origin=values[:3];size=values[3];pvs=row[-1]
  q.u.mem_write(0x680c00,bytes(0x1e0));q.word(0x680c08,0x1c);q.word(0x680c0c,bits(1000.))
  q.u.mem_write(0x680c10,bytes([pvs]));q.u.mem_write(0x680c50,struct.pack('<f',size));q.u.mem_write(0x680c94,struct.pack('<3f',*origin))
  q.u.mem_write(0x3634988,struct.pack('<24f',*values[4:]));q.now=10.;q.call_at(0x1de2790,0x680c00)
  query_expected.append(q.u.reg_read(UC_X86_REG_EAX)&255)
 visibility_actual=run(exe,'--visibility-oracle',query_rows)
 query_diffs=[dict(case=i,input=query_rows[i],original=x,actual=y) for i,(x,y) in enumerate(zip(query_expected,visibility_actual)) if x!=y]
 inputs += [O.identity(p) for p in capture_paths]+[O.identity(exe),O.identity(Path(__file__)),O.identity(ROOT/'native/csol/scenario/core/particle_visibility.hpp'),O.identity(ROOT/'native/csol/client/scenario/core/particle_effects.cpp')]
 unique={i['path']:i for i in inputs}
 unchanged=all(O.identity(Path(i['path']))==i for i in unique.values())
 report=dict(success=unchanged and not matrix_diffs and not query_diffs,inputsUnchanged=unchanged,inputs=list(unique.values()),
  completeIdaStarts=664,matrixCases=len(matrices),matrixFloat32Fields=len(matrices)*24,visibilityCases=len(query_rows),
  matrixDifferences=matrix_diffs,visibilityDifferences=query_diffs,sourceCopies=0,deployed=False,liveGameplay=False,performanceAccepted=False,
  boundary='Actual original463-instruction matrix generation and201-instruction fixed draw1C visibility, with controlled GetMatrix/time/security-cookie APIs; pure current helpers compared. Cached PVS is supplied, full due-PVS caching/reset lifecycle tested separately in SDK. No original or host GL/renderer performance claim.')
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n','utf8')
 print(json.dumps({k:report[k] for k in ('success','inputsUnchanged','matrixCases','matrixFloat32Fields','visibilityCases')}))
 return 0 if report['success'] else 1
if __name__=='__main__':raise SystemExit(main())
