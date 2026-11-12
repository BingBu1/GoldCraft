#!/usr/bin/env python3
"""SHA-pinned original x86 resource/matrix execution, no original bytes shipped."""
import argparse, hashlib, importlib.util, json, random, struct, subprocess
from pathlib import Path
from unicorn.x86_const import *
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cof_original',ROOT/'tests/cryoffear-aim/oracle.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)

def main():
 p=argparse.ArgumentParser();p.add_argument('--sample',required=True,type=Path);p.add_argument('--runner',required=True,type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 o=base.Original(a.sample);u=o.uc;bp=base.STACK+0x2000;heap=0x22000000;u.mem_map(heap,0x20000)
 def call(start,end,budget=3000000):
  u.reg_write(UC_X86_REG_EBP,bp);u.reg_write(UC_X86_REG_ESP,bp-0x800)
  u.reg_write(UC_X86_REG_FPCW,0x37F);u.reg_write(UC_X86_REG_MXCSR,0x1F80);u.emu_start(start,end,count=budget)
  assert u.reg_read(UC_X86_REG_EIP)==end
 call(0x1005a643,0x1005a69e);a1=bytes(u.mem_read(bp-0x84,128))
 # Large original stack allocation; use the mapped heap as an explicit EBP area.
 oldbp=bp;bp=heap+0x8000
 call(0x1005a767,0x1005a7f5);a2=bytes(u.mem_read(bp-0x4004,128*128));bp=oldbp
 u.reg_write(UC_X86_REG_EAX,heap);call(0x1005a900,0x1005aa36);a3=bytes(u.mem_read(heap,32*32*32*3))
 # Run whole original cube-normal helper, including original normalization.
 cube=bytearray();u.mem_write(base.STUB+0x200,b'\xc3');u.mem_write(0x101BE764,base.u32(base.STUB+0x200))
 for face in range(6):
  for y in range(32):
   for x in range(32):
    sp=base.STACK+0x1800;u.mem_write(sp,struct.pack('<5I',base.STOP,face,x,y,heap))
    u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_FPCW,0x37F)
    u.emu_start(0x10060c00,base.STOP,count=500)
    assert u.reg_read(UC_X86_REG_EIP)==base.STOP
    values=struct.unpack('<3f',u.mem_read(heap,12))
    cube.extend(int(base.f32(base.f32(v*127.0)+128.0))&255 for v in values)
 rng=random.Random(0xC0F5);rows=[];expected=[];gamma_rows=[];gamma_expected=[]
 for case in range(1024):
  g=[rng.uniform(0,2),1.0,rng.uniform(0,.3),rng.uniform(0,2)]
  if case>=512:g[1]=rng.choice([.5,1.2,1.8,2.2,3.0])
  packed=struct.pack('<4f',*g);gamma_rows.append(packed)
  u.mem_write(heap+0x10,packed[:4]);u.mem_write(0x104CD4D0,base.u32(heap));u.mem_write(0x101CC084,packed[4:])
  call(0x1005c900,0x1005c926,100000);gamma_expected.append(bytes(u.mem_read(0x104CC488,4)))
 # Unit finite bases, nonunit bases, signed-zero and large coordinates retain
 # all original multiply/add ordering; no GPU approximation is an oracle.
 for case in range(2048):
  row=[rng.uniform(-8,8) for _ in range(9)]+[rng.uniform(-10000,10000) for _ in range(3)]
  if case<12:row[case]=-0.0
  packed=struct.pack('<12f',*row);rows.append(packed)
  vals=struct.unpack('<12f',packed)
  u.mem_write(heap,struct.pack('<i9f',0,*vals[:9]));u.mem_write(heap+0x100,packed[36:])
  u.mem_write(0x104CDF90,base.u32(heap));u.mem_write(heap+0x200+0x24,base.u32(heap+0x300));u.mem_write(heap+0x300+14,b'\0\0')
  u.mem_write(bp+8,base.u32(heap+0x200));u.mem_write(bp+12,base.u32(heap+0x100))
  call(0x10063160,0x100632ac,500);matrix=bytes(u.mem_read(bp-0x44,64))
  u.mem_write(heap+12,packed[:4]);u.mem_write(0x104CD4D0,base.u32(heap));u.mem_write(0x104CC4A0,packed[36:])
  call(0x1005c6c0,0x1005c784,500)
  expected.append(matrix+bytes(u.mem_read(bp-0x14,16))+bytes(u.mem_read(bp-0x24,16))+bytes(u.mem_read(bp-0x34,16)))
 result=subprocess.run([str(a.runner)],input=b''.join(gamma_rows)+b''.join(rows),capture_output=True,check=True).stdout
 original=a1+a2+a3+bytes(cube)+b''.join(gamma_expected)+b''.join(expected)
 if result!=original:
  mismatches=[i for i,(x,y) in enumerate(zip(result,original)) if x!=y]
  raise AssertionError({'expectedSize':len(original),'actualSize':len(result),'mismatches':len(mismatches),'first':mismatches[:20]})
 # Original1005ADA0 inverse brush transform. AngleVectors is a modeled engine
 # provider; execute the complete original subtraction and ordered dot blocks.
 brush_rows=[];brush_expected=[]
 for case in range(1536):
  values=[rng.uniform(-10000,10000) for _ in range(6)]+[rng.uniform(-1,1) for _ in range(9)]+[float(case&1)]
  if case<30:values[case%15]=-0.0
  packed=struct.pack('<16f',*values);brush_rows.append(packed)
  values=struct.unpack('<16f',packed);entity=heap+0x1000;light=heap+0x2000
  u.mem_write(entity+0xB48,packed[12:24]);u.mem_write(light,packed[:12])
  u.reg_write(UC_X86_REG_EDI,entity);u.reg_write(UC_X86_REG_ESI,light+12)
  call(0x1005af55,0x1005af9c,100)
  if values[15]:
   u.mem_write(bp-0xC0,packed[24:36]);u.mem_write(bp-0xCC,packed[36:48]);u.mem_write(bp-0xD8,packed[48:60])
   call(0x1005afc4,0x1005b088,100)
  brush_expected.append(bytes(u.mem_read(0x104CC4A0,12)))
 brush_result=subprocess.run([str(a.runner),'--brush'],input=b''.join(brush_rows),capture_output=True,check=True).stdout
 if brush_result!=b''.join(brush_expected):
  raise AssertionError('Original brush transform bit mismatch')
 ranges={f'{s:08x}-{e:08x}':hashlib.sha256(u.mem_read(s,e-s)).hexdigest() for s,e in ((0x1005a643,0x1005a69e),(0x1005a767,0x1005a7f5),(0x1005a900,0x1005aa36),(0x10060c00,0x10060cf7),(0x100947c0,0x1009487c),(0x10063160,0x100632ac),(0x1005c6c0,0x1005c784),(0x1005c900,0x1005c926),(0x100595f0,0x1005965a))}
 receipt=dict(originalSha256=base.SHA,resources={'attenuation1D':128,'attenuation2D':16384,'attenuation3D':98304,'normalizationCube':18432},matrixCases=len(rows),gammaCases=len(gamma_rows),brushTransformCases=len(brush_rows),brushOutputSha256=hashlib.sha256(brush_result).hexdigest(),texgenPlanes=len(rows)*3,byteMismatches=0,instructionRanges=ranges,outputSha256=hashlib.sha256(result).hexdigest(),gameplay=False,performance=False)
 a.output.write_text(json.dumps(receipt,indent=2),encoding='utf-8');print(json.dumps(receipt))
if __name__=='__main__':main()
