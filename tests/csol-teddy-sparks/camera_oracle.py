#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare ordinary-gameplay/observer particle camera branches to original x86."""
import argparse, itertools, json, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'tests/csol-teddy-blackhole-smoke'))
import originals as O
from unicorn.x86_const import *

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for key in ('executable','client','evidence','camera-evidence','output'):ap.add_argument('--'+key,type=Path,required=True)
 a=ap.parse_args();out=a.output.resolve()
 if out.exists():raise FileExistsError(out)
 D=a.camera_evidence.resolve(strict=True);exe=a.executable.resolve(strict=True)
 Ring,_,inputs,_=O.make_originals(ROOT,a.client.resolve(strict=True),a.evidence.resolve(strict=True))
 imports=[D/'camera-ida.json',D/'camera-player-ida.json',D/'camera-raw-ida.json']
 def structured(p):
  value=json.loads(p.read_text('utf8'))
  return value['structuredContent'] if 'structuredContent' in value else value
 ranges=[structured(p)['asm']['lines'] for p in imports[:2]]
 # Match captured IDA bytes against the exact mapped PE before executing it.
 raw=a.client.resolve(strict=True).read_bytes();pe=struct.unpack_from('<I',raw,60)[0]
 sections,optional=struct.unpack_from('<H',raw,pe+6)[0],struct.unpack_from('<H',raw,pe+20)[0]
 mapping=[struct.unpack_from('<IIII',raw,pe+24+optional+40*i+8) for i in range(sections)]
 for row in structured(imports[2])['result']:
  address=int(row['addr'],16);value=bytes(int(v,16) for v in row['data'].split());rva=address-0x1900000
  match=next((raw[off+rva-va:off+rva-va+len(value)] for vs,va,size,off in mapping if va<=rva and rva+len(value)<=va+size),None)
  if match!=value:raise AssertionError('Camera IDA bytes do not match PE')

 allowed={}
 for lines,end in zip(ranges,(0x2ad3ae4,0x2a57064)):
  starts=[int(line['addr'],16) for line in lines]
  allowed.update({address:after-address for address,after in zip(starts,starts[1:]+[end])})
 if len(ranges[0])!=70 or len(ranges[1])!=4:raise AssertionError('Incomplete original camera IDA')
 class Original(Ring):
  def __init__(self):
   super().__init__();self.camera_count=0
   self.allowed.update(allowed)
   self.word(0x3ae8ffc,0x680400);self.word(0x680000,0x680100);self.word(0x680290,0x680420)
   self.word(0x3ae8e34,0x680440);self.word(0x3c2ccac,1)
   self.u.mem_write(0x3ed25a1+0x298,b'\0')
  def hook(self,u,address,size,context):
   if address==0x680400:self.ret(eax=0x680000);return
   if address==0x680420:self.ret(eax=0);return # Declared native-gameplay mode, not VXL/53.
   if address==0x680440:
    sp=u.reg_read(UC_X86_REG_ESP);dest=self.integer(sp+4)
    u.mem_write(dest,struct.pack('<3f',*self.camera));self.camera_count+=1;self.ret();return
   super().hook(u,address,size,context)
 rows=[];expected=[]
 for mode,target,secondary in itertools.product((-1,0,1,2,3,4,5,6,8),(-1,0,1),(-1,0,1)):
  q=Original();q.camera=[12.5,-81.,3.25]
  for address,value in ((0x3a48858,mode),(0x3a4885c,target),(0x3a48860,secondary)):q.word(address,value&0xffffffff)
  q.u.mem_write(0x3f33128,struct.pack('<3f',40,41,42))
  q.u.mem_write(0x3f33158,struct.pack('<3f',60,61,62))
  q.u.mem_write(0x67f000,struct.pack('<II',0x500000,0x680800))
  q.u.reg_write(UC_X86_REG_ESP,0x67f000)
  q.u.emu_start(0x2ad39f0,0x500000,count=2000)
  if q.u.reg_read(UC_X86_REG_EIP)!=0x500000 or q.u.reg_read(UC_X86_REG_ESP)!=0x67f004:raise AssertionError('Original camera ABI')
  expected.append([*struct.unpack('<3I',q.u.mem_read(0x680800,12)),q.camera_count])
  rows.append([mode,target,secondary,40,41,42,60,61,62,*q.camera])
 text='\n'.join(' '.join(str(v) for v in row) for row in rows)+'\n'
 p=subprocess.run([str(exe),'--camera-oracle'],input=text,text=True,capture_output=True,cwd=ROOT,timeout=60)
 if p.returncode:raise AssertionError(p.stderr)
 actual=[json.loads(line) for line in p.stdout.splitlines()]
 if len(actual)!=len(expected):raise AssertionError('Missing actual camera rows')
 differences=[dict(case=i,input=rows[i],original=x,actual=y) for i,(x,y) in enumerate(zip(expected,actual)) if x!=y]
 inputs += [O.identity(p) for p in imports]+[O.identity(exe),O.identity(Path(__file__)),O.identity(ROOT/'native/csol/client/scenario/core/particle_effects.cpp')]
 unique={i['path']:i for i in inputs}
 unchanged=all(O.identity(Path(i['path']))==i for i in unique.values())
 report=dict(success=unchanged and not differences,inputsUnchanged=unchanged,cases=len(rows),differences=differences,
  inputs=list(unique.values()),originalIdaStarts=74,sourceCopies=0,deployed=False,liveGameplay=False,performanceAccepted=False,
  boundary='Original full70-instruction getter and4-instruction viewed-player selector; controlled native-gameplay interface, no cutscene flag, engine getter, and two original global angle vectors. Actual SDK camera uses matching native globals. Original VXL/mode53 providers remain unimplemented and are not claimed.')
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n','utf8')
 print(json.dumps({k:report[k] for k in ('success','inputsUnchanged','cases','originalIdaStarts')}))
 return 0 if report['success'] else 1
if __name__=='__main__':raise SystemExit(main())
