// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_control.hpp"
#include "../../native/csol/client/weapons/guest_cof_particles.hpp"
#include "../../native/guest/cof/client/forward_displacement.hpp"
#include "../../native/guest/cof/control_protocol.hpp"
#include "../../native/guest/cof/particle_protocol.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <fstream>
#include <iterator>
#include <triangleapi.h>
namespace csol::client { metahook_api_t* api{}; cl_enginefunc_t engine{}; }
namespace
{
using namespace csol::client;
using namespace guest::cof;
unsigned checks{}, failures{};
std::uint64_t generation{1};
pfnUserMsgHook handlers[4]{};
cl_entity_t vm{};
bool hasView{true}, cancelView{}, retireView{}, cancelAngles{};
std::array<float,3> cameraAngles{};unsigned angleSets{};
cvar_t cvar{};
model_t sprite{};
triangleapi_t triangles{};
bool forwardReady{true};unsigned forwardResets{};
guest::cof::client::ForwardDisplacementState forwardState{4.0F};
void Check(bool value, int line) { ++checks; if(!value) { ++failures; std::printf("FAIL %d\n",line); } }
#define CHECK(x) Check((x),__LINE__)
unsigned Slot(const char* name)
{
    if(!std::strcmp(name,ControlMessage))return 0;
    if(!std::strcmp(name,ParticleMessage))return 1;
    if(!std::strcmp(name,ViewEntityMessage))return 2;
    CHECK(!std::strcmp(name,"ResetHUD"));return 3;
}
pfnUserMsgHook Hook(const char* name,pfnUserMsgHook next)
{ auto& head=handlers[Slot(name)];auto old=head;if(old)head=next;return old; }
int Register(const char* name,pfnUserMsgHook next) { handlers[Slot(name)]=next;return 1; }
std::uint64_t Generation() { return generation; }
cl_entity_t* View()
{
    if(cancelView)++generation;
    if(retireView)ShutdownGuestCofControl();
    return hasView?&vm:nullptr;
}
cvar_t* Cvar(const char*) { return &cvar; }
float Clock() { return 0; }
cl_entity_t* Entity(int) { return &vm; }
const model_t* Sprite(int id) { return id==7?&sprite:nullptr; }
float Random(float a,float b) { return (a+b)*.5f; }
void Angles(float* v) { if(cancelAngles)++generation;std::memcpy(v,cameraAngles.data(),sizeof(float)*3); }
void SetAngles(float* v) { ++angleSets;std::memcpy(cameraAngles.data(),v,sizeof(float)*3); }
void Vectors(const float*,float*,float*,float*) {}
void Deliver(ControlOperation op,std::uint8_t body=0)
{
    ControlPacket packet{};CHECK(EncodeControl({op,body},packet));
    CHECK(handlers[0](ControlMessage,int(packet.size()),packet.data())==1);
}
}
namespace csol::client
{
bool ResetGuestCofForwardDisplacement() noexcept
{
    ++forwardResets;
    if(!forwardReady)return false;
    guest::cof::client::ResetForwardDisplacement(forwardState);return true;
}
}
int main(int argc,char** argv)
{
    CHECK(argc==2);
    metahook_api_t host{};host.HookUserMsg=Hook;api=&host;
    engine.pfnHookUserMsg=Register;engine.GetViewModel=View;engine.pfnGetCvarPointer=Cvar;
    GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};
    InitializeGuestCofParticles(&provider);InitializeGuestCofControl(&provider);
    for(unsigned b=0;b<256;++b)
    {
        Deliver(ControlOperation::WeaponBody,std::uint8_t(b));
        CHECK(vm.curstate.body==int(b));CHECK(GuestCofLastControl()==GuestCofControlResult::BodyApplied);
    }
    hasView=false;Deliver(ControlOperation::WeaponBody,19);
    CHECK(GuestCofLastControl()==GuestCofControlResult::NoViewModel);
    hasView=true;CHECK(vm.curstate.body==255); // no retained/replayed assignment
    cancelView=true;Deliver(ControlOperation::WeaponBody,23);cancelView=false;
    CHECK(vm.curstate.body==255 && GuestCofLastControl()==GuestCofControlResult::Cancelled);
    retireView=true;Deliver(ControlOperation::WeaponBody,24);retireView=false;
    CHECK(vm.curstate.body==255 && GuestCofLastControl()==GuestCofControlResult::Cancelled);
    InitializeGuestCofControl(&provider);
    Deliver(ControlOperation::WeaponDeploy);
    CHECK(GuestCofLastControl()==GuestCofControlResult::DeployApplied);
    CHECK(forwardResets==1&&forwardState.value==0.0F);
    forwardReady=false;Deliver(ControlOperation::WeaponDeploy);
    CHECK(GuestCofLastControl()==GuestCofControlResult::MissingForwardDisplacementBackend);forwardReady=true;
    CHECK(!GuestCofParticlesReady()); // KillLocal did not invent preparation
    std::ifstream file(argv[1],std::ios::binary);
    const std::string script((std::istreambuf_iterator<char>(file)),{});
    engine.GetSpritePointer=Sprite;engine.pfnRandomFloat=Random;
    engine.GetViewAngles=Angles;engine.pfnAngleVectors=Vectors;sprite.numframes=1;
    engine.GetClientTime=Clock;engine.GetEntityByIndex=Entity;engine.GetLocalPlayer=View;engine.pTriAPI=&triangles;
    // Reinitialize snapshots the actual backend; real original script preparation
    // must survive Deploy. A full reset or resource retirement makes this fail.
    InitializeGuestCofParticles(&provider);
    CHECK(PrepareGuestCofParticles({script,7})==guest::cof::client::AuroraResult::Applied);
    CHECK(GuestCofParticlesReady());
    Deliver(ControlOperation::WeaponDeploy);CHECK(GuestCofParticlesReady());
    generation=0;Deliver(ControlOperation::WeaponBody,25);CHECK(vm.curstate.body==255);
    generation=8;Deliver(ControlOperation::WeaponBody,26);CHECK(vm.curstate.body==26);
    ControlPacket packet{};CHECK(EncodeControl({ControlOperation::WeaponBody,9},packet));
    for(unsigned n=0;n<6;++n)if(n!=4)
    { std::byte bytes[6]{};std::memcpy(bytes,packet.data(),4);handlers[0](ControlMessage,n,bytes);CHECK(vm.curstate.body==26); }
    for(unsigned i=0;i<4;++i)
    { auto bad=packet;bad[i]=std::byte{255};if(i==2)continue;handlers[0](ControlMessage,4,bad.data());CHECK(vm.curstate.body==26); }
    CHECK(!EncodeControl({ControlOperation::WeaponDeploy,1},packet));
    CHECK(!EncodeControl({ControlOperation(255),0},packet));
    auto wrong=std::thread([]{Deliver(ControlOperation::WeaponBody,27);});wrong.join();CHECK(vm.curstate.body==26);
    engine.GetViewAngles=Angles;engine.SetViewAngles=SetAngles;
    for(unsigned b=0;b<256;++b)
    {
        cameraAngles={73.25f,-19.5f,4.0f};
        Deliver(ControlOperation::ShotState,std::uint8_t(b));
        CHECK(cameraAngles[0]==73.25f-float(b) && cameraAngles[1]==-19.5f && cameraAngles[2]==4.0f);
        CHECK(GuestCofLastControl()==GuestCofControlResult::ShotStateApplied);
    }
    const auto sets=angleSets;
    SetGuestCofControlCamera(GuestCofControlCamera::Unsupported);
    Deliver(ControlOperation::ShotState,3);CHECK(angleSets==sets);
    CHECK(GuestCofLastControl()==GuestCofControlResult::UnsupportedCamera);
    SetGuestCofControlCamera(GuestCofControlCamera::OrdinaryHost);
    cancelAngles=true;Deliver(ControlOperation::ShotState,3);cancelAngles=false;
    CHECK(angleSets==sets && GuestCofLastControl()==GuestCofControlResult::Cancelled);
    engine.SetViewAngles=nullptr;Deliver(ControlOperation::ShotState,3);
    CHECK(GuestCofLastControl()==GuestCofControlResult::MissingCameraBackend);
    ShutdownGuestCofParticles();Deliver(ControlOperation::WeaponDeploy);
    CHECK(GuestCofLastControl()==GuestCofControlResult::MissingParticleBackend);
    ShutdownGuestCofControl();Deliver(ControlOperation::WeaponBody,28);CHECK(vm.curstate.body==26);
    std::printf("checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
