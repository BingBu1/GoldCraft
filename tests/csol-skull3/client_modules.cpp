// SPDX-License-Identifier: GPL-3.0-or-later
// Current real controller, eleven descriptors and ten predictor owners.
// Only unrelated UI/GL boundaries are controlled; scope pixels have separate GPU tests.
#include "../csol-skull-balrog/skull5_integration_client.cpp"
#include "../../native/csol/client/weapons/cannon.hpp"
#include "../../native/csol/client/weapons/k3.hpp"
#include "../../native/csol/client/weapons/melee.hpp"
#include "../../native/csol/client/weapons/beamgun.hpp"
#include "../../native/csol/weapons/melee_identity.hpp"
#include <bit>
#include <fstream>
#define REGISTRY_AFTER 1
#if REGISTRY_AFTER
#include "../../native/csol/client/weapons/weapon_module.hpp"
#endif
namespace csol::client {
void ResetSniperScope(){} void DrawSniperScope(int,int,int){}
bool HudProportionalValue(int normalized,int& scaled){scaled=normalized*2;return true;}
}
namespace {
std::ofstream observations;bool capture{},checkPalette{};int spriteR{},spriteG{},spriteB{},paletteSprites{},paletteDividers{};
void* watched[32]{};int watchedCount{},entered[128]{},enteredCount{};bool observeCallbacks{};
template<class F> void Watch(F fn){if(fn)watched[watchedCount++]=reinterpret_cast<void*>(fn);}
void ClearWatch(){observeCallbacks=false;watchedCount=enteredCount=0;}
void ExpectEntered(int n,const char* label){observeCallbacks=false;Check(enteredCount==n,label);for(int i=0;i<std::min(n,enteredCount);++i)Check(entered[i]==i,label);ClearWatch();}
void CaptureSprite(int sprite,int r,int g,int b){currentSprite=sprite;spriteR=r;spriteG=g;spriteB=b;}
void CaptureDraw(int frame,int x,int y,const wrect_t* r){if(checkPalette){++paletteSprites;Check(spriteR==17&&spriteG==37&&spriteB==57,"module RGB reaches ammo digits and icon");}if(capture)observations<<"sprite "<<spriteNames[currentSprite]<<' '<<frame<<' '<<x<<' '<<y<<' '<<spriteR<<' '<<spriteG<<' '<<spriteB<<' '<<r->left<<' '<<r->right<<' '<<r->top<<' '<<r->bottom<<'\n';}
void CaptureFill(int x,int y,int w,int h,int r,int g,int b,int a){if(checkPalette&&w==2&&h==25){++paletteDividers;Check(r==17&&g==37&&b==57,"module RGB reaches ammo divider");}if(capture)observations<<"fill "<<x<<' '<<y<<' '<<w<<' '<<h<<' '<<r<<' '<<g<<' '<<b<<' '<<a<<'\n';}
struct WeaponCase {int id,slot,clip,reserve,charge,module,predictor;const char* name;};
constexpr WeaponCase cases[]{
 {725,0,100,200,80,0,0,"weapon_beamgun"},{726,0,100,200,80,0,0,"weapon_beamgunle"},
 {144,0,20,0,0,1,1,"weapon_cannon"},{197,0,150,200,0,2,2,"weapon_k3"},
 {1022,2,-1,0,0,3,3,"weapon_dragonsword"},{1023,2,-1,0,0,3,3,"weapon_skullaxe"},
 {96,0,120,240,0,4,4,"weapon_m249ex"},{130,1,7,35,0,5,5,"weapon_skull1"},
 {312,1,7,35,0,6,6,"weapon_skull2"},{142,0,35,200,0,7,7,"weapon_skull3"},
 {143,0,70,200,0,8,7,"weapon_skull3d"},{112,0,24,240,0,9,8,"weapon_skull5"},
 {203,0,7,32,7,10,9,"weapon_balrog11"},{220,1,1,29,0,-1,10,"weapon_tknifeex2"}};
unsigned inventoryGeneration=9;
void Own(std::initializer_list<int> ids){WeaponOwnership owned{};for(int id:ids)owned[id]=true;for(const auto& packet:EncodeWeaponInventory(owned,++inventoryGeneration))Send(WeaponInventoryMessage,packet);}
void RegisterAll(){
 Register();WeaponOwnership owned{};owned[29]=true;
 int position{};
 for(const auto& c:cases){const bool knife=c.slot==2;
  Send(WeaponDefinitionMessage,EncodeWeaponDefinition({c.id,c.slot,position++,knife?-1:24,c.id==203?52:-1,knife?-1:999,c.id==203?8:-1,c.clip,0,c.name}));owned[c.id]=true;}
 for(const auto& packet:EncodeWeaponInventory(owned,++inventoryGeneration))Send(WeaponInventoryMessage,packet);
}
void SlotSelection(){
 for(const auto& c:cases){
  Own({c.id});Select(29,-1,0);sent.clear();
  commands.at("slot"+std::to_string(c.slot+1))();
  usercmd_t selection{};selection.buttons=IN_ATTACK;
  WeaponInventoryCreateMove(.01f,&selection,1);
  Check(!(selection.buttons&IN_ATTACK)&&sent==c.name,"every extended module actual slot confirms exact classname without firing");
 }
 std::vector<std::byte> stock;for(char ch:std::string("weapon_hegrenade"))stock.push_back(std::byte(ch));stock.push_back(std::byte{});
 for(int value:{12,1,255,255,3,0,4,0})stock.push_back(std::byte(value));Send("WeaponList",stock);
 Own({142,130,29,4});Select(142,35,200);
 for(const auto& [slot,name]:std::initializer_list<std::pair<int,const char*>>{{1,"weapon_skull3"},{2,"weapon_skull1"},{3,"weapon_knife"},{4,"weapon_hegrenade"}}){
  commands.at("slot"+std::to_string(slot))();usercmd_t selection{};selection.buttons=IN_ATTACK;sent.clear();
  WeaponInventoryCreateMove(.01f,&selection,1);Check(!(selection.buttons&IN_ATTACK)&&sent==name,"native keys 1-4 select correct slot with extended inventory");
 }
 RegisterAll();
}
void Hud(const char* label){const auto h=CurrentWeaponHudState();observations<<label<<' '<<h.id<<' '<<h.slot<<' '<<h.clip<<' '<<h.reserve<<' '<<h.secondary<<' '<<h.total<<' '<<int(h.kind)<<' '<<h.infinite<<' '<<h.specialAmmo<<'\n';}
void Result(const local_state_t& s){const auto& w=s.weapondata[0];observations<<"state "<<s.client.m_iId<<' '<<s.client.weaponanim<<' '<<s.client.health<<' '<<s.client.fov<<' '<<std::bit_cast<unsigned>(s.client.m_flNextAttack)<<' '<<s.client.vuser4[1]<<' '<<w.m_iId<<' '<<w.m_iClip<<' '<<w.m_iWeaponState<<' '<<w.m_fInReload<<' '<<std::bit_cast<unsigned>(w.m_flNextPrimaryAttack)<<' '<<std::bit_cast<unsigned>(w.m_flNextSecondaryAttack)<<' '<<std::bit_cast<unsigned>(w.m_flTimeWeaponIdle)<<' '<<w.iuser1<<' '<<w.iuser2<<' '<<w.iuser3<<' '<<w.iuser4<<' '<<std::bit_cast<unsigned>(w.fuser1)<<' '<<std::bit_cast<unsigned>(w.fuser2)<<' '<<std::bit_cast<unsigned>(w.fuser3)<<' '<<std::bit_cast<unsigned>(w.fuser4)<<' '<<animCount<<' '<<lastAnim<<' '<<soundCount<<' '<<traceCount<<'\n';}
void Lifecycle(){
#if REGISTRY_AFTER
 Check(WeaponModules().size()==11,"LTO retains eleven static module registrations before initialization");
 for(const auto& c:cases)if(c.module>=0)Check(FindWeaponModule(c.id)==WeaponModules()[c.module],"all thirteen IDs map to ordered actual descriptors");
 Check(!FindWeaponModule(220)&&!FindWeaponModule(29),"inline throwing knife and stock remain outside registry");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->initialize);observeCallbacks=true;
#endif
 InitializeWeapons();InitializeWeaponInventory();
#if REGISTRY_AFTER
 ExpectEntered(10,"Initialize calls all ten modules in established order");
 Check(WeaponModules().size()==11,"repeated HUD initialization does not append registrations");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->initialize);observeCallbacks=true;
