// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/csol/client/weapons/guest_cof_muzzle.hpp"
#include "../../native/csol/client/weapons/guest_cof_light_equipment.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/csol/client/weapons/guest_cof_scene.hpp"
#if __has_include("../../native/csol/client/studio_attachments.hpp")
#include "../../native/csol/client/studio_attachments.hpp"
#endif
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
unsigned checks{},failures{},modelQueries{},visible{},shells{},engineLights{},customLights{},randoms{},lightReads{},boneReads{},nativeDraws{};
unsigned attachmentObservations{}, equipmentPoses{};
bool drawWrappedSkin{};
std::uint64_t generation{1};
std::uint64_t effectResources{1};
bool effectsReady{true},pointReady{true},mainView{true},retireVisible{},retireEffects{},retireModel{},localOwned{true};
cl_entity_t local{},viewEntity{},remote{},lastVisible{};
model_t viewModel{},playerModel{},flashModel{},otherModel{},bodyModel{};
model_t p345ViewModel{},p345PlayerModel{};
model_t revolverViewModel{},revolverPlayerModel{};
model_t vp70ViewModel{},vp70PlayerModel{},tmpViewModel{},tmpPlayerModel{};
model_t mp5ViewModel{},mp5PlayerModel{},mp5FlashModel{};
model_t m16ViewModel{},m16PlayerModel{},g43ViewModel{},g43PlayerModel{},shotgunViewModel{},shotgunPlayerModel{};
std::vector<std::byte> p345PlayerBytes;
std::vector<std::byte> revolverPlayerBytes;
std::vector<std::byte> vp70PlayerBytes,tmpPlayerBytes;
std::vector<std::byte> mp5PlayerBytes;
std::vector<std::byte> m16PlayerBytes,g43PlayerBytes,shotgunPlayerBytes;
bool p345Available{},p345Owned{};
bool revolverAvailable{},revolverOwned{};
bool vp70Available{},vp70Owned{},tmpAvailable{},tmpOwned{};
bool mp5Available{},mp5Owned{},mp5FlashReady{true};
bool m16Available{},m16Owned{},g43Available{},g43Owned{},shotgunAvailable{},shotgunOwned{};
cl_enginefunc_t muzzleEngine{};
event_api_t cachedEvents{};
std::vector<std::byte> playerBytes;
PointLightState pool;
dlight_t dlight{};
std::array<float,3> shellOrigin{},shellVelocity{};
float bones[MAXSTUDIOBONES][3][4]{},wrongBones[MAXSTUDIOBONES][3][4]{};
cl_entity_t* CurrentEntity(){return &remote;}
float**** LightMatrices(){++lightReads;return reinterpret_cast<float****>(bones);}
float**** BoneMatrices(){++boneReads;return reinterpret_cast<float****>(wrongBones); }
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
 if(!std::strcmp(path,P345Models[0].host))return p345Available?21:0;
 if(!std::strcmp(path,P345Models[unsigned(P345Model::Player)].host))return p345Available?22:0;
 if(!std::strcmp(path,RevolverModels[0].host))return revolverAvailable?31:0;
 if(!std::strcmp(path,RevolverModels[unsigned(RevolverModel::Player)].host))return revolverAvailable?32:0;
 if(!std::strcmp(path,TMPModels[0].host))return tmpAvailable?51:0;
 if(!std::strcmp(path,TMPModels[unsigned(TMPModel::Player)].host))return tmpAvailable?52:0;
 if(!std::strcmp(path,MP5Models[0].host))return mp5Available?61:0;
 if(!std::strcmp(path,MP5Models[unsigned(MP5Model::Player)].host))return mp5Available?62:0;
 if(!std::strcmp(path,ShotgunModels[0].host))return shotgunAvailable?91:0;
 if(!std::strcmp(path,ShotgunModels[unsigned(ShotgunModel::Player)].host))return shotgunAvailable?92:0;
 if(!std::strcmp(path,G43Models[0].host))return g43Available?81:0;
 if(!std::strcmp(path,G43Models[unsigned(G43Model::Player)].host))return g43Available?82:0;
 if(!std::strcmp(path,M16Models[0].host))return m16Available?71:0;
 if(!std::strcmp(path,M16Models[unsigned(M16Model::Player)].host))return m16Available?72:0;
 if(!std::strcmp(path,VP70Models[0].host))return vp70Available?41:0;
 if(!std::strcmp(path,VP70Models[unsigned(VP70Model::Player)].host))return vp70Available?42:0;
 // This fixture exercises maps that advertise the nine earlier firearms.
 if(!std::strcmp(path,FamasModels[unsigned(FamasModel::View)].host) ||
    !std::strcmp(path,FamasModels[unsigned(FamasModel::Player)].host))return 0;
 CHECK(!std::strcmp(path,GlockModels[unsigned(GlockModel::Player)].host));return 12;
}
model_t* Model(int id){return id==11?&viewModel:id==12?&playerModel:id==21?&p345ViewModel:id==22?&p345PlayerModel:id==31?&revolverViewModel:id==32?&revolverPlayerModel:id==41?&vp70ViewModel:id==42?&vp70PlayerModel:id==51?&tmpViewModel:id==52?&tmpPlayerModel:id==61?&mp5ViewModel:id==62?&mp5PlayerModel:id==71?&m16ViewModel:id==72?&m16PlayerModel:id==81?&g43ViewModel:id==82?&g43PlayerModel:id==91?&shotgunViewModel:id==92?&shotgunPlayerModel:nullptr;}
void* Extra(model_t* model){CHECK(model==&playerModel||model==&p345PlayerModel||model==&revolverPlayerModel||model==&vp70PlayerModel||model==&tmpPlayerModel||model==&mp5PlayerModel||model==&m16PlayerModel||model==&g43PlayerModel||model==&shotgunPlayerModel);return model==&shotgunPlayerModel?shotgunPlayerBytes.data():model==&g43PlayerModel?g43PlayerBytes.data():model==&m16PlayerModel?m16PlayerBytes.data():model==&mp5PlayerModel?mp5PlayerBytes.data():model==&tmpPlayerModel?tmpPlayerBytes.data():model==&p345PlayerModel?p345PlayerBytes.data():model==&revolverPlayerModel?revolverPlayerBytes.data():model==&vp70PlayerModel?vp70PlayerBytes.data():playerBytes.data();}
model_t* Flash(const char* path,int* id){if(!std::strcmp(path,"models/flash2.mdl")){*id=63;return mp5FlashReady?&mp5FlashModel:nullptr;}CHECK(!std::strcmp(path,"models/flash1.mdl"));*id=13;return &flashModel;}
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
bool GuestCofEffectsOwnsWeapon(Weapon value) noexcept{return effectsReady&&localOwned&&(shotgunOwned?value==Weapon::Shotgun:g43Owned?value==Weapon::G43:m16Owned?value==Weapon::M16:mp5Owned?value==Weapon::Mp5:tmpOwned?value==Weapon::Tmp:vp70Owned?value==Weapon::Vp70:revolverOwned?value==Weapon::Revolver:p345Owned?value==Weapon::P345:value==Weapon::Glock);}
bool GuestCofMuzzleModelsReady(unsigned original) noexcept{return effectsReady&&((original!=27&&original!=9&&original!=26)||mp5FlashReady);}
cl_enginefuncs_s* GuestCofMuzzleEngine() noexcept{return effectsReady?&muzzleEngine:nullptr;}
bool GuestCofPointLightsReady() noexcept{return pointReady;}
bool GuestCofMainStudioView() noexcept{return mainView;}
PointLight* AllocateGuestCofPointLight(int key) noexcept{++customLights;return &AllocatePointLight(pool,key,Time());}
void* currentStudioHeader{};model_t* currentStudioModel{};
static void ObservePlayerPose(){}
static void ObserveHeldPose(){}
void CofLightEquipmentPose(const cl_entity_s* entity,const model_s* model,const void* header)
{
 CHECK(entity==&remote && model==currentStudioModel && header==currentStudioHeader);
 ++equipmentPoses;
}
bool DrawWorldWeaponPoints(void (*drawStudio)())
{
 if(!drawWrappedSkin)return false;
 drawStudio();
 return true;
}
void ObserveStudioAttachments(const cl_entity_s*,const model_s*,const void*,const float (*observed)[3][4]) noexcept
{CHECK(observed==wrongBones);++attachmentObservations;}
static void NativePoints(){++nativeDraws;}
static void (*originalDrawPoints)()=NativePoints;
#include "native-studio-points.inc"
}
int main(int argc,char** argv)
{
 if(argc<2||argc>10)return 2;FILE* file=std::fopen(argv[1],"rb");if(!file)return 3;
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
 // Execute the unchanged production StudioDrawPoints callback: the native
 // light and geometry matrices deliberately disagree. Only light transforms
 // may position the original attachments; a wrong getter fails this case.
 InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle());
 studio.GetCurrentEntity=CurrentEntity;studio.StudioGetLightTransform=LightMatrices;studio.StudioGetBoneTransform=BoneMatrices;
 for(auto& bone:wrongBones)for(unsigned i=0;i<3;++i){bone[i][i]=1;bone[i][3]=9000.0F+float(i);}
 currentStudioModel=&bodyModel;currentStudioHeader=header;const auto boundaryShells=shells,boundaryVisible=visible;
 eventCallback(&event);StudioDrawPoints();
 CHECK(shells==boundaryShells+1&&visible==boundaryVisible+1&&lightReads==1&&boneReads==attachmentObservations&&attachmentObservations==ExpectedStudioObservers&&equipmentPoses==1&&nativeDraws==1);
 CHECK(shellOrigin==Transform(attachment[1]));
 drawWrappedSkin=true;StudioDrawPoints();drawWrappedSkin=false;
 CHECK(lightReads==1&&nativeDraws==2&&equipmentPoses==2&&shells==boundaryShells+1);
 ShutdownGuestCofMuzzle();
 if(argc>=3)
 {
  FILE* pfile=std::fopen(argv[2],"rb");CHECK(pfile!=nullptr);if(!pfile)return 3;
  std::fseek(pfile,0,SEEK_END);const long psize=std::ftell(pfile);std::rewind(pfile);CHECK(psize>0);
  p345PlayerBytes.resize(std::size_t(psize));CHECK(std::fread(p345PlayerBytes.data(),p345PlayerBytes.size(),1,pfile)==1);std::fclose(pfile);
  auto* ph=reinterpret_cast<studiohdr_t*>(p345PlayerBytes.data());CHECK(ph->length==psize&&ph->numattachments>=2);
  auto* pa=reinterpret_cast<mstudioattachment_t*>(p345PlayerBytes.data()+ph->attachmentindex);
  p345ViewModel.type=p345PlayerModel.type=mod_studio;
  std::strcpy(p345ViewModel.name,P345Models[0].host);std::strcpy(p345PlayerModel.name,P345Models[2].host);
  p345Available=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle());
  CHECK(GuestCofMuzzleReady(3)&&GuestCofMuzzleReady(7)&&!GuestCofMuzzleReady(19));
  const auto oldVisible=visible,oldShells=shells,oldQueries=modelQueries;
  remote.curstate.weaponmodel=22;event.iparam1=7;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);CHECK(visible==oldVisible&&GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,ph,bones);
  CHECK(visible==oldVisible+1&&shells==oldShells&&!GuestCofMuzzleWorldPending()&&modelQueries==oldQueries);
  const auto position=Transform(pa[0]);CHECK(std::equal(position.begin(),position.end(),lastVisible.origin));
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,ph,bones);CHECK(visible==oldVisible+1&&shells==oldShells);
  p345Owned=true;viewEntity.model=&p345ViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==oldVisible+2&&shells==oldShells);
  // One original overwrite latch still spans weapon types. Only the last
  // accepted owner/type event is consumed, once, at the matching P header.
  remote.curstate.weaponmodel=12;event.iparam1=3;eventCallback(&event);
  remote.curstate.weaponmodel=22;event.iparam1=7;eventCallback(&event);
  remote.curstate.weaponmodel=12;GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);CHECK(visible==oldVisible+2);
  remote.curstate.weaponmodel=22;GuestCofMuzzleWorldPoints(&remote,&bodyModel,ph,bones);
  CHECK(visible==oldVisible+3&&shells==oldShells&&!GuestCofMuzzleWorldPending());
  ShutdownGuestCofMuzzle();p345Owned=false;
 }
 if(argc>=4)
 {
  FILE* rfile=std::fopen(argv[3],"rb");CHECK(rfile!=nullptr);if(!rfile)return 3;
  std::fseek(rfile,0,SEEK_END);const long rsize=std::ftell(rfile);std::rewind(rfile);CHECK(rsize>0);
  revolverPlayerBytes.resize(std::size_t(rsize));CHECK(std::fread(revolverPlayerBytes.data(),revolverPlayerBytes.size(),1,rfile)==1);std::fclose(rfile);
  auto* rh=reinterpret_cast<studiohdr_t*>(revolverPlayerBytes.data());CHECK(rh->length==rsize&&rh->numattachments>=1);
  auto* ra=reinterpret_cast<mstudioattachment_t*>(revolverPlayerBytes.data()+rh->attachmentindex);
  revolverViewModel.type=revolverPlayerModel.type=mod_studio;
  std::strcpy(revolverViewModel.name,RevolverModels[0].host);std::strcpy(revolverPlayerModel.name,RevolverModels[2].host);
  revolverAvailable=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(21)&&!GuestCofMuzzleReady(19));
  const auto beforeRevolver=visible,beforeShells=shells,queriesBeforeRevolver=modelQueries;
  remote.curstate.weaponmodel=32;event.iparam1=21;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);CHECK(visible==beforeRevolver&&GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,rh,bones);
  CHECK(visible==beforeRevolver+1&&shells==beforeShells&&!GuestCofMuzzleWorldPending()&&modelQueries==queriesBeforeRevolver);
  const auto position=Transform(ra[0]);CHECK(std::equal(position.begin(),position.end(),lastVisible.origin));
  revolverOwned=true;viewEntity.model=&revolverViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==beforeRevolver+2&&shells==beforeShells&&modelQueries==queriesBeforeRevolver);
  CHECK(lastVisible.curstate.scale==6.0F);
  const int count=rh->numattachments;
  if(count>1)
  {
   const int bone=ra[1].bone;ra[1].bone=rh->numbones;
   CHECK(PrepareGuestCofMuzzle() && GuestCofMuzzleReady(3) && !GuestCofMuzzleReady(21));
   rh->numattachments=1;CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(21));
   ra[1].bone=bone;rh->numattachments=count;
  }
  ShutdownGuestCofMuzzle();revolverOwned=false;
 }
 if(argc>=5)
 {
  FILE* rfile=std::fopen(argv[4],"rb");CHECK(rfile!=nullptr);if(!rfile)return 3;
  std::fseek(rfile,0,SEEK_END);const long rsize=std::ftell(rfile);std::rewind(rfile);CHECK(rsize>0);
  vp70PlayerBytes.resize(std::size_t(rsize));CHECK(std::fread(vp70PlayerBytes.data(),vp70PlayerBytes.size(),1,rfile)==1);std::fclose(rfile);
  auto* rh=reinterpret_cast<studiohdr_t*>(vp70PlayerBytes.data());CHECK(rh->length==rsize&&rh->numattachments>=1);
  auto* ra=reinterpret_cast<mstudioattachment_t*>(vp70PlayerBytes.data()+rh->attachmentindex);
  vp70ViewModel.type=vp70PlayerModel.type=mod_studio;
  std::strcpy(vp70ViewModel.name,VP70Models[0].host);std::strcpy(vp70PlayerModel.name,VP70Models[2].host);
  vp70Available=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(21)&&GuestCofMuzzleReady(19));
  const auto beforeVP70=visible,beforeShells=shells,queriesBeforeVP70=modelQueries;
  remote.curstate.weaponmodel=42;event.iparam1=19;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);CHECK(visible==beforeVP70&&GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,rh,bones);
  CHECK(visible==beforeVP70+1&&shells==beforeShells&&!GuestCofMuzzleWorldPending()&&modelQueries==queriesBeforeVP70);
  const auto position=Transform(ra[0]);CHECK(std::equal(position.begin(),position.end(),lastVisible.origin));
  vp70Owned=true;viewEntity.model=&vp70ViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==beforeVP70+2&&shells==beforeShells&&modelQueries==queriesBeforeVP70);
  CHECK(lastVisible.curstate.scale==4.0F);
  const int count=rh->numattachments;
  if(count>1)
  {
   const int bone=ra[1].bone;ra[1].bone=rh->numbones;
   CHECK(PrepareGuestCofMuzzle() && GuestCofMuzzleReady(3) && !GuestCofMuzzleReady(19));
   rh->numattachments=1;CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(19));
   ra[1].bone=bone;rh->numattachments=count;
  }
  ShutdownGuestCofMuzzle();vp70Owned=false;
 }
 if(argc>=6)
 {
  FILE* rfile=std::fopen(argv[5],"rb");CHECK(rfile!=nullptr);if(!rfile)return 3;
  std::fseek(rfile,0,SEEK_END);const long rsize=std::ftell(rfile);std::rewind(rfile);CHECK(rsize>0);
  tmpPlayerBytes.resize(std::size_t(rsize));CHECK(std::fread(tmpPlayerBytes.data(),tmpPlayerBytes.size(),1,rfile)==1);std::fclose(rfile);
  auto* rh=reinterpret_cast<studiohdr_t*>(tmpPlayerBytes.data());CHECK(rh->length==rsize&&rh->numattachments>=1);
  auto* ra=reinterpret_cast<mstudioattachment_t*>(tmpPlayerBytes.data()+rh->attachmentindex);
  tmpViewModel.type=tmpPlayerModel.type=mod_studio;
  std::strcpy(tmpViewModel.name,TMPModels[0].host);std::strcpy(tmpPlayerModel.name,TMPModels[2].host);
  tmpAvailable=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(21)&&GuestCofMuzzleReady(14));
  const auto beforeTMP=visible,beforeShells=shells,queriesBeforeTMP=modelQueries;
  remote.curstate.weaponmodel=52;event.iparam1=14;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,header,bones);CHECK(visible==beforeTMP&&GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,rh,bones);
  CHECK(visible==beforeTMP+1&&shells==beforeShells&&!GuestCofMuzzleWorldPending()&&modelQueries==queriesBeforeTMP);
  const auto position=Transform(ra[0]);CHECK(std::equal(position.begin(),position.end(),lastVisible.origin));
  tmpOwned=true;viewEntity.model=&tmpViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==beforeTMP+2&&shells==beforeShells&&modelQueries==queriesBeforeTMP);
  CHECK(lastVisible.curstate.scale==4.0F);
  const int count=rh->numattachments;
  if(count>1)
  {
   const int bone=ra[1].bone;ra[1].bone=rh->numbones;
   CHECK(PrepareGuestCofMuzzle() && GuestCofMuzzleReady(3) && !GuestCofMuzzleReady(14));
   rh->numattachments=1;CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(14));
   ra[1].bone=bone;rh->numattachments=count;
  }
  ShutdownGuestCofMuzzle();tmpOwned=false;
 }
 if(argc>=7)
 {
  FILE* source=std::fopen(argv[6],"rb");if(!source)return 3;
  std::fseek(source,0,SEEK_END);const auto bytes=std::ftell(source);std::rewind(source);if(bytes<=0)return 4;
  mp5PlayerBytes.resize(std::size_t(bytes));if(std::fread(mp5PlayerBytes.data(),mp5PlayerBytes.size(),1,source)!=1)return 5;std::fclose(source);
  auto* mh=reinterpret_cast<studiohdr_t*>(mp5PlayerBytes.data());CHECK(mh->numattachments==1&&mh->length==bytes);
  auto* ma=reinterpret_cast<mstudioattachment_t*>(mp5PlayerBytes.data()+mh->attachmentindex);
  mp5ViewModel.type=mp5PlayerModel.type=mp5FlashModel.type=mod_studio;
  std::strcpy(mp5ViewModel.name,MP5Models[0].host);std::strcpy(mp5PlayerModel.name,MP5Models[2].host);
  mp5Available=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(27));
  const auto initial=visible,initialShells=shells,initialQueries=modelQueries;
  remote.curstate.weaponmodel=62;event.iparam1=27;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,mh,bones);
  CHECK(visible==initial+1&&lastVisible.model==&flashModel&&lastVisible.curstate.scale==2.F&&shells==initialShells);
  const auto position=Transform(ma[0]);CHECK(std::equal(position.begin(),position.end(),lastVisible.origin));
  mp5Owned=true;viewEntity.model=&mp5ViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2&&lastVisible.model==&mp5FlashModel&&lastVisible.curstate.scale==2.F);
  CHECK(shells==initialShells&&modelQueries==initialQueries);
  mp5FlashReady=false;CHECK(!GuestCofMuzzleReady(27)&&GuestCofMuzzleReady(3));
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2);
  mp5FlashReady=true;ShutdownGuestCofMuzzle();mp5Owned=false;
 }
 if(argc>=8)
 {
  FILE* source=std::fopen(argv[7],"rb");if(!source)return 3;
  std::fseek(source,0,SEEK_END);const auto bytes=std::ftell(source);std::rewind(source);if(bytes<=0)return 4;
  m16PlayerBytes.resize(std::size_t(bytes));if(std::fread(m16PlayerBytes.data(),m16PlayerBytes.size(),1,source)!=1)return 5;std::fclose(source);
  auto* mh=reinterpret_cast<studiohdr_t*>(m16PlayerBytes.data());CHECK(mh->numattachments>=1&&mh->length==bytes);
  auto* ma=reinterpret_cast<mstudioattachment_t*>(m16PlayerBytes.data()+mh->attachmentindex);
  m16ViewModel.type=m16PlayerModel.type=mod_studio;
  std::strcpy(m16ViewModel.name,M16Models[0].host);std::strcpy(m16PlayerModel.name,M16Models[2].host);
  m16Available=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(9));
  const auto initial=visible,initialShells=shells,initialQueries=modelQueries;
  remote.curstate.weaponmodel=72;event.iparam1=9;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,mh,bones);
  CHECK(visible==initial+1&&lastVisible.model==&mp5FlashModel&&lastVisible.curstate.scale==3.F&&shells==initialShells);
  const auto position=Transform(ma[0]);CHECK(std::equal(position.begin(),position.end(),lastVisible.origin));
  m16Owned=true;viewEntity.model=&m16ViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2&&lastVisible.model==&mp5FlashModel&&lastVisible.curstate.scale==6.F);
  CHECK(shells==initialShells&&modelQueries==initialQueries);
  mp5FlashReady=false;CHECK(!GuestCofMuzzleReady(9)&&GuestCofMuzzleReady(3));
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2);
  mp5FlashReady=true;ShutdownGuestCofMuzzle();m16Owned=false;
 }
 if(argc>=9)
 {
  FILE* source=std::fopen(argv[8],"rb");if(!source)return 3;
  std::fseek(source,0,SEEK_END);const long bytes=std::ftell(source);std::rewind(source);if(bytes<=0)return 4;
  g43PlayerBytes.resize(std::size_t(bytes));if(std::fread(g43PlayerBytes.data(),g43PlayerBytes.size(),1,source)!=1)return 5;std::fclose(source);
  auto* gh=reinterpret_cast<studiohdr_t*>(g43PlayerBytes.data());CHECK(gh->numattachments>=1&&gh->length==bytes);
  auto* ga=reinterpret_cast<mstudioattachment_t*>(g43PlayerBytes.data()+gh->attachmentindex);
  g43ViewModel.type=g43PlayerModel.type=mod_studio;
  std::strcpy(g43ViewModel.name,G43Models[0].host);std::strcpy(g43PlayerModel.name,G43Models[2].host);
  g43Available=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(26));
  const auto initial=visible,initialShells=shells,initialQueries=modelQueries;
  remote.curstate.weaponmodel=82;event.iparam1=26;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&g43PlayerModel,gh,bones);
  CHECK(visible==initial+1&&lastVisible.model==&flashModel&&lastVisible.curstate.scale==3.F&&shells==initialShells);
  CHECK(std::equal(lastVisible.origin,lastVisible.origin+3,Transform(ga[0]).begin()));
  g43Owned=true;viewEntity.model=&g43ViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2&&lastVisible.model==&mp5FlashModel&&lastVisible.curstate.scale==6.F);
  CHECK(shells==initialShells&&modelQueries==initialQueries);
  mp5FlashReady=false;CHECK(!GuestCofMuzzleReady(26)&&GuestCofMuzzleReady(3));
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2);
  mp5FlashReady=true;ShutdownGuestCofMuzzle();g43Owned=false;
 }
 if(argc>=10)
 {
  FILE* source=std::fopen(argv[9],"rb");if(!source)return 3;
  std::fseek(source,0,SEEK_END);const long bytes=std::ftell(source);std::rewind(source);if(bytes<=0)return 4;
  shotgunPlayerBytes.resize(std::size_t(bytes));if(std::fread(shotgunPlayerBytes.data(),shotgunPlayerBytes.size(),1,source)!=1)return 5;std::fclose(source);
  auto* sh=reinterpret_cast<studiohdr_t*>(shotgunPlayerBytes.data());CHECK(sh->numattachments==2&&sh->length==bytes); // Original model stores two; type4 consumes only attachment0.
  auto* sa=reinterpret_cast<mstudioattachment_t*>(shotgunPlayerBytes.data()+sh->attachmentindex);
  shotgunViewModel.type=shotgunPlayerModel.type=mod_studio;
  std::strcpy(shotgunViewModel.name,ShotgunModels[0].host);std::strcpy(shotgunPlayerModel.name,ShotgunModels[2].host);
  shotgunAvailable=true;InitializeGuestCofMuzzle(&provider);CHECK(PrepareGuestCofMuzzle()&&GuestCofMuzzleReady(4));
  const auto initial=visible,initialShells=shells,initialQueries=modelQueries;
  remote.curstate.weaponmodel=92;event.iparam1=4;eventCallback(&event);CHECK(GuestCofMuzzleWorldPending());
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,sh,bones);
  CHECK(visible==initial+1&&lastVisible.model==&flashModel&&lastVisible.curstate.scale==3.F&&shells==initialShells);
  CHECK(std::equal(lastVisible.origin,lastVisible.origin+3,Transform(sa[0]).begin()));
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,sh,bones);CHECK(visible==initial+1);
  shotgunOwned=true;viewEntity.model=&shotgunViewModel;
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+2&&lastVisible.model==&flashModel&&lastVisible.curstate.scale==6.F);
  CHECK(shells==initialShells&&modelQueries==initialQueries);
  mp5FlashReady=false;CHECK(GuestCofMuzzleReady(4)&&GuestCofMuzzleReady(3));
  CHECK(GuestCofMuzzleStudioEvent(&studioEvent,&viewEntity)&&visible==initial+3&&lastVisible.model==&flashModel);
  mp5FlashReady=true;eventCallback(&event);++generation;
  GuestCofMuzzleWorldPoints(&remote,&bodyModel,sh,bones);CHECK(visible==initial+3&&!GuestCofMuzzleReady(4));
  ShutdownGuestCofMuzzle();shotgunOwned=false;
 }
 std::printf("checks=%u failures=%u actualGlockPlayerModel=yes sceneServices=controlled live=no performance=no\n",checks,failures);
 return failures?1:0;
}
