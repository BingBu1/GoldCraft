// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_muzzle.hpp"
#include "guest_cof_effects.hpp"
#include "guest_cof_scene.hpp"
#include "../../../guest/cof/client/muzzle_flash.hpp"
#include "../../../guest/cof/resource_catalog.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <event_args.h>
#include <event_api.h>
#include <cmath>
#include <cstring>
#include <limits>
namespace csol::client
{
namespace
{
using namespace guest::cof::client;
using namespace guest::cof::resources;
const GoldCraftConnectionAPI* connection{};
std::uint64_t generation{},preparedGeneration{},serial{1},effectResources{};
DWORD thread{};
cl_entity_t localFlash{},worldFlash{};
MuzzleLatch latch;
model_t *viewModel{},*playerModel{};
studiohdr_t* playerHeader{};
struct Attachment {int bone{};std::array<float,3> origin{};};
std::array<Attachment,2> attachments{};
cvar_t* cheapSmoke{};
int playerModelIndex{};
bool eventInstalled{},prepared{};
void Clear() noexcept {
    if(serial)serial=serial==std::numeric_limits<std::uint64_t>::max()?0:serial+1;
    prepared=false;preparedGeneration=0;latch={};viewModel=playerModel=nullptr;
    playerHeader=nullptr;playerModelIndex=0;attachments={};effectResources=0;
    // Visible entity storage remains stable until the host retires the frame.
    // Clearing readiness never overwrites an entity already borrowed by it.
}
bool Owner() noexcept {return thread&&thread==GetCurrentThreadId();}
bool Current() noexcept {
    if(!Owner()||!connection)return false;
    const auto now=connection->generation();
    if(now!=generation){Clear();generation=now;}
    return now!=0&&serial!=0;
}
struct Lifetime {std::uint64_t serial,generation,resources;};
bool Alive(void* p) noexcept {
    const auto call=*static_cast<Lifetime*>(p);
    return call.serial==serial&&prepared&&Current()&&call.serial==serial&&call.generation==generation&&
        preparedGeneration==call.generation&&call.resources==GuestCofEffectsResourceSerial();
}
MuzzleHost Host(Lifetime& lifetime) noexcept {
    return {GuestCofMuzzleEngine(),&localFlash,&worldFlash,&lifetime,
        [](void*) noexcept {return GuestCofPointLightsReady();},
        [](void* p,int key) noexcept {return Alive(p)?AllocateGuestCofPointLight(key):nullptr;},Alive};
}
bool Ready() noexcept {
    return prepared&&eventInstalled&&Current()&&preparedGeneration==generation&&
        effectResources&&effectResources==GuestCofEffectsResourceSerial()&&GuestCofPointLightsReady();
}
void Event(event_args_s* event) {
    if(!event||!Ready()||event->iparam1!=3||event->entindex<=0||event->entindex>32||
       event->iparam2<=0||event->iparam2>32767||!engine.GetEntityByIndex)return;
    const auto revision=serial;
    const auto* owner=engine.GetEntityByIndex(event->entindex);
    if(!Current()||serial!=revision)return;
    if(!owner||!owner->player||owner->curstate.weaponmodel!=playerModelIndex)return;
    // Original weaponmodel-as-secondary-entity lookup cannot be copied to CS:
    // AttachToPlayer sets the weapon modelindex0, excluding it from networking.
    // The server authenticates iparam2; the current P model authenticates the
    // visible owner. Preserve the original single overwrite latch semantics.
    latch.pending=true;latch.type=3;latch.owner=event->entindex;
    std::copy_n(event->angles,3,latch.eventVector.begin());
}
model_t* Model(const char* path,std::uint64_t revision,int* handle=nullptr) noexcept {
    const int index=engine.pEventAPI->EV_FindModelIndex(path);
    if(!Current()||serial!=revision)return nullptr;
    auto* result=index>0?studio.GetModelByIndex(index):nullptr;
    if(!Current()||serial!=revision)return nullptr;
    if(!result||result->type!=mod_studio||std::strncmp(result->name,path,sizeof(result->name)))return nullptr;
    if(handle)*handle=index;
    return result;
}
bool Finite(const std::array<float,3>& value) noexcept {
    return std::isfinite(value[0])&&std::isfinite(value[1])&&std::isfinite(value[2]);
}
}
void InitializeGuestCofMuzzle() {InitializeGuestCofMuzzle(GoldCraftConnectionInterface());}
void InitializeGuestCofMuzzle(const GoldCraftConnectionAPI* provider) {
    if(thread&&!Owner())return;
    Clear();thread=GetCurrentThreadId();connection=nullptr;eventInstalled=false;generation=0;
    if(!provider||provider->size<sizeof(*provider)||provider->version!=1||!provider->generation||
       !engine.pfnHookEvent||!engine.pfnGetCvarPointer||!engine.pfnRegisterVariable)return;
    connection=provider;generation=provider->generation();
    cheapSmoke=engine.pfnGetCvarPointer("cof_cheapweaponsmoke");
    if(!cheapSmoke)cheapSmoke=engine.pfnRegisterVariable("cof_cheapweaponsmoke","0",FCVAR_ARCHIVE);
    engine.pfnHookEvent(GlockEvents[0].host,Event);eventInstalled=true;
}
bool PrepareGuestCofMuzzle() noexcept {
    if(!Current())return false;
    Clear();const auto active=generation,revision=serial,resources=GuestCofEffectsResourceSerial();
    if(!eventInstalled||!resources||!cheapSmoke||!studio.Mod_Extradata||
       !studio.GetModelByIndex||!engine.pEventAPI||!engine.pEventAPI->EV_FindModelIndex)return false;
    auto* view=Model(GlockModels[unsigned(GlockModel::View)].host,revision);
    if(!Current()||serial!=revision)return false;
    int index{};auto* player=Model(GlockModels[unsigned(GlockModel::Player)].host,revision,&index);
    if(!Current()||serial!=revision)return false;
    auto* header=player?static_cast<studiohdr_t*>(studio.Mod_Extradata(player)):nullptr;
    if(!Current()||generation!=active||serial!=revision||resources!=GuestCofEffectsResourceSerial()||!view||!header||header->id!=0x54534449||header->version!=10||
       header->numattachments<2||header->attachmentindex<int(sizeof(*header))||header->numbones<=0||
       header->numbones>MAXSTUDIOBONES||header->attachmentindex>header->length||
       sizeof(mstudioattachment_t)*2>std::size_t(header->length-header->attachmentindex))return false;
    const auto* source=reinterpret_cast<const mstudioattachment_t*>(reinterpret_cast<const char*>(header)+header->attachmentindex);
    std::array<Attachment,2> cached{};
    for(unsigned i=0;i<cached.size();++i) {
        cached[i].bone=source[i].bone;std::copy_n(source[i].org,3,cached[i].origin.begin());
        if(cached[i].bone<0||cached[i].bone>=header->numbones||!Finite(cached[i].origin))return false;
    }
    attachments=cached;viewModel=view;playerModel=player;playerHeader=header;playerModelIndex=index;
    preparedGeneration=active;effectResources=resources;prepared=true;return true;
}
void InvalidateGuestCofMuzzle() noexcept {if(Owner())Clear();}
void ShutdownGuestCofMuzzle() noexcept {
    if(!Owner())return;Clear();connection=nullptr;generation=0;eventInstalled=false;thread=0;
}
bool GuestCofMuzzleReady() noexcept {return Ready();}
bool GuestCofMuzzleWorldPending() noexcept {return latch.pending&&Ready();}
bool GuestCofMuzzleStudioEvent(const mstudioevent_s* event,const cl_entity_s* entity) noexcept {
    if(!event||event->event!=5001||!entity||!entity->model)return false;
    if(std::strncmp(entity->model->name,GlockModels[0].host,sizeof(entity->model->name)))return false;
    if(!engine.GetViewModel||entity!=engine.GetViewModel())return false;
    if(!Ready()||!GuestCofEffectsOwnsWeapon(Weapon::Glock)||!GuestCofMainStudioView()||entity->model!=viewModel)return true;
    Lifetime lifetime{serial,generation,effectResources};const auto host=Host(lifetime);
    GlockMuzzle(host).Local(*entity,0,1.0F,cheapSmoke->value);
    return true;
}
void GuestCofMuzzleWorldPoints(const cl_entity_s* entity,const model_s* model,const void* header,
                              const float (*bones)[3][4]) noexcept {
    // Native CS retains the player render model while merging the active P
    // header/bones (1004D177..1004D19C). Both current identities are required.
    if(!Ready()||!GuestCofMainStudioView()||!entity||!entity->player||!model||
       (model!=playerModel&&model!=entity->model)||header!=playerHeader||!bones||
       entity->curstate.weaponmodel!=playerModelIndex||!P345Muzzle::Take(latch,entity->index))return;
    std::array<std::array<float,3>,2> world;
    for(unsigned i=0;i<world.size();++i)for(unsigned axis=0;axis<3;++axis) {
        const auto& a=attachments[i];const auto& m=bones[a.bone][axis];
        world[i][axis]=((a.origin[0]*m[0]+a.origin[1]*m[1])+a.origin[2]*m[2])+m[3];
    }
    Lifetime lifetime{serial,generation,effectResources};
    GlockMuzzle(Host(lifetime)).World(world[0],world[1],latch.eventVector,1.0F);
}
}
