#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay original Teddy Blackhole events and writers with explicit ABI fixtures.

Actor feet, cylinder damage and engine serialization use controlled fixtures.
The original host origin getter and MP instructions execute unchanged under
Unicorn. This does not execute original client rendering.
"""
import argparse,hashlib,importlib.util,json,struct,sys
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from pathlib import Path
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EIP,UC_X86_REG_ESP
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('sliding',ROOT/'tests/csol-scenario-teddy-messages/sliding_event_oracle.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class Original(module.Original):
 def __init__(self,sample,sliding_evidence,evidence):
  super().__init__(sample,sliding_evidence)
  self.blackhole_verified=[]
  for name in ('event-and-helpers-raw-ida.json','registration-resources-raw-ida.json','host-table-constructor-raw-ida.json','host-getter-raw-ida.json'):
   path=evidence/name;self.inputs.append(module.identity(path))
   for row in module.windows(json.loads(path.read_text('utf8'))):
    address=int(row['addr'],16);data=bytes(int(b,16) for b in row['data'].split())
    assert bytes(self.u.mem_read(address,len(data)))==data,(name,hex(address))
    self.blackhole_verified.append(dict(evidence=name,va=hex(address),bytes=len(data),sha256=hashlib.sha256(data).hexdigest()))
  assert len(self.blackhole_verified)==22
  registration=evidence/'registration-complete-ida.json';bounds=evidence/'registration-instruction-boundaries.json'
  self.inputs += [module.identity(registration),module.identity(bounds)]
  captures=json.loads(registration.read_text())['result'];blocks=json.loads(bounds.read_text())
  assert len(captures)==len(blocks)==3
  for capture,block in zip(captures,blocks):
   assert capture['cursor']['done'] and not capture.get('truncated') and capture['count']==len(block['items'])
   assert [int(x['addr'],16) for x in capture['matches']]==[int(x['addr'],16) for x in block['items']]
   start,end=int(block['start'],16),int(block['end'],16)
   decoded=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(self.u.mem_read(start,end-start)),start))
   assert [(x.address,x.size) for x in decoded]==[(int(x['addr'],16),x['size']) for x in block['items']]
   assert decoded[-1].address+decoded[-1].size==end
  for address,expected in [(0x119fde14,b'models/concrete_gibs.mdl'),(0x119dc8bc,b'ScreenShake'),(0x119dcb1c,b'MPToCL')]:
   assert bytes(self.u.mem_read(address,len(expected)+1))==expected+b'\0'
  self.inputs+=[module.identity(Path(__file__)),module.identity(evidence/'instruction-boundaries.json'),module.identity(evidence/'event-and-helpers-raw-ida.json')]
  for index,f in enumerate(json.loads((evidence/'instruction-boundaries.json').read_text())):
   path=evidence/f'complete-ida-{index}.json';self.inputs.append(module.identity(path));ida=json.loads(path.read_text())
   while isinstance(ida,dict) and 'content' in ida:ida=json.loads(next(x['text'] for x in ida['content'] if x['type']=='text'))
   assert ida['instruction_count']==ida['total_instructions']==f['instructionCount'] and ida['cursor']['done']
   assert [int(x['addr'],16) for x in ida['asm']['lines']]==[int(x['addr'],16) for x in f['items']]
   for x in f['items']:
    address,size=int(x['addr'],16),x['size'];decoded=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(self.u.mem_read(address,size)),address))
    assert len(decoded)==1 and decoded[0].size==size
    self.allowed[address]=size
  getter_bounds=evidence/'host-getter-boundaries.json';getter_ida=evidence/'host-getter-complete-ida.json'
  self.inputs += [module.identity(getter_bounds),module.identity(getter_ida)]
  getter=json.loads(getter_bounds.read_text());capture=json.loads(getter_ida.read_text())['result']
  assert getter['success'] and getter['sampleSHA256']==module.MP_SHA
  assert self.dword(0x1181adbc+0x128)==0x10612440
  assert len(capture)==len(getter['ranges'])==2
  for region,ida in zip(getter['ranges'],capture):
   start,end=int(region['start'],16),int(region['endExclusive'],16)
   items=[x for x in getter['items'] if start<=int(x['addr'],16)<end]
   assert ida['cursor']['done'] and not ida.get('truncated') and ida['count']==len(items)==region['instructions']
   assert [int(x['addr'],16) for x in ida['matches']]==[int(x['addr'],16) for x in items]
   decoded=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(bytes(self.u.mem_read(start,end-start)),start))
   assert [(x.address,x.size) for x in decoded]==[(int(x['addr'],16),x['size']) for x in items]
   for item in items:self.allowed[int(item['addr'],16)]=item['size']
  self.callbacks={0x501000:'begin',0x501010:'end',0x501020:'byte',0x501030:'short',0x501040:'coord',0x501050:'float',0x501060:'long',0x501070:'string',0x501100:'feet',0x501120:'edictAt',0x113e8a30:'cylinder',0x1093cf40:'base',0x11408e50:'create',0x113d6a10:'configureBlackhole'}
  for slot,callback in [(0x12278664,0x501000),(0x12278668,0x501010),(0x1227866c,0x501020),(0x12278674,0x501030),(0x12278688,0x501040),(0x12278680,0x501050),(0x12278678,0x501060),(0x1227868c,0x501070),(0x122786d4,0x501120)]:self.word(slot,callback)
  self.u.mem_map(0x700000,0x20000)
  self.word(0x600100,0x601000);self.word(0x601000,0x603000);self.word(0x60101c,0x602000);self.word(0x602d58,0x602000);self.word(0x602000,0x604000);self.word(0x602008,0x607080);self.word(0x603084,0x501100);self.word(0x604128,0x10612440);self.word(0x603328,0x605000)
  self.word(0x122774d0,78);self.word(0x1227737c,79);self.word(0x606090,5)
  # Five engine slots: grounded, airborne, free, no private data, grounded+dormant.
  for i in range(1,6):
   ed=0x700000+i*0x1000;obj=0x710000+i*0x100;pev=0x711000+i*0x300
   self.word(ed,1 if i==3 else 0);self.word(ed+0x80,0 if i==4 else obj);self.word(obj+8,pev);self.word(pev+0x1c8,0 if i==2 else 0x80000200 if i==5 else 0x200);self.word(pev+0x238,ed)
  self.word(0x60500c,0x3f400000);self.word(0x605044,0x43fa0000)

 def hook(self,u,address,size,_):
  if address==0x10612440:
   assert u.reg_read(UC_X86_REG_ECX)==0x602000,'host getter this identity'
   pose=self.case['centers'][self.center_calls];self.center_calls+=1
   self.edges.append(dict(operation='center',value=pose))
   u.mem_write(0x607088,struct.pack('<3f',*pose))
  if address not in self.callbacks:
   assert self.allowed.get(address)==size,('unverified instruction',hex(address),size)
   self.executed.add(address);return
  op=self.callbacks[address];sp=u.reg_read(UC_X86_REG_ESP);ret,arg=struct.unpack('<2I',u.mem_read(sp,8));cleanup=4
  if op=='feet':
   assert u.reg_read(UC_X86_REG_ECX)==0x601000,'actor getter this identity'
   self.edges.append(dict(operation=op,value=self.case['feet']));u.mem_write(0x607040,struct.pack('<3f',10,-20,self.case['feet']));u.reg_write(UC_X86_REG_EAX,0x607040)
  elif op=='cylinder':
   assert u.reg_read(UC_X86_REG_ECX)==0x601000,'actor cylinder this identity'
   low,high,center,style,kick,flag=struct.unpack('<2fIiIi',u.mem_read(sp+4,24));self.edges.append(dict(operation=op,low=low,high=high,center=list(struct.unpack('<3f',u.mem_read(center,12))),style=style,kick=kick,flag=flag));cleanup=28
  elif op=='base':self.edges.append(dict(operation=op,event=arg));cleanup=8
  elif op=='create':
   name=bytes(u.mem_read(arg,50)).split(b'\0')[0].decode();self.edges.append(dict(operation=op,name=name));u.reg_write(UC_X86_REG_EAX,0x709000 if self.case['created'] else 0)
  elif op=='configureBlackhole':
   self.edges.append(dict(operation=op,args=list(struct.unpack('<2f',u.mem_read(sp+4,8)))));cleanup=12
  elif op=='edictAt':
   self.edges.append(dict(operation=op,index=arg));assert 1<=arg<=5;u.reg_write(UC_X86_REG_EAX,0x700000+arg*0x1000)
  elif op=='begin':
   assert self.open is None;route,message,origin,recipient=struct.unpack('<4I',u.mem_read(sp+4,16));self.open=dict(route=route,message=message,recipient=recipient,pvs=list(struct.unpack('<3f',u.mem_read(origin,12))) if origin else None,fields=[],payload=bytearray());self.edges.append(dict(operation=op,message=message))
  elif op=='end':
   self.open['payload']=self.open['payload'].hex();self.packets.append(self.open);self.edges.append(dict(operation=op,message=self.open['message']));self.open=None
   # Engine callback mutation cannot change a local center snapshot already copied.
   u.mem_write(0x607088,struct.pack('<3f',999,-999,555))
  else:
   assert self.open is not None;field=dict(operation=op,raw=arg)
   if op=='string':b=bytes(u.mem_read(arg,260)).split(b'\0')[0];field['value']=b.decode();encoded=b+b'\0'
   elif op in ('coord','float'):
    f=struct.unpack('<f',struct.pack('<I',arg))[0];field['value']=f;encoded=struct.pack('<H',int(f*8)&65535) if op=='coord' else struct.pack('<I',arg)
   else:encoded=struct.pack('<I',arg)[:{'byte':1,'short':2,'long':4}[op]]
   self.open['payload']+=encoded;self.open['fields'].append(field)
  u.reg_write(UC_X86_REG_ESP,sp+cleanup);u.reg_write(UC_X86_REG_EIP,ret)

 def run(self,event,variant):
  centers=[[10.1875+variant,-20.1875,30.0625],[100.1875,-200.1875+variant,300.0625],[-1000.,2000.,-3000.+variant]]
  self.case=dict(feet=50.25+variant,centers=centers,created=bool(variant%2));self.center_calls=0;self.packets=[];self.edges=[];self.open=None
  self.u.mem_write(0x605018,struct.pack('<f',200.0));self.u.mem_write(0x122bbdb0,struct.pack('<H',400+variant));self.u.mem_write(0x608000,struct.pack('<2I',0x500000,event&0xffffffff));self.u.reg_write(UC_X86_REG_ESP,0x608000);self.u.reg_write(UC_X86_REG_ECX,0x600104)

  try:self.u.emu_start(0x10997a90,0x500000,count=10000)
  except Exception:
   print("REPLAY FAILURE",event,variant,hex(self.u.reg_read(UC_X86_REG_EIP)),self.edges);raise
  assert self.open is None and self.u.reg_read(UC_X86_REG_ESP)==0x608008
  if event==1:
   assert self.center_calls==3 and [p['message'] for p in self.packets]==[23,79,79,78]
   assert self.packets[0]['pvs']==centers[1] and len(bytes.fromhex(self.packets[0]['payload']))==25
   assert [p['recipient'] for p in self.packets[1:3]]==[0x701000,0x705000]
   assert all(p['payload']=='004000100003' for p in self.packets[1:3])
   smoke=self.packets[3];assert smoke['route']==2 and smoke['fields'][0]['raw']==0x58 and smoke['fields'][1]['value']=='sprites/smoke_ia.spr'
   assert [x['value'] for x in smoke['fields'] if x['operation']=='coord']==centers[1]
   assert len(bytes.fromhex(smoke['payload']))==99
  else:
   assert self.center_calls==0 and not self.packets
   if event==0:
    assert self.edges[0]==dict(operation='create',name='blackhole')
    assert self.edges[1:]==([dict(operation='configureBlackhole',args=[500.0,0.75])] if self.case['created'] else [])
   elif event==1003:assert self.edges==[dict(operation='base',event=1003)]
   else:assert self.edges==[]
  return dict(event=event,variant=variant,model=400+variant,inputs=self.case,edges=self.edges,packets=self.packets)

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--mp',type=Path,required=True)
 parser.add_argument('--sliding-evidence',type=Path,required=True)
 parser.add_argument('--evidence',type=Path,required=True)
 parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args()
 assert not args.output.exists(),'output directory must be fresh'
 original=Original(args.mp,args.sliding_evidence,args.evidence)
 cases=[original.run(event,v) for event in [-1,0,1,2,1000,1001,1003] for v in range(3)]
 assert all(module.identity(Path(row['path']))==row for row in original.inputs)
 result=dict(success=True,cases=cases,inputs=original.inputs,inputsUnchanged=True,events=len(cases),packets=sum(len(c['packets']) for c in cases),verifiedBlackholeWindows=original.blackhole_verified,executedInstructions=[hex(x) for x in sorted(original.executed)],boundaries=dict(feetCenter='actor feet is explicit fixture; original host getter10612440 executes on controlled entvars origin, varied at each call',cylinder='explicit thiscall boundary; damage internals not executed',engine='explicit user-message IDs, edict layout and field serialization fixtures',originalClientExecuted=False,deployed=False,performanceMeasured=False))
 rows=[str(len(cases))]
 for c in cases:
  row=[c['event'],c['variant'],int(c['inputs']['created']),c['inputs']['feet'],c['model']]
  row += [x for center in c['inputs']['centers'] for x in center]
  row += [c['packets'][i]['payload'] for i in (0,1,3)] if c['packets'] else ['-','-','-']
  rows.append(' '.join(map(str,row)))
 args.output.mkdir(parents=True)
 (args.output/'blackhole_event_cases.txt').write_text('\n'.join(rows)+'\n',encoding='utf8')
 (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(success=True,events=result['events'],packets=result['packets'],instructions=len(original.executed),verifiedBlackholeWindows=len(original.blackhole_verified))))

if __name__=='__main__':main()
