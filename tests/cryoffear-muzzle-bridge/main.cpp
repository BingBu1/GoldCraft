// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_muzzle.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/csol/client/weapons/guest_cof_scene.hpp"
#include "../../native/guest/cof/client/muzzle_flash.hpp"
#include "../../native/guest/cof/resource_catalog.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include <event_args.h>
#include <event_api.h>
#include <cstdio>
#include <cstring>
#include <thread>
namespace csol::client {cl_enginefunc_t engine{};engine_studio_api_t studio{};}
namespace
{
using namespace csol::client;
using namespace guest::cof::client;
using namespace guest::cof::resources;
unsigned checks{},failures{},modelQueries{},visible{},shells{},engineLights{},customLights{},randoms{};
std::uint64_t generation{1};
std::uint64_t effectResources{1};
bool effectsReady{true},pointReady{true},mainView{true},retireVisible{},retireEffects{},retireModel{},localOwned{true};
cl_entity_t local{},viewEntity{},remote{},lastVisible{};
model_t viewModel{},playerModel{},flashModel{},otherModel{},bodyModel{};
cl_enginefunc_t muzzleEngine{};
event_api_t cachedEvents{};
std::vector<std::byte> playerBytes;
PointLightState pool;
dlight_t dlight{};
std::array<float,3> shellOrigin{},shellVelocity{};
float bones[MAXSTUDIOBONES][3][4]{};
void (*eventCallback)(event_args_s*);
cvar_t cheap{};
void Check(bool ok,int line){++checks;if(!ok){++failures;std::printf("FAIL %d\n",line);}}
#define CHECK(...) Check((__VA_ARGS__),__LINE__)
std::uint64_t Generation(){return generation;}
void HookEvent(const char* path,void (*callback)(event_args_s*)) {CHECK(!std::strcmp(path,GlockEvents[0].host));eventCallback=callback;}
cvar_t* Cvar(const char* name){CHECK(!std::strcmp(name,"cof_cheapweaponsmoke"));return &cheap;}
cvar_t* Register(const char*,const char*,int){CHECK(false);return nullptr;}
cl_entity_t* Local(){return &local;}
cl_entity_t* View(){return &viewEntity;}
cl_entity_t* Entity(int index){return index==1?&local:index==2?&remote:nullptr;}
float Time(){return 5;}
float Random(float,float){++randoms;return 0;}
int Find(const char* path){
 ++modelQueries;if(retireModel){retireModel=false;InvalidateGuestCofMuzzle();}
 if(!std::strcmp(path,GlockModels[0].host))return 11;
 CHECK(!std::strcmp(path,GlockModels[unsigned(GlockModel::Player)].host));return 12;
}
model_t* Model(int id){return id==11?&viewModel:id==12?&playerModel:nullptr;}
void* Extra(model_t* model){CHECK(model==&playerModel);return playerBytes.data();}
model_t* Flash(const char* path,int* id){CHECK(!std::strcmp(path,"models/flash1.mdl"));*id=13;return &flashModel;}
int MuzzleIndex(const char* path){CHECK(!std::strcmp(path,"models/shell.mdl"));return 14;}
int Visible(int type,cl_entity_t* entity){CHECK(type==0);++visible;lastVisible=*entity;if(retireVisible)InvalidateGuestCofMuzzle();if(retireEffects)effectsReady=false;return 1;}
dlight_t* Dlight(int key){CHECK(key==0);++engineLights;return &dlight;}
void Angles(const float*,float*,float*,float*){}
TEMPENTITY* Shell(float* origin,float* velocity,float* angles,float life,int model,int sound){
 CHECK(life==2.5F&&model==14&&sound==1&&angles[0]==0&&angles[1]==0&&angles[2]==0);
 ++shells;std::copy_n(origin,3,shellOrigin.begin());std::copy_n(velocity,3,shellVelocity.begin());return nullptr;
}
std::array<float,3> Transform(const mstudioattachment_t& a){
 std::array<float,3> out;for(unsigned i=0;i<3;++i){const auto& m=bones[a.bone][i];out[i]=((a.org[0]*m[0]+a.org[1]*m[1])+a.org[2]*m[2])+m[3];}return out;
}
}
namespace csol::client
{
bool GuestCofEffectsReady() noexcept{return effectsReady;}
std::uint64_t GuestCofEffectsResourceSerial() noexcept{return effectsReady?effectResources:0;}
bool GuestCofEffectsOwnsWeapon(Weapon value) noexcept{return effectsReady&&localOwned&&value==Weapon::Glock;}
cl_enginefuncs_s* GuestCofMuzzleEngine() noexcept{return effectsReady?&muzzleEngine:nullptr;}
bool GuestCofPointLightsReady() noexcept{return pointReady;}
bool GuestCofMainStudioView() noexcept{return mainView;}
PointLight* AllocateGuestCofPointLight(int key) noexcept{++customLights;return &AllocatePointLight(pool,key,Time());}
}
int main(int argc,char** argv)
{
 if(argc!=2)return 2;FILE* file=std::fopen(argv[1],"rb");if(!file)return 3;
 std::fseek(file,0,SEEK_END);const long size=std::ftell(file);std::rewind(file);if(size<=0)return 4;
 playerBytes.resize(std::size_t(size));if(std::fread(playerBytes.data(),playerBytes.size(),1,file)!=1)return 5;std::fclose(file);
 auto* header=reinterpret_cast<studiohdr_t*>(playerBytes.data());CHECK(header->length==size&&header->numattachments>=2);
 auto* attachment=reinterpret_cast<mstudioattachment_t*>(playerBytes.data()+header->attachmentindex);
 for(auto& bone:bones)for(unsigned i=0;i<3;++i){bone[i][i]=1;bone[i][3]=float(i)*10;}
 engine.pfnHookEvent=HookEvent;engine.pfnGetCvarPointer=Cvar;engine.pfnRegisterVariable=Register;
 engine.GetLocalPlayer=Local;engine.GetViewModel=View;engine.GetEntityByIndex=Entity;engine.GetClientTime=Time;
 engine.CL_LoadModel=Flash;engine.CL_CreateVisibleEntity=Visible;engine.pfnRandomFloat=Random;engine.pfnAngleVectors=Angles;
 event_api_t events{};events.EV_FindModelIndex=Find;engine.pEventAPI=&events;
 efx_api_t fx{};fx.CL_AllocDlight=Dlight;fx.R_TempModel=Shell;engine.pEfxAPI=&fx;
 muzzleEngine=engine;cachedEvents=events;cachedEvents.EV_FindModelIndex=MuzzleIndex;muzzleEngine.pEventAPI=&cachedEvents;
 studio.GetModelByIndex=Model;studio.Mod_Extradata=Extra;
 viewModel.type=playerModel.type=flashModel.type=otherModel.type=mod_studio;
 std::strcpy(viewModel.name,GlockModels[0].host);std::strcpy(playerModel.name,GlockModels[2].host);
 std::strcpy(otherModel.name,"models/v_usp.mdl");viewEntity.model=&viewModel;
 local.index=1;remote.index=2;local.player=remote.player=1;local.curstate.weaponmodel=remote.curstate.weaponmodel=12;
 local.curstate.frame=17;local.curstate.sequence=23;viewEntity.attachment[0][0]=1;viewEntity.attachment[0][1]=2;viewEntity.attachment[0][2]=3;
 GoldCraftConnectionAPI provider{sizeof(provider),1,Generation};
 InitializeGuestCofMuzzle(nullptr);CHECK(!PrepareGuestCofMuzzle()&&!GuestCofMuzzleReady());
 InitializeGuestCofMuzzle(&provider);CHECK(eventCallback&&PrepareGuestCofMuzzle()&&GuestCofMuzzleReady());
 const auto queries=modelQueries;
 mstudioevent_t studioEvent{};studioEvent.event=5001;CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity));
 CHECK(visible==1&&engineLights==1&&customLights==1&&shells==0&&modelQueries==queries);
 CHECK(lastVisible.curstate.sequence==0&&lastVisible.curstate.frame==17&&lastVisible.curstate.renderfx==161);
 CHECK(lastVisible.origin[0]==1&&lastVisible.origin[1]==2&&lastVisible.origin[2]==3);
 localOwned=false;CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity));CHECK(visible==1);localOwned=true;
 mainView=false;CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity));CHECK(visible==1);mainView=true;
 viewEntity.model=&otherModel;CHECK(!GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity));viewEntity.model=&viewModel;
 studioEvent.event=5004;CHECK(!GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity));studioEvent.event=5001;
 event_args_t event{};event.entindex=2;event.iparam1=3;event.iparam2=777;event.angles[0]=20;event.angles[1]=-40;event.angles[2]=60;
 eventCallback(&event);mainView=false;GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==1);mainView=true;
 GuestCofMuzzleWorldPoints(&local,&playerModel,header,bones);CHECK(visible==1);
 GuestCofMuzzleWorldPoints(&remote,&otherModel,header,bones);CHECK(visible==1);
 GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==2&&shells==1);
 CHECK(shellVelocity==std::array<float,3>{1,-2,3}&&shellOrigin==Transform(attachment[1]));
 const auto flashOrigin=Transform(attachment[0]);CHECK(std::equal(flashOrigin.begin(),flashOrigin.end(),lastVisible.origin));
 CHECK(lastVisible.curstate.frame==17&&lastVisible.curstate.sequence==23&&modelQueries==queries);
 GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==2&&shells==1);
 for(int wrong:{0,7}){event.iparam1=wrong;eventCallback(&event);GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==2);}
 event.iparam1=3;event.iparam2=0;eventCallback(&event);GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==2);event.iparam2=777;
 remote.curstate.weaponmodel=99;eventCallback(&event);GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==2);remote.curstate.weaponmodel=12;
 const int originalBone=attachment[1].bone;attachment[1].bone=header->numbones;CHECK(!PrepareGuestCofMuzzle()&&!GuestCofMuzzleReady());attachment[1].bone=originalBone;
 CHECK(PrepareGuestCofMuzzle());retireModel=true;CHECK(!PrepareGuestCofMuzzle()&&!GuestCofMuzzleReady());CHECK(PrepareGuestCofMuzzle());
 pointReady=false;CHECK(!GuestCofMuzzleReady());pointReady=true;
 bool foreignReady=true;std::thread wrongThread([&]{foreignReady=GuestCofMuzzleReady();InvalidateGuestCofMuzzle();});wrongThread.join();CHECK(!foreignReady&&GuestCofMuzzleReady());
 const auto beforeEngine=engineLights,beforeCustom=customLights;retireVisible=true;
 eventCallback(&event);GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);
 CHECK(visible==3&&shells==1&&engineLights==beforeEngine&&customLights==beforeCustom&&!GuestCofMuzzleReady());retireVisible=false;
 CHECK(PrepareGuestCofMuzzle());retireEffects=true;eventCallback(&event);GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);
 CHECK(visible==4&&shells==1&&engineLights==beforeEngine&&customLights==beforeCustom&&!GuestCofMuzzleReady());retireEffects=false;effectsReady=true;
 CHECK(PrepareGuestCofMuzzle());eventCallback(&event);++generation;
 GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==4&&!GuestCofMuzzleReady());
 CHECK(PrepareGuestCofMuzzle());eventCallback(&event);ShutdownGuestCofMuzzle();
 GuestCofMuzzleWorldPoints(&remote,&playerModel,header,bones);CHECK(visible==4&&!GuestCofMuzzleReady());
 // Match the actual native merged-P route: current render model stays the
 // player while current Studio header is the held weapon. Invalid attempts
 // must leave the one-shot latch available for its valid owner/phase.
 InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle());
 bodyModel.type=mod_studio;std::strcpy(bodyModel.name,"models/player/cof-test/cof-test.mdl");
 remote.model=&bodyModel;const auto mergedVisible=visible,mergedShells=shells;
 eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
 GuestCofMuzzleWorldPoints(&remote,nullptr,header,bones);
 GuestCofMuzzleWorldPoints(&remote,&otherModel,header,bones);
 GuestCofMuzzleWorldPoints(&remote,&bodyModel,nullptr,bones);
 CHECK(visible==mergedVisible&&shells==mergedShells&&GuestCofMuzzleWorldPending());
 GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);
 CHECK(visible==mergedVisible+1&&shells==mergedShells+1&&!GuestCofMuzzleWorldPending());
 CHECK(shellVelocity==std::array<float,3>{1,-2,3}&&shellOrigin==Transform(attachment[1]));
 GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);
 CHECK(visible==mergedVisible+1&&shells==mergedShells+1);
 ShutdownGuestCofMuzzle();
 std::printf("checks=%u failures=%u actualGlockPlayerModel=yes sceneServices=controlled live=no performance=no\n",checks,failures);
 return failures?1:0;
}
