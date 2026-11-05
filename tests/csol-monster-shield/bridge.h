// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
struct ShieldTraceResult {
    int count,group,hit,startsolid,allsolid;
    float fraction,end[3],normal[3];
};
using ShieldTraceFunction=int(__cdecl*)(const void*,int,int,int,const float*,const float*,ShieldTraceResult*);
