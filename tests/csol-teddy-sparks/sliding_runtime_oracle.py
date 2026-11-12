#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare the actual shared Sliding runtime to original parser/factory/Frame."""
import argparse, importlib.util, json, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
def module(name,path):
 s=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
from unicorn.x86_const import UC_X86_REG_EAX
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 for name in ('executable','client','output'):ap.add_argument('--'+name,type=Path,required=True)
 a=ap.parse_args();out=a.output.resolve()
 if out.exists():raise FileExistsError(out)
 S=module('runtime_sliding_originals',ROOT/'tests/csol-teddy-sliding/particle_oracle.py')
 E,_,inputs=S.make_originals(ROOT,a.client.resolve(strict=True),ROOT/'analysis/teddy-spawn-visuals-20261111',
                            ROOT/'analysis/teddy-spark-oracle-20261111',ROOT/'analysis/teddy-sliding-client-review-20261111')
 class Original(E):
  def __init__(self,scenario):
   super().__init__();self.scenario=scenario
  def hook(self,u,address,size,context):
   if self.scenario==4 and self.callbacks.get(address)=='allocate':
    self.ret(4,eax=0);return
   super().hook(u,address,size,context)
 packets=[S.packet(owner=44),S.packet(owner=44,offset=(0,0,0),velocity=(0,0,0))]
 expected=[];case_rows=[];objects=[]
 for packet in packets:
  for scenario in range(5):
   q=Original(scenario);q.expected_owner=44;q.parse(packet.hex());states=[]
   for step,now in enumerate((10.,10.01,10.11,10.2,10.4)):
    if scenario==1 and step>=2:
     q.u.mem_write(q.entity_address+0xc7c,struct.pack('<6f',200,400,600,90,180,270))
    if (scenario==2 and step>=1) or (scenario==3 and step>=2):q.current_stamp=78
    q.now=f32(now);q.rng_values=[-1.,.25,1.,1.25]*128;q.rng_index=0;q.call(0x2aa6650)
    base=0x600000+0xb414
    states.append([q.integer(base+0x10),q.integer(base+0x1c),bits(q.floats(base+0x28)[0]),q.integer(base+4)])
   particles=[]
   for slot in q.particles:
    p=slot['address']
    values=(*q.floats(p+0x94,3),*q.floats(p+0xb8,3),q.floats(p+0x6c)[0],q.floats(p+0x70)[0],
             q.floats(p+0x50)[0],q.floats(p+0x54)[0],q.floats(p+0x64)[0],q.floats(p+0xa8)[0])
    particles.append([bits(v) for v in values])
   expected.append(dict(states=states,particles=particles,rng=sum(e['operation']=='rng' for e in q.events)))
   case_rows.append(packet.hex()+' '+str(scenario));objects.append(q)
 p=subprocess.run([str(a.executable.resolve(strict=True)),'--sliding-runtime-oracle'],
                  input='\n'.join(case_rows)+'\n',text=True,capture_output=True,cwd=ROOT,timeout=60)
 if p.returncode:raise AssertionError(p.stderr)
 actual=[json.loads(line) for line in p.stdout.splitlines()]
 if len(actual)!=len(expected):raise AssertionError('Missing actual runtime cases')
 differences=[dict(case=i,original=x,actual=y) for i,(x,y) in enumerate(zip(expected,actual)) if x!=y]
 inputs += [S.identity(p) for p in [Path(__file__),a.executable.resolve(strict=True),
               ROOT/'native/csol/client/scenario/core/particle_effects.cpp',ROOT/'native/csol/scenario/bosses/teddy_sliding.hpp']]
 for q in objects:inputs+=q.inputs
 unique={i['path']:i for i in inputs};unchanged=all(S.identity(Path(i['path']))==i for i in unique.values())
 report=dict(success=unchanged and not differences,inputsUnchanged=unchanged,cases=len(expected),
             emissionFrames=len(expected)*5,differences=differences,inputs=list(unique.values()),sourceCopies=0,
             deployed=False,liveGameplay=False,performanceAccepted=False,
             boundary='Original parser/Add/Frame and actual factory/ctor/init instructions; supplied entity object, stamp, allocation/time/RNG boundaries. Acquisition/parse-count semantics independently proved by private IDA; actual SDK maps to typed native origin/messagenum and matched cl_parsecount. No original/host renderer, trace or network execution claim.')
 out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n','utf8')
 print(json.dumps({k:report[k] for k in ('success','inputsUnchanged','cases','emissionFrames')}))
 return 0 if report['success'] else 1
if __name__=='__main__':raise SystemExit(main())
