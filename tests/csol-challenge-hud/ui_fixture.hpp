// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tests/csol-hud-style/surface_stub.hpp"
#include <cstdlib>
#include <iostream>
#include <set>
namespace fixture
{
inline unsigned checks{};
inline void Check(bool value,const char* why)
{++checks;if(!value){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Surface:vgui::HudTestSurface
{
    int width=1280,height=960;
    std::set<int> deleted;
    void GetScreenSize(int& w,int& h)override{w=width;h=height;}
    bool DeleteTextureByID(int texture)override{deleted.insert(texture);return true;}
};
inline int Passed(){std::cout<<"PASS "<<checks<<" actual-owner checks\n";return 0;}
}
namespace csol::client
{
metahook_api_t* api{};mh_interface_t* interfaces{};cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};engine_studio_api_t studio{};
r_studio_interface_t* studioInterface{};ref_params_t view{};bool glewReady{};
void SynchronizeSession(){}
bool ResourceFontIsTracked(void**){return false;}
}
CreateInterfaceFn Sys_GetFactory(HINTERFACEMODULE){return nullptr;}
