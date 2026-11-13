// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_decals.hpp"
#include "../../native/guest/cof/resource_catalog.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
namespace csol::client {metahook_api_t* api{};cl_enginefunc_t engine{};}
void RunDecalResourceOwnerTests();
namespace
{
using namespace csol::client;
std::uint64_t generation=10;
cvar_t enabled{};
pfnUserMsgHook installed{};
int calls{},registrations{};
bool cancelRandom{};
void Check(bool yes,const char* text){if(!yes){std::fprintf(stderr,"FAIL adapter %s\n",text);std::exit(1);}}
std::uint64_t Generation(){return generation;}
cvar_t* Lookup(const char* name){Check(std::strcmp(name,"gl_customdecals")==0,"cvar identity");return &enabled;}
pfnUserMsgHook Hook(const char* name,pfnUserMsgHook callback){Check(std::strcmp(name,guest::cof::resources::CustomDecalMessage)==0,"namespaced message");const auto previous=installed;installed=callback;return previous;}
int Register(const char* name,pfnUserMsgHook callback){Check(std::strcmp(name,guest::cof::resources::CustomDecalMessage)==0&&callback==installed,"engine message install");++registrations;return 1;}
int Random(int minimum,int maximum){Check(minimum==0&&maximum==4,"engine inclusive RNG");++calls;if(cancelRandom){cancelRandom=false;++generation;InvalidateGuestCofDecalResources();}return 3;}
void Deliver(float x=0)
{
    std::array<std::uint8_t,18> bytes{};const auto raw=std::bit_cast<std::uint16_t>(std::int16_t(x*8));bytes[0]=std::uint8_t(raw);bytes[1]=std::uint8_t(raw>>8);bytes[10]=8;std::memcpy(bytes.data()+13,"shot",5);Check(installed("CoFDecal",18,bytes.data())==1,"handler result");
}
}
void RunDecalAdapterTests()
{
    using namespace csol::client;using namespace guest::cof::client;
    static metahook_api_t host{};host.HookUserMsg=Hook;api=&host;
    engine.pfnGetCvarPointer=Lookup;engine.pfnHookUserMsg=Register;engine.pfnRandomLong=Random;
    const GoldCraftConnectionAPI connection{sizeof(GoldCraftConnectionAPI),1,Generation};enabled.value=1;
    InitializeGuestCofDecals(&connection);Check(registrations==1&&GuestCofDecalsState().status==GuestCofDecalStatus::Armed,"cold initialize");
    Deliver();Check(GuestCofDecalsState().pending==1&&calls==0,"message queues before world");
    std::vector<std::uint8_t> image(18+64*64*3,64);image[0]=0;image[2]=2;image[12]=64;image[13]=0;image[14]=64;image[15]=0;image[16]=24;image[17]=32;std::array<std::span<const std::uint8_t>,5> images;images.fill(image);
    Check(PrepareGuestCofDecalResources(images)&&GuestCofDecalsState().resourcesPrepared,"real cold texture owner");
    model_t model{};std::array<msurface_t,66> surfaces{};mplane_t plane{};plane.normal[2]=1;std::uint8_t sample{};std::vector<std::byte> polygon(offsetof(glpoly_t,verts)+4*7*sizeof(float));auto* poly=reinterpret_cast<glpoly_t*>(polygon.data());poly->numverts=4;const std::array<DecalVector,4> points{{{-8,-8,0},{8,-8,0},{8,8,0},{-8,8,0}}};for(unsigned i=0;i<4;++i)std::memcpy(poly->verts[i],points[i].data(),12);
    surfaces[65].plane=&plane;surfaces[65].samples=&sample;surfaces[65].polys=poly;surfaces[65].visframe=-99;model.type=mod_brush;model.numsurfaces=66;model.firstmodelsurface=65;model.nummodelsurfaces=1;model.surfaces=surfaces.data();
    Check(PrepareGuestCofDecalMap(model,false),"cold renderer world");Check(calls==1&&GuestCofDecalsState().pending==0&&GuestCofDecalsState().marks==1&&GuestCofDecalsState().status==GuestCofDecalStatus::Prepared,"queue flush maps resources");
    std::array<std::uint64_t,2> mask{0,2};const CustomDecalFrame frame{true,true,true,0,mask};
    reinterpret_cast<void(APIENTRY*)(GLenum)>(wglGetProcAddress("glActiveTexture"))(GL_TEXTURE0);glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(-8,8,-8,8,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();glDisable(GL_DEPTH_TEST);glClearColor(.4F,.4F,.4F,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_DITHER);
    DrawGuestCofDecals(model,frame);Check(GuestCofDecalsState().draw==CustomDecalDrawResult::Drawn,"draw uses supplied scene gate");std::array<std::uint8_t,3> pixel{};glReadPixels(32,32,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());Check(pixel[0]>=50&&pixel[0]<=52,"global surface index65 real PVS mask wins over stamp");
    mask[1]=0;glClear(GL_COLOR_BUFFER_BIT);DrawGuestCofDecals(model,frame);glReadPixels(32,32,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());Check(pixel[0]>=101&&pixel[0]<=103,"PVS masked surface skipped");
    enabled.value=0;Deliver(5);Check(calls==1&&GuestCofDecalsState().marks==1,"disabled no RNG");enabled.value=1;
    ClearGuestCofDecalMap();Check(!GuestCofDecalsState().mapPrepared&&GuestCofDecalsState().resourcesPrepared&&GuestCofDecalsState().marks==0,"first-level scene clear preserves signon textures");Check(PrepareGuestCofDecalMap(model,false),"cold prepare after first-level clear");Deliver();Check(GuestCofDecalsState().marks==1,"message after first-level clear");
    bool foreignResult=true;std::thread other([&]{foreignResult=PrepareGuestCofDecalMap(model,false);ShutdownGuestCofDecals();});other.join();Check(!foreignResult&&GuestCofDecalsState().marks==1,"wrong thread refuses");
    cancelRandom=true;Deliver(5);Check(GuestCofDecalsState().marks==0&&!GuestCofDecalsState().resourcesPrepared,"nested generation retirement");
    Check(PrepareGuestCofDecalResources(images)&&PrepareGuestCofDecalMap(model,false),"cold reprepare next connection");Deliver();Check(GuestCofDecalsState().marks==1,"next connection message");
    Check(GuestCofDecalsVideoInit()&&!GuestCofDecalsState().mapPrepared&&!GuestCofDecalsState().resourcesPrepared&&GuestCofDecalsState().marks==0,"video owner releases GL and world");
    ShutdownGuestCofDecals();Check(GuestCofDecalsState().status==GuestCofDecalStatus::Inactive,"shutdown");
    RunDecalResourceOwnerTests();
    std::puts("adapter: real namespaced engine hook, cold GL/world, global PVS, generation/reentrant retirement, video, thread, shutdown passed");
}
