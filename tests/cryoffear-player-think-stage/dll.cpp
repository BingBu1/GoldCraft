// SPDX-License-Identifier: GPL-3.0-or-later
#include <Windows.h>
#define EXT_FUNC
#include <interface.h>
#include "goldcraft/player_think_stage_api.hpp"
#include <cstdio>

int main(int argc,char** argv)
{
    if(argc!=2)return 2;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!module){std::printf("LoadLibrary error=%lu\n",GetLastError());return 1;}
    auto factory=reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface"));
    auto* stage=factory?static_cast<goldcraft::IPlayerThinkStage*>(factory(goldcraft::PlayerThinkStageVersion,nullptr)):nullptr;
    unsigned checks=1,failures=stage?0:1;
    if(stage)
    {
        auto check=[&](bool value){++checks;if(!value)++failures;};
        check(factory(goldcraft::PlayerThinkStageVersion,nullptr)==stage);
        check(factory("GoldCraftPlayerThinkStage002",nullptr)==nullptr);
        check(!stage->Current(0)&&!stage->Current(1));
        check(!stage->Release(0)&&!stage->Release(1));
        check(!stage->Register(nullptr,nullptr));
        check(!stage->Register(nullptr,[](void*,edict_s*,float,double) noexcept{}));
        check(!stage->Current(1));
    }
    std::printf("actualEngineStageChecks=%u failures=%u; no server initialized\n",checks,failures);
    FreeLibrary(module);return failures?1:0;
}
