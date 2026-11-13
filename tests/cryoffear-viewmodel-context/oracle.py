# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay native render-only mirror and exact restore instructions; private sample stays local."""
import argparse,hashlib,json,struct
from pathlib import Path
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EBP,UC_X86_REG_ESP
SHA='b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2'
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sample',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();b=a.sample.read_bytes();assert hashlib.sha256(b).hexdigest()==SHA
 pe=struct.unpack_from('<I',b,60)[0];count=struct.unpack_from('<H',b,pe+6)[0];optional=pe+24;size=struct.unpack_from('<I',b,optional+56)[0];table=optional+struct.unpack_from('<H',b,pe+20)[0];u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000000,(size+4095)&~4095)
 for i in range(count):
  at=table+i*40;vs,rva,raw,off=struct.unpack_from('<4I',b,at+8);u.mem_write(0x10000000+rva,b[off:off+raw])
 u.mem_map(0x20000000,0x20000);cvar=0x20010000;ebp=0x2000ff00;cases=[]
 for game in [0,1]:
  for local in [0,1]:
   for flags in [0,1,2,3]:
    for knife in [0,1]:
     for elite in [0,1]:
      for hand in [0.,1.]:
       def run(h,k,e):
        u.mem_write(0x1010f860,bytes([e,k]));u.mem_write(0x1012ea30,struct.pack('<I',cvar));u.mem_write(cvar+12,struct.pack('<f',h));u.mem_write(ebp+8,struct.pack('<I',flags));u.mem_write(ebp-0x159,bytes(1))
        for reg,value in [(UC_X86_REG_EAX,local),(UC_X86_REG_ECX,game),(UC_X86_REG_EBP,ebp),(UC_X86_REG_ESP,ebp-0x180)]:u.reg_write(reg,value)
        u.emu_start(0x10066bc1,0x10066c35,count=100);inside=struct.unpack('<f',u.mem_read(cvar+12,4))[0];u.emu_start(0x10066f96,0x10066fb5,count=30);after=struct.unpack('<f',u.mem_read(cvar+12,4))[0];return inside,after
       original=run(hand,knife,elite);expected=1. if game and elite and local and flags&1 else (1.-hand if not game and knife and local and flags&1 else hand);assert original==(expected,hand)
       scoped=run(1.-hand,0,0);assert scoped==(1.-hand,1.-hand)
       cases.append(dict(game=game,local=local,flags=flags,knife=knife,elite=elite,hand=hand,original=original,onehandedScoped=scoped))
 out=a.output.absolute();root=Path(__file__).resolve().parents[2];assert not out.exists() and out.resolve().is_relative_to(root/'build');out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(dict(success=True,sampleSha256=SHA,cases=cases,originalCaseCount=len(cases),instructions='10066BC1..10066C35 and10066F96..10066FB5; no code patching',boundary='Original mirror branch only, no rendering/gameplay/performance'),indent=2),'utf8');print(json.dumps(dict(success=True,cases=len(cases))))
if __name__=='__main__':main()
