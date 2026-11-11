#!/usr/bin/env python3
"""Execute SHA-pinned original impulse/stage/decay x86; compare all float bits."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import struct
import subprocess
from unicorn.x86_const import UC_X86_REG_ESI, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("aim_oracle", ROOT / "tests/cryoffear-aim/oracle.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
INPUT = struct.Struct("<13f3i")
OUTPUT = struct.Struct("<9f2i")

class Original(module.Original):
    def __init__(self, sample):
        super().__init__(sample)
        self.uc.mem_write(module.STUB + 0x80, b"\xA1" + module.u32(module.DATA+0x100) + b"\xC3")
        self.uc.mem_write(0x101B2698, module.u32(module.STUB + 0x80))
        self.ranges = {f"{a:08x}-{b:08x}": hashlib.sha256(self.uc.mem_read(a,b-a)).hexdigest()
                       for a,b in ((0x100AE9A0,0x100AEA0E),(0x100B0834,0x100B08A6),
                                   (0x100B2140,0x100B22D9),(0x10094510,0x10094588),
                                   (0x10094770,0x10094880),(0x10094880,0x100948B8))}
    def run(self,row):
        uc=self.uc
        state=row[:6]
        origin=row[6:9]
        view=row[9:12]
        dt,clients,camera,mode=row[12:]
        uc.mem_write(0x105595B4,struct.pack("<3f",*state[:3]))
        uc.mem_write(0x10559620,struct.pack("<3f",*state[3:]))
        uc.mem_write(0x10542BC0,module.u32(camera))
        uc.mem_write(0x10542BC4,struct.pack("<i",mode))
        uc.mem_write(module.DATA+0x100,struct.pack("<i",clients))
        sp=module.STACK+0x1000
        uc.mem_write(sp,module.u32(module.STOP)+struct.pack("<3f",*origin))
        uc.reg_write(UC_X86_REG_ESP,sp)
        uc.emu_start(0x100AE9A0,module.STOP,count=1000)
        uc.mem_write(module.HUD,bytes(0x100))
        uc.mem_write(module.HUD+0xC,struct.pack("<3f",*view))
        uc.mem_write(module.HUD+0x3C,struct.pack("<f",dt))
        uc.reg_write(UC_X86_REG_ESI,module.HUD)
        uc.reg_write(UC_X86_REG_ESP,sp)
        uc.emu_start(0x100B0834,0x100B08A6,count=2000)
        status=1 if camera and mode<=0 else 0
        return (bytes(uc.mem_read(0x105595B4,12))+bytes(uc.mem_read(0x10559620,12))+
                bytes(uc.mem_read(module.HUD+0xC,12))+struct.pack("<2i",status,0))

def cases():
    for clients in (-1,0,1,2,32):
        for dt in (-.25,-.0,0,.001,.016,.1,1,4):
            for camera,mode in ((0,0),(1,-1),(1,0),(1,1),(1,2)):
                yield (1.,-2.,3.,-40.,-10.,20.,2.,-1.,.5,20.,40.,60.,dt,clients,camera,mode)
    for value in (0.,-0.,.0009999999,.001,.001000001,89.,179.,1000.,1e-30):
        for dt in (0.,.016,-.1):
            yield (value,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,0.,dt,32,0,0)
    rng=random.Random(0xC0F11)
    for _ in range(5000):
        yield (*[rng.uniform(-300,300) for _ in range(6)],
               *[rng.uniform(-5,5) for _ in range(3)],
               *[rng.uniform(-180,180) for _ in range(3)],
               rng.uniform(-.2,.2),rng.choice((1,32)),rng.choice((0,1)),rng.choice((-1,0,1)))

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--sample",required=True,type=Path)
    parser.add_argument("--runner",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    args=parser.parse_args()
    original=Original(args.sample)
    rows=list(cases())
    for clients in (1,32):
        state=(0.,)*6
        for frame in range(360):
            impulse=(2.,-1.,.5) if frame in (0,30,90) else (0.,0.,0.)
            row=INPUT.unpack(INPUT.pack(*state,*impulse,0.,0.,0.,1/60,clients,0,0))
            state=struct.unpack("<6f",original.run(row)[:24])
            rows.append(row)
    payload=b"".join(INPUT.pack(*row) for row in rows)
    rows=list(INPUT.iter_unpack(payload))
    expected=b"".join(original.run(row) for row in rows)
    result=subprocess.run([str(args.runner.resolve()),"--oracle"],input=payload,capture_output=True,check=True)
    actual=result.stdout
    assert len(actual)==len(expected),(len(actual),len(expected),result.stderr)
    mismatches=[]
    for i,row in enumerate(rows):
        a=actual[i*OUTPUT.size:(i+1)*OUTPUT.size]
        e=expected[i*OUTPUT.size:(i+1)*OUTPUT.size]
        if a!=e:
            mismatches.append(dict(case=i,input=row,expected=list(struct.unpack("<11I",e)),actual=list(struct.unpack("<11I",a))))
    args.output.write_text(json.dumps(dict(sampleSha256=module.SHA,cases=len(rows),
        originalRanges=original.ranges,bitExactMismatches=len(mismatches),firstMismatches=mismatches[:8],
        trajectories=["SP_360frames","MP_360frames"],gameplay=False),indent=2),encoding="utf8")
    assert not mismatches, f"{len(mismatches)} viewpunch float-bit mismatches; see {args.output}"
    print(f"viewpunch: {len(rows)} original x86 differential cases, zero float-bit mismatches")
if __name__=="__main__":main()
