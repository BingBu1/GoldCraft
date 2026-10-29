"""Independent float32 reference constants, not original-instruction execution.

Research inputs: ordinary-behavior-tree/inverse-sqrt.asm and
ordinary-follow-events/move-vector-normalize.asm. No production C++ is read.
The preserved SSE order is xx+yy+zz, half*estimate*estimate, one Newton step,
component*inverse, then component*(acceleration*dt). Keep explicit rounding.
"""
import json
import struct

def f32(v):return struct.unpack('<f',struct.pack('<f',v))[0]
def bits(v):return struct.unpack('<I',struct.pack('<f',v))[0]
def frombits(v):return struct.unpack('<f',struct.pack('<I',v))[0]

def east_component(acceleration):
    x=f32(100);squared=f32(f32(x*x)+f32(0))
    estimate=frombits(0x5f3759d5-(bits(squared)>>1))
    first=f32(f32(squared*f32(.5))*estimate)
    inverse=f32(f32(f32(1.5)-f32(first*estimate))*estimate)
    return f32(f32(x*inverse)*f32(f32(acceleration)*f32(.1)))

if __name__=='__main__':
    expected={250:0x41c7ac19,500:0x4247ac19,1000:0x42c7ac19,1200:0x42ef9b51}
    result=[]
    for acceleration,want in expected.items():
        value=east_component(acceleration)
        assert bits(value)==want,(acceleration,value,hex(bits(value)))
        result.append({'acceleration':acceleration,'component':value,'bits':f'{want:08x}'})
    # Strict upper melee height is origin+36 <149. One float below113 rounds
    # back to149; two floats below113 gives the first accepted target sum.
    below1=frombits(bits(113.)-1);below2=frombits(bits(113.)-2)
    assert f32(below1+36)==149 and f32(below2+36)<149
    print(json.dumps({'scope':'independent formula reference only','east':result,
        'upperHeight':{'oneUlp':below1,'oneUlpSum':f32(below1+36),
            'twoUlp':below2,'twoUlpSum':f32(below2+36)}},indent=2))
