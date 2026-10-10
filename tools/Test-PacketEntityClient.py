"""Run the exact workspace hw10210 full/delta packet decoder in x86 Unicorn.

Original bit readers, delta-field decoding, frame reconstruction and state
publication execute unchanged. External allocation/registry/model/animation
services are supplied by the fixture. No game startup or live input is used.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parent.parent
EXPECTED = "9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295"


class Bits:
    def __init__(self): self.bits = []
    def put(self, value, width): self.bits.extend((value >> i) & 1 for i in range(width))
    def bytes(self):
        return bytes(sum(self.bits[j+i] << i for i in range(min(8, len(self.bits)-j)))
                     for j in range(0, len(self.bits), 8))


class EngineError(Exception): pass


class Engine:
    def __init__(self, path):
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != EXPECTED: raise ValueError("Unverified engine")
        pe = struct.unpack_from("<I", data, 60)[0]
        if data[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", data, pe+4)[0] != 0x14c:
            raise ValueError("Expected the verified x86 PE")
        count, optional = struct.unpack_from("<H", data, pe+6)[0], struct.unpack_from("<H", data, pe+20)[0]
        self.base, size = struct.unpack_from("<I", data, pe+52)[0], struct.unpack_from("<I", data, pe+80)[0]
        self.mu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.mu.mem_map(self.base, (size+4095) & ~4095)
        for i in range(count):
            _, rva, length, offset = struct.unpack_from("<IIII", data, pe+24+optional+i*40+8)
            if length: self.mu.mem_write(self.base+rva, data[offset:offset+length])
        self.stack, self.heap, self.entities, self.registry, self.packet = 0x20000000,0x21000000,0x23000000,0x24000000,0x25000000
        self.mu.mem_map(self.stack, 0x10000); self.mu.mem_map(self.heap, 0x2000000)
        self.mu.mem_map(self.entities, 0x600000); self.mu.mem_map(self.registry,0x100000)
        self.mu.mem_map(self.packet,0x20000)
        # The mapped image has an unresolved CRT _CIfmod import. Supply the
        # equivalent x87 primitive so original angle normalization executes.
        # Stack convention:ST0 divisor,ST1 dividend -> ST0 remainder.
        self.mu.mem_write(self.registry+0x80,bytes.fromhex("d9 c9 d9 f8 df e0 f6 c4 04 75 f7 dd d9 c3"))
        self.set(0x2b2450,self.registry+0x80)
        self.next_heap = self.heap+4096
        self.allocations, self.freed, self.registrations, self.events, self.warnings = {}, [], {}, [], []
        self.rejections, self.publications, self.delta_calls = [], 0, 0
        self.stop = self.stack+0x100
        self.set(0x32ead8,self.entities); self.set(0x13fcc44,32); self.set(0x13f540c,0)
        self.set(0x1257f60,1); self.set(0x31a230,63); self.set(0x1407748,0)
        self.mu.mem_write(self.base+0x1282ee8,struct.pack("<d",12.5))
        fields = [dict(fieldType=8,fieldName="modelindex",fieldOffset=40,fieldSize=4,significant_bits=9,premultiply=1,postmultiply=1),
                  dict(fieldType=0x80000004,fieldName="origin[0]",fieldOffset=16,fieldSize=4,significant_bits=16,premultiply=8,postmultiply=1),
                  dict(fieldType=8,fieldName="effects",fieldOffset=64,fieldSize=4,significant_bits=16,premultiply=1,postmultiply=1)]
        self.schema({name: fields for name in ("entity_state_t","entity_state_player_t","custom_entity_state_t","event_t")})
        # Observe only fixture dependencies and routine entries. Calling
        # Python for every unchanged decoder instruction obscures the actual
        # engine work and needlessly slows large real server captures.
        for rva in (0x1bfdd0,0x1da430,0x1da3e0,0x1bb020,0x2ad9c8,0x1bafc0,0x2ad9ce,
                    0x1d26e0,0x1bc070,0x1bb8c0,0x1a2fa0,0x1fa050,0x1a0ce0,0x19e9c0,
                    0x1f6100,0x1a0c40,0x1a6b80,0x2ab707,0x19d8e0,0x1edd10,0x1d9f30,
                    0x19fd20,0x1c0090,0x1aa750):
            self.mu.hook_add(UC_HOOK_CODE,self.hook,begin=self.base+rva,end=self.base+rva)

    def integer(self, at): return struct.unpack("<I",self.mu.mem_read(at,4))[0]
    def set(self,rva,value): self.mu.mem_write(self.base+rva,struct.pack("<I",value & 0xffffffff))
    def string(self, at):
        value = bytearray()
        while (ch := bytes(self.mu.mem_read(at+len(value),1))) != b"\0": value.extend(ch)
        return value.decode("ascii",errors="replace")
    def schema(self, definitions):
        self.registrations.clear()
        at = self.registry+4096
        for name,fields in definitions.items():
            pointer, descriptor, field_table = at,at+4,at+64
            self.mu.mem_write(pointer,struct.pack("<I",descriptor))
            self.mu.mem_write(descriptor,struct.pack("<II",0,len(fields)))
            self.mu.mem_write(descriptor+44,struct.pack("<I",field_table))
            for i,f in enumerate(fields):
                packed = struct.pack("<I32sih2xiffh2xII",f["fieldType"] & 0xffffffff,
                    f["fieldName"].encode()[:31],f["fieldOffset"],f["fieldSize"],f["significant_bits"],
                    f["premultiply"],f["postmultiply"],0,0,0)
                assert len(packed) == 68
                self.mu.mem_write(field_table+i*68,packed)
            self.registrations[name] = pointer
            at = (field_table+len(fields)*68+63) & ~63
    def baselines(self,states):
        assert len(states)%340 == 0 and len(states)//340 <= 2048
        for i in range(len(states)//340): self.mu.mem_write(self.entities+i*3000+8,states[i*340:(i+1)*340])
    def ret(self, value=0):
        sp = self.mu.reg_read(UC_X86_REG_ESP)
        destination = self.integer(sp)
        self.mu.reg_write(UC_X86_REG_EAX,value & 0xffffffff)
        self.mu.reg_write(UC_X86_REG_ESP,sp+4); self.mu.reg_write(UC_X86_REG_EIP,destination)
    def hook(self,mu,address,size,unused):
        rva = address-self.base
        def arg(index): return self.integer(mu.reg_read(UC_X86_REG_ESP)+4+index*4)
        if rva == 0x1bfdd0:
            name = self.string(arg(0)); self.ret(self.registrations[name])
        elif rva == 0x1da430:
            size = arg(0)
            if size > 0x100000: raise EngineError("invalid fixture allocation")
            at = self.next_heap; self.next_heap = (at+size+8191) & ~4095
            self.allocations[at] = size
            mu.mem_write(at-4,b"HEAD"); mu.mem_write(at,b"\0"*size); mu.mem_write(at+size,b"TAIL")
            self.ret(at)
        elif rva == 0x1da3e0:
            at = arg(0)
            assert at in self.allocations and at not in self.freed
            assert bytes(mu.mem_read(at-4,4)) == b"HEAD" and bytes(mu.mem_read(at+self.allocations[at],4)) == b"TAIL"
            self.freed.append(at); self.ret()
        elif rva == 0x1bb020 or rva == 0x2ad9c8:
            at,val,size = arg(0),arg(1),arg(2); mu.mem_write(at,bytes([val & 255])*size); self.ret(at)
        elif rva in (0x1bafc0,0x2ad9ce):
            dest,src,size = arg(0),arg(1),arg(2); mu.mem_write(dest,bytes(mu.mem_read(src,size))); self.ret(dest)
        elif rva == 0x1d26e0:
            self.rejections.append(self.string(arg(0))); raise EngineError(self.rejections[-1])
        elif rva in (0x1bc070,0x1bb8c0): self.warnings.append(self.string(arg(0))); self.ret()
        elif rva in (0x1a2fa0,0x1fa050,0x1a0ce0,0x19e9c0,0x1f6100,0x1a0c40,0x1a6b80,0x2ab707,0x19d8e0,0x1edd10,0x1d9f30): self.ret()
        elif rva == 0x19fd20: self.publications += 1
        elif rva == 0x1c0090: self.delta_calls += 1
        elif rva == 0x1aa750:
            self.events.append(dict(event=arg(1),state=bytes(mu.mem_read(arg(3),72)).hex())); self.ret()

    def call(self,rva,*args):
        sp = self.stack+0x8000
        self.mu.mem_write(sp,struct.pack("<"+"I"*(1+len(args)),self.stop,*args))
        self.mu.reg_write(UC_X86_REG_ESP,sp)
        self.mu.emu_start(self.base+rva,self.stop,count=50000000)
        assert self.mu.reg_read(UC_X86_REG_EIP) == self.stop, "original routine did not return"
        return self.mu.reg_read(UC_X86_REG_EAX)
    def prepare(self,payload,sequence,svc=False):
        self.mu.mem_write(self.packet,payload+b"\0"*16)
        self.set(0x1245fe8,self.packet);self.set(0x1245fec,65536);self.set(0x1245ff0,len(payload))
        self.set(0x124c984,int(svc));self.set(0x124c980,0)
        self.set(0x1282a84,sequence);self.set(0x1282a88,sequence & 63);self.set(0x1408e20,sequence)
    def parse(self,payload,sequence,delta=False,svc=False):
        self.prepare(payload,sequence,svc);self.call(0x19f0d0,int(delta),0)
        slot = self.base+0x1282f08+(sequence & 63)*17176
        count,pointer = self.integer(slot+17020),self.integer(slot+17152)
        states = bytes(self.mu.mem_read(pointer,count*340)) if pointer else b""
        return count,pointer,states,bytes(self.mu.mem_read(slot+17024,128))
    def canaries(self):
        return all(bytes(self.mu.mem_read(p-4,4)) == b"HEAD" and bytes(self.mu.mem_read(p+s,4)) == b"TAIL"
                   for p,s in self.allocations.items())
    def publication_matches(self,states):
        for i in range(len(states)//340):
            expected = bytearray(states[i*340:(i+1)*340])
            number = struct.unpack_from("<i",expected,4)[0]
            # Original publication normalizes the three angles through its
            # unchanged VectorAngles-style180-degree bounds and CRT fmod.
            for offset in (28,32,36):
                angle = struct.unpack_from("<f",expected,offset)[0]
                if angle > 180: angle = math.fmod(angle,360)-360
                elif angle < -180: angle = math.fmod(angle,360)+360
                struct.pack_into("<f",expected,offset,angle)
            movetype = struct.unpack_from("<i",expected,88)[0]
            if movetype not in (0,3,4,5) and struct.unpack_from("<ff",expected,252) == (0,0):
                expected[92:96] = expected[8:12] # Native publication derives animtime from msg_time.
            if bytes(self.mu.mem_read(self.entities+number*3000+688,340)) != expected: return False
        return True


def compare_fields(parsed, expected, definition):
    """Compare every encoded field, allowing its actual wire quantization."""
    failures = []
    for i in range(len(parsed)//340):
        if struct.unpack_from("<i",parsed,i*340+4)[0] != struct.unpack_from("<i",expected,i*340+4)[0]:
            failures.append(dict(entity=i,field="number"));continue
        number = struct.unpack_from("<i",parsed,i*340+4)[0]
        fields = definition["entity_state_player_t"] if 1<=number<=32 else definition["entity_state_t"]
        for field in fields:
            offset = i*340+field["fieldOffset"];kind = field["fieldType"] & 0x7fffffff
            if kind in (4,16,32,64):
                actual,wanted = struct.unpack_from("<f",parsed,offset)[0],struct.unpack_from("<f",expected,offset)[0]
                quantum = (360/(1 << field["significant_bits"]) if kind == 16 else
                    0.01 if kind == 32 else 1/max(abs(field["premultiply"]),1))
                error = abs(actual-wanted)
                if kind == 16: error = min(error,abs(error-360))
                if error > quantum+1e-4: failures.append(dict(entity=i,field=field["fieldName"],actual=actual,expected=wanted))
            else:
                size = 1 if kind == 1 else 2 if kind == 2 else field["fieldSize"] if kind == 128 else 4
                if parsed[offset:offset+size] != expected[offset:offset+size]:
                    failures.append(dict(entity=i,field=field["fieldName"],actual=parsed[offset:offset+size].hex(),expected=expected[offset:offset+size].hex()))
    return failures


def full(count):
    bits = Bits()
    for i in range(count):
        if i == 0: bits.put(0,1);bits.put(1,1);bits.put(33,11)
        else: bits.put(1,1)
        bits.put(0,1);bits.put(0,1) # ordinary entity, existing per-slot baseline
        bits.put(1,3);bits.put(7,8)
        bits.put(77,9);bits.put(0,1);bits.put(i,15);bits.put(i+33,16)
    bits.put(0,16)
    return struct.pack("<h",count)+bits.bytes()


def delta(count,previous,changes=()):
    bits = Bits();last = 0
    for slot,removed,model in changes:
        bits.put(int(removed),1)
        distance = slot-last
        if 0 <= distance < 64:bits.put(0,1);bits.put(distance,6)
        else:bits.put(1,1);bits.put(slot,11)
        last = slot
        if not removed:
            bits.put(0,1);bits.put(1,3);bits.put(1,8);bits.put(model,9)
    bits.put(0,16)
    return struct.pack("<hB",count,previous & 255)+bits.bytes()


def run(args):
    path = args.engine.resolve(strict=True)
    if not path.is_relative_to(ROOT): raise ValueError("Use the workspace copy")
    engine = Engine(path);checks = {};frames = []
    for sequence,count in enumerate((256,257,512,1024),65):
        payload = full(count)
        n,pointer,states,resets = engine.parse(payload,sequence)
        checks[f"full{count}Count"] = n == count and engine.allocations[pointer] == count*340
        checks[f"full{count}AllStates"] = all(struct.unpack_from("<i",states,i*340+4)[0] == i+33 and
            struct.unpack_from("<i",states,i*340+40)[0] == 77 and
            struct.unpack_from("<f",states,i*340+16)[0] == i/8 and
            struct.unpack_from("<i",states,i*340+64)[0] == i+33 and
            struct.unpack_from("<i",states,i*340+12)[0] == sequence for i in range(count))
        checks[f"full{count}ResetBits"] = resets[:count//8] == b"\xff"*(count//8)
        frames.append(dict(count=count,bytes=len(payload),decodedSha256=hashlib.sha256(states).hexdigest()))
    before = states
    n,pointer,states,resets = engine.parse(delta(1024,68),69,True)
    checks["delta1024UnchangedCopy"] = n == 1024 and all(states[i*340:i*340+12] == before[i*340:i*340+12] and
        states[i*340+16:(i+1)*340] == before[i*340+16:(i+1)*340] and
        struct.unpack_from("<I",states,i*340+12)[0] == 69 for i in range(n)) and not any(resets)
    n,pointer,states,_ = engine.parse(delta(1024,69,[(800,False,88)]),70,True)
    checks["delta1024ModificationAbove256"] = n == 1024 and struct.unpack_from("<i",states,(800-33)*340+40)[0] == 88
    n,pointer,states,_ = engine.parse(delta(1024,70,[(33,True,0),(1057,False,99)]),71,True)
    checks["delta1024RemoveAdd"] = n == 1024 and struct.unpack_from("<i",states,4)[0] == 34 and struct.unpack_from("<ii",states,1023*340+4)[0] == 1057 and struct.unpack_from("<i",states,1023*340+40)[0] == 99
    old = pointer
    n,pointer,states,_ = engine.parse(full(257),135) # Same slot as sequence71.
    checks["historyWrapFreesOwnedAllocation"] = old in engine.freed and pointer != old and n == 257
    publications = engine.publications
    n,pointer,states,_ = engine.parse(delta(1024,1),136,True)
    checks["missingDeltaDoesNotPublish"] = n == 0 and pointer == 0 and engine.publications == publications
    try:engine.parse(full(1025),137)
    except EngineError:checks["full1025RejectsBeforeOverflow"] = "MAX_PACKET_ENTITIES" in engine.rejections[-1]
    else:checks["full1025RejectsBeforeOverflow"] = False
    checks["allocationCanaries"] = engine.canaries()
    result = dict(engineSha256=EXPECTED,scope="Actual x86 bitread/delta/full/history/publication; no live rendering/network/gameplay",checks=checks,frames=frames,passed=all(checks.values()))
    if args.server_fixture:
        directory = args.server_fixture if args.server_fixture.is_dir() else args.server_fixture.parent
        fixture = json.loads((directory/"cases.json").read_text(encoding="utf-8"))
        schema = json.loads((directory/"schema.json").read_text(encoding="utf-8"))
        definitions = {d["name"]:d["fields"] for d in schema["schemas"]}
        replay = Engine(path);replay.schema(definitions);outputs=[]
        replay.baselines((directory/"baselines.bin").read_bytes())
        instances = (directory/"instance-baselines.bin").read_bytes()
        if instances: replay.mu.mem_write(replay.base+0x1402248,instances)
        replay.set(0x1407748,schema["instanceBaselines"])
        replay.set(0x13fcc44,schema["maxClients"])
        replay.mu.mem_write(replay.base+0x1282ee8,struct.pack("<d",schema["time"]))
        def replay_frame(frame):
            wire = (directory/(frame["name"]+".bin")).read_bytes()
            count,pointer,states,_ = replay.parse(wire,frame["sequence"],wire[0] == 41,True)
            expected = (directory/(frame["name"]+".states")).read_bytes()
            failures = compare_fields(states,expected,definitions)
            outputs.append(dict(file=frame["name"],count=count,bytes=len(wire),sha256=hashlib.sha256(states).hexdigest(),fieldFailures=failures[:16]))
            assert count == frame["count"]
            assert replay.publication_matches(states), "original cl_entity curstate publication mismatch: "+frame["name"]
            outputs[-1]["publishedStatesMatch"] = True
            if failures:
                args.output.with_suffix(".failed.json").write_text(json.dumps(outputs,indent=2),encoding="utf-8")
                raise AssertionError(f"Server frame field mismatch: {frame['name']} / {failures[:8]}")
        for frame in fixture["cases"]: replay_frame(frame)
        event_file = directory/"wide-events.bin"
        if event_file.exists():
            replay.prepare(event_file.read_bytes(),fixture["cases"][-1]["sequence"],True)
            replay.call(0x1a9840)
            assert len(replay.events) == 2
            assert [struct.unpack_from("<i",bytes.fromhex(event["state"]),4)[0] for event in replay.events] == [
                schema["firstEntity"]+1023,schema["firstEntity"]+1024]
            result["serverEvents"] = replay.events
        production = directory/"production.json"
        if production.exists():
            replay_frame(json.loads(production.read_text(encoding="utf-8")))
            result["productionSnapshot1024"] = outputs[-1]["count"] == 1024
        result["serverReplay"] = outputs
        result["serverFieldComparisons"] = True
        assert replay.canaries()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(result))
    if not result["passed"]: raise AssertionError("actual packet parser regression")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine",type=Path,default=ROOT/"sandbox/cs-client-b/Half-Life/hw.dll")
    parser.add_argument("--output",type=Path,default=ROOT/"analysis/packet-entities/client-x86.json")
    parser.add_argument("--server-fixture",type=Path)
    run(parser.parse_args())
