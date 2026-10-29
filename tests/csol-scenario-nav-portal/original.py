"""Bounded complete mode-15 portal + original edge/connectivity instructions."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,random,struct,subprocess,sys
sys.dont_write_bytecode=True
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDI,UC_X86_REG_ESI,UC_X86_REG_EBP,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_MXCSR
helper=Path(__file__).parents[1]/'csol-scenario/spawn_points_original.py'
spec=importlib.util.spec_from_file_location('portal_binary_reader',helper)
support=importlib.util.module_from_spec(spec);spec.loader.exec_module(support)
ENTRY=0x11341e40;END=0x113421da;EDGE=0x1134a000;CONNECTED=0x11349d20;MEMBER=0x10618f50
BASE=0x20000000;FROM=BASE;TO=BASE+0x1000;NEIGHBOR=BASE+0x2000;OUTPUT=BASE+0x3100;FROMPOS=BASE+0x3200
SERVICE=BASE+0x4000;VTABLE=BASE+0x4100;GETMODE=BASE+0x5000;GETSERVICE=BASE+0x5100;RANDOM=BASE+0x5200
STACK=0x21008000;STOP=0x22000000

def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]
def value(raw):return struct.unpack('<f',struct.pack('<I',raw))[0]
class Original:
    original=support.Original.original
    def __init__(self,mp,metadata):
        self.data=mp.read_bytes();self.meta=json.loads(metadata.read_text(encoding='utf-8-sig'))
        assert hashlib.sha256(self.data).hexdigest()==support.EXPECTED_MP==self.meta['sha256']
        self.u=Uc(UC_ARCH_X86,UC_MODE_32)
        for page in [0x11341000,0x11342000,0x11349000,0x1134a000,0x10618000,0x11740000,0x1186c000,0x119ab000,0x117f7000]:
            self.u.mem_map(page,4096);self.u.mem_write(page,self.original(page,4096))
        self.u.mem_map(0x12278000,4096)
        self.u.mem_map(BASE,0x10000);self.u.mem_map(0x21000000,0x10000);self.u.mem_map(STOP,4096)
        self.put(0x12278834,GETSERVICE);self.put(0x117404d4,RANDOM)
        self.put(SERVICE,VTABLE);self.put(VTABLE+0x190,GETMODE)
        for address in [GETSERVICE,GETMODE,RANDOM,MEMBER,STOP]:self.u.hook_add(UC_HOOK_CODE,self.hook,begin=address,end=address)
    def get(self,address):return struct.unpack('<I',self.u.mem_read(address,4))[0]
    def put(self,address,number):self.u.mem_write(address,struct.pack('<I',number&0xffffffff))
    def ret(self,result,pop=0):
        sp=self.u.reg_read(UC_X86_REG_ESP);target=self.get(sp)
        self.u.reg_write(UC_X86_REG_EAX,result&0xffffffff)
        self.u.reg_write(UC_X86_REG_ESP,sp+4+pop);self.u.reg_write(UC_X86_REG_EIP,target)
    def hook(self,u,address,size,unused):
        if address==STOP:self.stopped=True;u.emu_stop();return
        if address==GETSERVICE:self.service_calls+=1;self.ret(SERVICE)
        elif address==GETMODE:
            assert u.reg_read(UC_X86_REG_ECX)==SERVICE;self.mode_calls+=1;self.ret(15)
        elif address==MEMBER:
            sp=u.reg_read(UC_X86_REG_ESP)
            arguments=[self.get(sp+4+i*4) for i in range(10)]
            assert arguments==[15,15,17,18,26,28,33,48,59,0xfffffffe],arguments
            self.member_calls+=1;self.ret(1)
        elif address==RANDOM:self.random_calls+=1;self.ret(self.sample)
    def area(self,address,extent):
        self.u.mem_write(address,bytes([0xa5])*0x400)
        self.u.mem_write(address+8,struct.pack('<6I',*extent))
        for direction in range(4):
            sentinel=BASE+0x6000+(address-BASE)//0x1000*0x100+direction*0x20
            self.put(address+0x1c8+direction*8,sentinel);self.put(address+0x1cc+direction*8,0)
            self.put(sentinel,sentinel);self.put(sentinel+4,sentinel);self.put(sentinel+8,0)
    def connect(self,address,direction,target):
        sentinel=self.get(address+0x1c8+direction*8);node=sentinel+0x10
        self.put(sentinel,node);self.put(sentinel+4,node)
        self.put(node,sentinel);self.put(node+4,sentinel);self.put(node+8,target)
        self.put(address+0x1cc+direction*8,1)
    def case(self,row,sample):
        direction,seed,geometry,edge_mask=row
        self.area(FROM,geometry[:6]);self.area(TO,geometry[6:12]);self.area(NEIGHBOR,geometry[:6])
        # A bit represents a real reciprocal adjacency on that side. Original
        # IsEdge/IsConnected execute these original-layout lists, not hooks.
        for side in range(4):
            if edge_mask&(1<<side):
                self.connect(TO,side,NEIGHBOR);self.connect(NEIGHBOR,(side+2)%4,TO)
        before_from=bytes(self.u.mem_read(FROM,0x400));before_to=bytes(self.u.mem_read(TO,0x400))
        self.u.mem_write(OUTPUT-8,bytes([0xa5])*32)
        self.u.mem_write(OUTPUT,struct.pack('<3I',*geometry[12:15]))
        self.u.mem_write(FROMPOS,struct.pack('<3f',-8192,8192,123))
        self.put(STACK,STOP);self.put(STACK+4,TO);self.put(STACK+8,direction);self.put(STACK+12,FROMPOS);self.put(STACK+16,OUTPUT)
        self.u.reg_write(UC_X86_REG_ECX,FROM);self.u.reg_write(UC_X86_REG_ESP,STACK);self.u.reg_write(UC_X86_REG_MXCSR,0x1f80)
        for register in [UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI]:self.u.reg_write(register,0x12345678)
        self.sample=sample;self.stopped=False;self.random_calls=self.member_calls=self.service_calls=self.mode_calls=0
        self.u.emu_start(ENTRY,0,count=1500)
        assert self.stopped and self.u.reg_read(UC_X86_REG_EIP)==STOP and self.u.reg_read(UC_X86_REG_ESP)==STACK+20
        for register in [UC_X86_REG_EBP,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_EDI]:assert self.u.reg_read(register)==0x12345678
        assert (self.random_calls,self.member_calls,self.service_calls,self.mode_calls)==(1,1,2,2)
        data=bytes(self.u.mem_read(OUTPUT-8,32));assert data[:8]==b'\xa5'*8 and data[20:]==b'\xa5'*12
        assert bytes(self.u.mem_read(FROM,0x400))==before_from and bytes(self.u.mem_read(TO,0x400))==before_to
        result=list(struct.unpack('<3I',self.u.mem_read(OUTPUT,12)));assert result[2]==geometry[14]
        return result

def matrix():
    layouts=[
        ([0,20,4,100,120,8],[10,40,-5,90,100,7]),
        ([0,-10,0,200,300,0],[0,1,0,value(bits(37.5)-1),2,0]),
        ([0,-10,0,200,300,0],[0,1,0,37.5,2,0]),
        ([0,-10,0,200,300,0],[0,1,0,value(bits(37.5)+1),2,0]),
        ([0,0,0,100,100,0],[0,0,0,5,5,0]),
        ([0,0,0,100,100,0],[50,50,0,50,50,0]),
        ([0,20,0,100,120,0],[150,140,0,170,190,0]),
        ([0,20,0,100,120,0],[-80,-60,0,-20,-40,0]),
        ([-100,-200,-30,-40,-80,90],[-90,-190,500,-45,-100,-500]),
        ([0,0,0,100,100,0],[25,25,0,50,50,0]),
        ([0,0,0,0,0,0],[0,0,0,0,0,0]),
    ]
    rows=[]
    for first,second in layouts:
        for direction in range(4):
            for seed in range(32):rows.append((direction,seed,[bits(v) for v in first+second]+[bits(-129),bits(239),0x7fc12345],0))
    geometry=[bits(v) for v in layouts[0][0]+layouts[0][1]]+[0x80000000,bits(-1),0x80000000]
    for mask in range(16):
        for direction in range(4):rows.append((direction,mask+77,geometry,mask))
    rng=random.Random(0xc5011504)
    for _ in range(64):
        first=[rng.uniform(-5000,4000),rng.uniform(-5000,4000),rng.uniform(-100,100)]
        first += [first[0]+rng.uniform(0,500),first[1]+rng.uniform(0,500),rng.uniform(-100,100)]
        second=[first[0]+rng.uniform(-200,200),first[1]+rng.uniform(-200,200),0]
        second += [second[0]+rng.uniform(0,500),second[1]+rng.uniform(0,500),0]
        rows.append((rng.randrange(4),rng.randrange(65536),[bits(v) for v in first+second]+[0,0,bits(123)],rng.randrange(16)))
    return rows
def main():
    parser=argparse.ArgumentParser()
    for name in ['mp','metadata','exe','output']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();rows=matrix()
    stream='\n'.join(' '.join(map(str,[direction,seed,*geometry])) for direction,seed,geometry,mask in rows)+'\n'
    result=subprocess.run([str(args.exe.resolve()),'--oracle'],input=stream,text=True,capture_output=True,timeout=30)
    outputs=[list(map(int,line.split())) for line in result.stdout.splitlines()]
    original=Original(args.mp,args.metadata);failures=[];residues=set()
    for index,(row,got) in enumerate(zip(rows,outputs)):
        assert len(got)==7;accepted,sample,next_sample,after,*point=got;residues.add(sample%10)
        expected=original.case(row,sample)
        if not accepted or after!=next_sample or point!=expected:
            failures.append({'index':index,'direction':row[0],'seed':row[1],'inputBits':row[2],'reciprocalMask':row[3],
                'sample':sample,'expectedBits':expected,'actual':got})
    complete=result.returncode==0 and len(rows)==len(outputs) and residues==set(range(10))
    ranges=[(ENTRY,END),(EDGE,0x1134a0ec),(CONNECTED,0x11349e0b)]
    report={'scope':'mode15 full original portal/IsEdge/IsConnected instructions against production with actual SDK extents',
        'cases':len(rows),'complete':complete,'failures':failures,'crtResidues':sorted(residues),'returnCode':result.returncode,'stderr':result.stderr,
        'ranges':[{'start':hex(start),'endExclusive':hex(end),'sha256':hashlib.sha256(original.original(start,end-start)).hexdigest()} for start,end in ranges],
        'instructionLimit':1500,'checks':['STOP/EIP','ret16 stack','EBP/EBX/ESI/EDI','one rand call','mode/membership calls','area immutability','output canaries/Z bits','next CRT value'],
        'substitutes':['Mode service returns15; explicit membership arguments verified then returns true','Original rand import returns sampled real CRT value from test EXE'],
        'inputs':[support.evidence(p) for p in [args.mp,args.metadata,args.exe,Path(__file__),helper]],
        'notCovered':['Original CRT generator equivalence','Other game modes','Full paths/NAV loading/BSP mapping/AI/live gameplay','Production finite rejection behavior has no original-equivalence claim']}
    args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'cases':len(rows),'complete':complete,'failures':len(failures),'crtResidues':sorted(residues)}))
    return 0 if complete and not failures else 1
if __name__=='__main__':raise SystemExit(main())
