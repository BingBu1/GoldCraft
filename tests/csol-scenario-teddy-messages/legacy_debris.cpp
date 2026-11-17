// SPDX-License-Identifier: GPL-3.0-or-later
// Link the exact extracted previous production method as a test-only adapter.
#include "precompiled.h"
#include <iostream>

enginefuncs_t g_engfuncs{};
bool g_bAllowedCSBot{};
bool LegacyDebris(const float*, int, int);
int main()
{
    int began{};
    static int* beginCount;
    beginCount=&began;
    g_engfuncs.pfnMessageBegin=[](int,int,const float*,edict_t*){++*beginCount;};
    g_engfuncs.pfnMessageEnd=[]{};
    g_engfuncs.pfnWriteByte=[](int){};
    g_engfuncs.pfnWriteShort=[](int){};
    g_engfuncs.pfnWriteCoord=[](float){};
    const float origin[]{4096.f,20.f,30.f};
    if(!LegacyDebris(origin,123,4)&&began==0)
    {
        std::cerr<<"legacy extra signed-short guard rejects the original low16 position\n";
        return 1;
    }
    std::cerr<<"legacy behavior did not reproduce\n";
    return 2;
}
