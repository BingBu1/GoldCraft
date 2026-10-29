"""Float32 reference transcribed from preserved MP instruction windows.

No production C++ is imported and no original instruction is executed.
"""
import json,math,struct
def f(value):return struct.unpack('<f',struct.pack('<f',value))[0]
def bits(value):return struct.unpack('<I',struct.pack('<f',value))[0]
def inv(value):
    guess=struct.unpack('<f',struct.pack('<I',0x5f3759d5-(bits(value)>>1)))[0]
    half=f(value*f(.5));first=f(half*guess);second=f(first*guess)
    return f(f(f(1.5)-second)*guess)
inverse=inv(f(40*40))
neighbor=f(f(f(1/inverse)-f(78.74))*f(f(40)*inverse))
recovery=f(float(f(.995))+.01)
print(json.dumps({'neighbor40':{'value':neighbor,'bits':hex(bits(neighbor))},
    'recoveryFrom0995':{'value':recovery,'bits':hex(bits(recovery))},
    'recovered100':{'value':f(f(100)*recovery),'bits':hex(bits(f(f(100)*recovery)))},
    'boundary':'formula-level static reference; not an executed original-MP oracle'},indent=2))