#endif
 InitializeWeapons();InitializeWeaponInventory();
#if REGISTRY_AFTER
 ExpectEntered(10,"second Initialize order preserved");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->reset);for(auto* m:WeaponModules())Watch(m->afterReset);observeCallbacks=true;
#endif
 ResetWeapons(false);
#if REGISTRY_AFTER
 ExpectEntered(11,"Reset all ten then CannonEffects");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->videoInit);observeCallbacks=true;
#endif
 WeaponVideoInit();
#if REGISTRY_AFTER
 ExpectEntered(10,"VideoInit ordered callback dispatch");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->receive);observeCallbacks=true;
#endif
 Select(29,-1,0);
#if REGISTRY_AFTER
 ExpectEntered(10,"reliable state visits every module");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->diagnostics);observeCallbacks=true;
#endif
 std::ostringstream diagnostics;WeaponDiagnostics(diagnostics);
#if REGISTRY_AFTER
 ExpectEntered(10,"diagnostic ordering preserved");
#endif
 observations<<"lifecycle "<<diagnostics.str()<<'\n';
}
void Behavior(){
 LoadLists();engine.pfnSPR_Set=CaptureSprite;engine.pfnSPR_DrawAdditive=CaptureDraw;engine.pfnFillRGBA=CaptureFill;engine.pfnFillRGBABlend=CaptureFill;
 downstream.HUD_PostRunCmd=[](local_state_t* f,local_state_t* t,usercmd_t*,int,double,unsigned){++stockCalls;Check(f->client.m_iId<32,"host predictor never indexes extended weapon");*t=*f;};
 Lifecycle();ResetWeapons(true);RegisterAll();WeaponVideoInit();SlotSelection();
 for(const auto& c:cases)for(int lw:{0,1})for(int buttons:{0,IN_ATTACK,IN_ATTACK2,IN_RELOAD}){
  ResetWeapons(false);localPrediction=float(lw);Select(c.id,c.clip,c.reserve,c.charge);
  auto state=State(c.id,c.clip,c.reserve,c.charge);state.client.weaponanim=0;state.weapondata[0].fuser1=.95f;state.weapondata[0].m_flTimeWeaponIdle=20;
  observations<<"case "<<c.id<<' '<<lw<<' '<<buttons<<'\n';Hud("reliable");
#if REGISTRY_AFTER
  ClearWatch();for(auto* m:WeaponModules())Watch(m->predict);observeCallbacks=true;
#endif
  const auto result=Dispatch(state,buttons);
#if REGISTRY_AFTER
  ExpectEntered(std::min(c.predictor+1,10),"real prediction dispatch preserves full prefix including inactive cleanup");
#endif
  Result(result);Hud("predicted");capture=true;WeaponRedraw(now,0);capture=false;
  const auto cooling=Dispatch(result,buttons);Result(cooling);Hud("cooling");
  const auto replay=Dispatch(state,buttons,0);Result(replay);Hud("replay");
  Select(29,-1,0);const int before=animCount;Dispatch(state,buttons);Check(animCount==before,"late old prediction cannot animate selected knife");
  Hud("knife");Select(c.id,c.clip,c.reserve,3);Hud("switch-back");
 }
 // Exercise unknown and stock chain tails, and the real pending handoff from K3.
 for(int id:{29,16,777}){auto state=State(id,-1,0);state.client.weaponanim=0;state.weapondata[0].m_flTimeWeaponIdle=20;
#if REGISTRY_AFTER
  ClearWatch();for(auto* m:WeaponModules())Watch(m->predict);observeCallbacks=true;
#endif
  Result(Dispatch(state,0));
#if REGISTRY_AFTER
  ExpectEntered(10,"stock/unknown visit every cleanup predictor");
#endif
 }
 ResetWeapons(false);localPrediction=1;Select(197,150,200);auto k3=State(197,150,200);k3.client.weaponanim=4;k3.weapondata[0].m_flTimeWeaponIdle=20;Dispatch(k3,0);
 Select(29,-1,0);auto knife=State(29,-1,0);knife.client.weaponanim=0;const int before=animCount;Dispatch(knife,0);
 Check(animCount<=before+1&&lastAnim==0,"idle K3 to knife reconciles idle, never heavy attack");
 const int settled=animCount;Dispatch(knife,0);Check(animCount==settled,"static handoff is consumed once");
 for(auto [path,callback]:events){event_args_t args{};args.entindex=1;callback(&args);}Check(animCount==settled,"all stale custom events stay silent after selecting knife");
 ResetWeapons(false);Check(CurrentWeaponHudState().id==0,"round reset clears selected HUD");for(const auto& c:cases)Check(FindWeaponDefinition(c.id),"round reset preserves definitions");
 level="maps/de_dust2.bsp";SynchronizeFrameSession();for(const auto& c:cases)Check(!FindWeaponDefinition(c.id),"map reset clears definitions");Check(CurrentWeaponHudState().id==0,"map reset clears HUD");
 std::ostringstream diagnostics;WeaponDiagnostics(diagnostics);observations<<"final "<<diagnostics.str()<<'\n';
#if REGISTRY_AFTER
 Check(WeaponModules().size()==11,"map reset leaves static module catalog intact");
 for(auto* m:WeaponModules()){if(m->predictedClip)Check(m->predictedClip()==-1,"map reset clears predicted clip");if(m->predictedReserve)Check(m->predictedReserve()==-1,"map reset clears predicted reserve");}
#endif
 Check(pushes==pops&&nativeResolves==2,"trace scopes balance and host RNG is resolved once");
}
void SyntheticModule(){
#if REGISTRY_AFTER
 static constexpr int customIds[]{900};static const WeaponModule module{.ids=customIds,.order=90,.ammoKind=hud_style::AmmoKind::Magazine,.ammoSprite="sprites/csol/640hud2.spr",.ammoRect={25,49,231,255},.ammoColor={17,37,57}};
 WeaponModuleRegistration registration(module);
 Check(WeaponModules().size()==12&&FindWeaponModule(900)==&module,"independent twelfth module attaches without controller edits");
 Send(WeaponDefinitionMessage,EncodeWeaponDefinition({900,0,1,24,-1,200,-1,12,0,"weapon_registry_test"}));
 WeaponOwnership owned{};owned[900]=true;for(const auto& packet:EncodeWeaponInventory(owned,10))Send(WeaponInventoryMessage,packet);
 Select(900,12,123);WeaponVideoInit();checkPalette=true;WeaponRedraw(now,0);checkPalette=false;
 Check(paletteSprites==6&&paletteDividers==1,"new magazine module draws five digits plus icon and divider");
#endif
}
}
extern "C" void __cyg_profile_func_enter(void* fn,void*){if(!observeCallbacks)return;for(int i=0;i<watchedCount;++i)if(watched[i]==fn){if(enteredCount<128)entered[enteredCount++]=i;return;}}
extern "C" void __cyg_profile_func_exit(void*,void*){}
int RunModuleChecks(int argc,wchar_t** argv){try{
 if(argc!=3)return 2;observations.open(std::filesystem::path(argv[2]));if(!observations)return 2;
 const auto path=std::filesystem::path(argv[1]).string();native_random_test::Image original(path.c_str(),native_fixture::spans);
 Setup();nativeModule=original.Base();nativeFunction=original.Address<void*>(native_fixture::functionRva);
 api->GetClientBase=[](){return nativeModule;};api->ResolveGameSymbol=[](void* module,const char* name,mh_gamesymbol_kind_t kind,void** output){
  ++nativeResolves;Check(module==nativeModule,"random imports resolve against matched host module");
  if(!std::strcmp(name,"UTIL_SharedRandomFloat")){Check(kind==MH_GAMESYMBOL_KIND_FUNCTION,"host shared RNG ABI");*output=nativeFunction;}
  else{Check(!std::strcmp(name,"CSOL_ClientCRTRand")&&kind==MH_GAMESYMBOL_KIND_GLOBAL,"host CRT rand import slot ABI");static int(__cdecl* crt)()=+[]()->int{return 1;};*output=&crt;}
  return MH_GAMESYMBOL_OK;};
 InitializeSharedRandom();Behavior();SyntheticModule();ShutdownSharedRandom();observations.close();std::cout<<"modules client checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
int main(int argc,char** argv){
 if(argc!=3)return 2;
 auto native=std::filesystem::path(argv[1]).wstring();auto output=std::filesystem::path(argv[2]).wstring();
 wchar_t* args[]{const_cast<wchar_t*>(L"module-test"),native.data(),output.data()};
 return RunModuleChecks(3,args);
}
