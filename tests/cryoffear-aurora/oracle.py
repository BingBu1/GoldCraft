#!/usr/bin/env python3
"""Run original Aurora parser/pool/manager/update/draw versus native lifecycle."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location("probe",ROOT/"tests/cryoffear-aurora/evidence_probe.py")
probe=importlib.util.module_from_spec(spec);spec.loader.exec_module(probe)
base=probe.base
INPUT=struct.Struct("<5f3i")
PARTICLE=struct.Struct("<3i32f")
HEADER=struct.Struct("<6i3f")
OUTPUT_SIZE=HEADER.size+20*PARTICLE.size
VIEW=probe.HEAP+0x8000
OWNER=probe.HEAP+0x9000
MODEL=probe.HEAP+0xA000
SPRITE=probe.HEAP+0xA800
TRI=base.CVARS+0x200
class Original(probe.Probe):
    def __init__(self,sample,script,owner=5):
        self.randoms=0;self.hasview=True;self.owner=owner
        super().__init__(sample,0)
        # Constructor/parser/pool is original code; zero raw seed is the authorized fix.
        self.run(script)
        uc=self.uc
        self.pool=struct.unpack("<I",uc.mem_read(base.HUD+0xB4,4))[0]
        main=struct.unpack("<I",uc.mem_read(base.HUD+0xC4,4))[0]
        self.types=[main,struct.unpack("<I",uc.mem_read(main+0x128,4))[0]]
        # Discard only the probe's frame; restore fresh original pool state.
        self.call(0x100817C0,(20,),base.HUD)
        self.pool=struct.unpack("<I",uc.mem_read(base.HUD+0xB4,4))[0]
        uc.mem_write(base.HUD+0xC,base.u32(owner))
        uc.mem_write(base.HUD+0xC4,base.u32(self.types[0]))
        uc.mem_write(base.HUD+0x98,bytes(0x14))
        uc.mem_write(base.HUD+0xA8,bytes(4))
        uc.mem_write(base.HUD+0x18,b"models/test.mdl\0")
        uc.mem_write(0x105448C8,base.u32(base.HUD))
        uc.mem_write(VIEW+0xB94,base.u32(MODEL))
        uc.mem_write(MODEL,b"models/test.mdl\0")
        uc.mem_write(SPRITE+0x48,base.u32(1))
        self.randoms=0;self.hasview=True
        for i in range(450):
            import math
            uc.mem_write(0x105448D0+4*i,struct.pack("<f",math.sin(i*math.pi/180)))
        for slot,stub in ((0x101B26D4,0x300),(0x101B26D8,0x320),(0x101B26DC,0x340),
                          (0x101B2690,0x360),(0x101B2668,0x380),(0x101B2700,0x3A0)):
            uc.mem_write(slot,base.u32(base.STUB+stub));uc.mem_write(base.STUB+stub,b"\xC3")
        uc.mem_write(0x101B2750,base.u32(TRI))
        for slot in (4,8,12,16,24,28,44):uc.mem_write(TRI+slot,base.u32(base.STUB+0x3C0))
        uc.mem_write(base.STUB+0x3C0,b"\xB8\x01\x00\x00\x00\xC3")
    def service(self,uc,address,size,data):
        if address==base.STUB+0x280:
            low,high=struct.unpack("<2f",uc.mem_read(uc.reg_read(UC_X86_REG_ESP)+4,8))
            unit=base.f32(((self.randoms*37+11)%101)/100)
            self.randoms+=1
            value=base.f32(low+(high-low)*unit)
            uc.mem_write(base.DATA+0x180,struct.pack("<f",value))
            uc.mem_write(base.STUB+0x400,b"\xD9\x05"+base.u32(base.DATA+0x180)+b"\xC3")
            uc.reg_write(UC_X86_REG_EIP,base.STUB+0x400)
        elif address==base.STUB+0x300:self.ret(OWNER)
        elif address==base.STUB+0x320:self.ret(VIEW if self.hasview else 0)
        elif address==base.STUB+0x340:self.ret(OWNER)
        elif address==base.STUB+0x360:self.ret()
        elif address==base.STUB+0x380:
            _,forward,right,up=self.args(4)
            for ptr,values in ((forward,(1,0,0)),(right,(0,1,0)),(up,(0,0,1))):uc.mem_write(ptr,struct.pack("<3f",*values))
            self.ret()
        elif address==base.STUB+0x3A0:self.ret(SPRITE)
        elif address==0x10072710:self.ret(0)
        elif address==0x100B42BB:self.ret()
        else:super().service(uc,address,size,data)
    def slot(self,pointer):return -1 if pointer==0 else (pointer-self.pool)//136
    def snapshot(self):
        uc=self.uc
        present=struct.unpack("<I",uc.mem_read(0x105448C8,4))[0]
        if not present:return HEADER.pack(0,0,0,0,0,self.randoms,0.,0.,0.)+bytes(20*PARTICLE.size)
        state=struct.unpack("<i",uc.mem_read(base.HUD+0x98,4))[0]
        head,free,main=[self.slot(struct.unpack("<I",uc.mem_read(base.HUD+off,4))[0]) for off in (4,0xB8,0xBC)]
        data=HEADER.pack(1,state,head,free,main,self.randoms,*struct.unpack("<3f",uc.mem_read(base.HUD+0x9C,12)))
        for i in range(20):
            p=self.pool+i*136
            next,_,typ=struct.unpack("<3I",uc.mem_read(p,12))
            owner=struct.unpack("<i",uc.mem_read(p+0x3C,4))[0]
            floats=bytes(uc.mem_read(p+0xC,48))+bytes(uc.mem_read(p+0x40,72))+bytes(8)
            data+=struct.pack("<3i",self.slot(next),self.types.index(typ) if typ else 0,owner)+floats
        return data
    def frame(self,row):
        dt,now,x,y,z,body,mode,command=row;uc=self.uc
        uc.mem_write(base.DATA,struct.pack("<f",now))
        uc.mem_write(VIEW+0xB60,struct.pack("<3f",x,y,z))
        uc.mem_write(OWNER+0x304,base.u32(body))
        if command in (1,2):self.call(0x10080B90,(command-1,),0x105448C8)
        if command==3:self.call(0x10080B40,(self.owner,),0x105448C8)
        if command==4:self.call(0x10080B00,(),0x105448C8)
        if command==5:uc.mem_write(MODEL,b"models/other.mdl\0")
        self.hasview=command!=6
        self.call(0x10080C90,(base.bits(dt),mode),0x105448C8)
        return self.snapshot()

def scenarios():
    for name in ("natural","softkill","hardkill_bodyguard","model_change","missing_view","delete","clear"):
        rows=[]
        for frame in range(340):
            command=0
            if frame==20:command={"softkill":1,"hardkill_bodyguard":2,"model_change":5,"missing_view":6,"delete":3,"clear":4}.get(name,0)
            body=70 if name=="hardkill_bodyguard" and 20<=frame<25 else 0
            rows.append((0.016,frame*0.016,10+frame*.02,20.,30.,body,0,command))
        yield name,rows,5
        if name in ("natural","delete","hardkill_bodyguard"):
            yield "owner_zero_"+name,rows,0
    yield "zero_dt_strict_rate_pool",[(0.,0.,10.,20.,30.,0,0,0)]+[(.04,i*.04,10.,20.,30.,0,0,0) for i in range(160)],5

def main():
    p=argparse.ArgumentParser();p.add_argument("--sample",type=Path,required=True)
    p.add_argument("--runner",type=Path,required=True);p.add_argument("--output",type=Path,required=True)
    args=p.parse_args();script_path=args.sample.parent.parent/"aurora-gunsmoke-original.txt"
    script=script_path.read_bytes();mismatches=[];total=0;names=[]
    for name,rows,owner in scenarios():
        original=Original(args.sample,script,owner)
        payload=b"".join(INPUT.pack(*r) for r in rows);rows=list(INPUT.iter_unpack(payload))
        expected=b"".join(original.frame(row) for row in rows)
        actual=subprocess.run([str(args.runner.resolve()),str(script_path.resolve()),"--oracle",str(owner)],input=payload,capture_output=True,check=True).stdout
        assert len(actual)==len(expected),(name,len(actual),len(expected))
        for i,row in enumerate(rows):
            a=actual[i*OUTPUT_SIZE:(i+1)*OUTPUT_SIZE];e=expected[i*OUTPUT_SIZE:(i+1)*OUTPUT_SIZE]
            if a!=e:
                differing=[j for j in range(0,OUTPUT_SIZE,4) if a[j:j+4]!=e[j:j+4]]
                mismatches.append(dict(scenario=name,frame=i,input=row,differing=differing[:15],
                    actual=[struct.unpack("<I",a[j:j+4])[0] for j in differing[:15]],
                    expected=[struct.unpack("<I",e[j:j+4])[0] for j in differing[:15]]))
        total+=len(rows);names.append(name)
        print(name,len(rows),"frames",len(mismatches),"mismatches",flush=True)
    args.output.write_text(json.dumps(dict(sampleSha256=base.SHA,cases=total,scenarios=names,
        bitExactMismatches=len(mismatches),firstMismatches=mismatches[:8],
        authorizedDeviation="Zero initialization only of proven raw Type controls and initial particle positions",
        originalExecution="Actual resource parser, type merge, pool, manager, local lifecycle, emission/motion and renderer instructions",
        modeledServices=["raw allocation zero", "CRT leaf parser helpers", "engine time/entity/model/random/AngleVectors/Triangle SDK"],
        gameplay=False,performance=False),indent=2),encoding="utf8")
    assert not mismatches,f"{len(mismatches)} Aurora lifecycle mismatches; see {args.output}"
    print(f"Aurora full lifecycle: {total} original x86 frames, zero state/float-bit/RNG-count mismatches")
if __name__=="__main__":main()
