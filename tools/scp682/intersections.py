"""Geometric queries used to bake and independently check the final one-weight mesh."""
import ctypes
import numpy as np
from views import WORK


def crossings(rest,xyz,faces,min_gap):
    if not hasattr(crossings,'api'):
        dll=ctypes.CDLL(str(WORK/'raster/Release/raster.dll'))
        fun=dll.surface_crossings
        ptr=ctypes.c_void_p
        fun.argtypes=[ptr,ptr,ptr,ctypes.c_int,ctypes.c_double,ptr,ctypes.c_int]
        fun.restype=ctypes.c_int
        crossings.api=fun
    args=[np.ascontiguousarray(rest,np.float64),np.ascontiguousarray(xyz,np.float64),np.ascontiguousarray(faces,np.int32)]
    pairs=np.empty((4096,2),np.int32)
    def collect():
        return crossings.api(*[a.ctypes.data for a in args],len(faces),min_gap,pairs.ctypes.data,len(pairs))
    n=collect()
    if n>len(pairs):
        pairs=np.empty((n,2),np.int32)
        n=collect()
    return pairs[:n]
