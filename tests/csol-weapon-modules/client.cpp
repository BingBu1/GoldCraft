// SPDX-License-Identifier: GPL-3.0-or-later
// Real controller and eight real module TUs; reuse the controlled SDK engine.
#include "../csol-skull-balrog/skull5_integration_client.cpp"
#include "../../native/csol/client/weapons/cannon.hpp"
#include "../../native/csol/client/weapons/k3.hpp"
#include "../../native/csol/client/weapons/melee.hpp"
#include "../../native/csol/client/weapons/beamgun.hpp"
#include "../../native/csol/weapons/melee_identity.hpp"
#include <bit>
#include <fstream>
#if REGISTRY_AFTER
#include "../../native/csol/client/weapons/weapon_module.hpp"
#endif
namespace {
std::ofstream observations;bool capture{},checkPalette{};int spriteR{},spriteG{},spriteB{},paletteSprites{},paletteDividers{};
void* watched[32]{};int watchedCount{},entered[128]{},enteredCount{};bool observeCallbacks{};
template<class F> void Watch(F fn){if(fn)watched[watchedCount++]=reinterpret_cast<void*>(fn);}
void ClearWatch(){observeCallbacks=false;watchedCount=enteredCount=0;}
void ExpectEntered(int n,const char* label){observeCallbacks=false;Check(enteredCount==n,label);for(int i=0;i<std::min(n,enteredCount);++i)Check(entered[i]==i,label);ClearWatch();}
void CaptureSprite(int sprite,int r,int g,int b){currentSprite=sprite;spriteR=r;spriteG=g;spriteB=b;}
void CaptureDraw(int frame,int x,int y,const wrect_t* r){if(checkPalette){++paletteSprites;Check(spriteR==17&&spriteG==37&&spriteB==57,"module RGB reaches ammo digits and icon");}if(capture)observations<<"sprite "<<spriteNames[currentSprite]<<' '<<frame<<' '<<x<<' '<<y<<' '<<spriteR<<' '<<spriteG<<' '<<spriteB<<' '<<r->left<<' '<<r->right<<' '<<r->top<<' '<<r->bottom<<'\n';}
void CaptureFill(int x,int y,int w,int h,int r,int g,int b,int a){if(checkPalette&&w==2&&h==25){++paletteDividers;Check(r==17&&g==37&&b==57,"module RGB reaches ammo divider");}if(capture)observations<<"fill "<<x<<' '<<y<<' '<<w<<' '<<h<<' '<<r<<' '<<g<<' '<<b<<' '<<a<<'\n';}
constexpr int ids[]{725,726,144,197,melee::DragonSwordId,melee::SkullAxeId,96,130,112,203,220};
constexpr const char* names[]{"weapon_beamgun","weapon_beamgunle","weapon_cannon","weapon_k3","weapon_dragonsword","weapon_skullaxe","weapon_m249ex","weapon_skull1","weapon_skull5","weapon_balrog11","weapon_tknifeex2"};
constexpr int clips[]{100,100,20,150,-1,-1,120,7,24,7,1};
constexpr int reserves[]{200,200,0,200,0,0,240,35,240,32,29};
int Order(int id){for(int i=0;i<11;++i)if(ids[i]==id)return i<2?0:i<4?i-1:i<6?3:i<10?i-2:8;return 8;}
void RegisterAll(){
 Register();WeaponOwnership owned{};owned[29]=true;
 for(int i=0;i<11;++i){const bool knife=i==4||i==5;int slot=knife?2:i==7||i==10?1:0;
  Send(WeaponDefinitionMessage,EncodeWeaponDefinition({ids[i],slot,i,knife?-1:24,i==9?52:-1,knife?-1:999,i==9?8:-1,clips[i],0,names[i]}));owned[ids[i]]=true;}
 for(const auto& packet:EncodeWeaponInventory(owned,9))Send(WeaponInventoryMessage,packet);
}
void Hud(const char* label){const auto h=CurrentWeaponHudState();observations<<label<<' '<<h.id<<' '<<h.slot<<' '<<h.clip<<' '<<h.reserve<<' '<<h.secondary<<' '<<h.total<<' '<<int(h.kind)<<' '<<h.infinite<<' '<<h.specialAmmo<<'\n';}
void Result(const local_state_t& s){const auto& w=s.weapondata[0];observations<<"state "<<s.client.m_iId<<' '<<s.client.weaponanim<<' '<<s.client.health<<' '<<s.client.fov<<' '<<std::bit_cast<unsigned>(s.client.m_flNextAttack)<<' '<<s.client.vuser4[1]<<' '<<w.m_iId<<' '<<w.m_iClip<<' '<<w.m_iWeaponState<<' '<<w.m_fInReload<<' '<<std::bit_cast<unsigned>(w.m_flNextPrimaryAttack)<<' '<<std::bit_cast<unsigned>(w.m_flNextSecondaryAttack)<<' '<<std::bit_cast<unsigned>(w.m_flTimeWeaponIdle)<<' '<<w.iuser1<<' '<<w.iuser2<<' '<<w.iuser3<<' '<<w.iuser4<<' '<<std::bit_cast<unsigned>(w.fuser1)<<' '<<std::bit_cast<unsigned>(w.fuser2)<<' '<<std::bit_cast<unsigned>(w.fuser3)<<' '<<std::bit_cast<unsigned>(w.fuser4)<<' '<<animCount<<' '<<lastAnim<<' '<<soundCount<<' '<<traceCount<<'\n';}
void Lifecycle(){
#if REGISTRY_AFTER
 Check(WeaponModules().size()==8,"LTO retains eight static module registrations before initialization");
 for(int i=0;i<10;++i)Check(FindWeaponModule(ids[i])==WeaponModules()[Order(ids[i])],"all ten IDs map to ordered real module");
 Check(!FindWeaponModule(220)&&!FindWeaponModule(29),"inline throwing knife and stock remain outside registry");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->initialize);observeCallbacks=true;
#endif
 InitializeWeapons();InitializeWeaponInventory();
#if REGISTRY_AFTER
 ExpectEntered(8,"Initialize calls all eight modules in established order");
 Check(WeaponModules().size()==8,"repeated HUD initialization does not append registrations");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->initialize);observeCallbacks=true;
#endif
 InitializeWeapons();InitializeWeaponInventory();
#if REGISTRY_AFTER
 ExpectEntered(8,"second Initialize order preserved");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->reset);for(auto* m:WeaponModules())Watch(m->afterReset);observeCallbacks=true;
#endif
 ResetWeapons(false);
#if REGISTRY_AFTER
 ExpectEntered(9,"Reset all eight then CannonEffects");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->videoInit);observeCallbacks=true;
#endif
 WeaponVideoInit();
#if REGISTRY_AFTER
 ExpectEntered(8,"VideoInit ordered callback dispatch");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->receive);observeCallbacks=true;
#endif
 Select(29,-1,0);
#if REGISTRY_AFTER
 ExpectEntered(8,"reliable state visits every module");
 ClearWatch();for(auto* m:WeaponModules())Watch(m->diagnostics);observeCallbacks=true;
#endif
 std::ostringstream diagnostics;WeaponDiagnostics(diagnostics);
#if REGISTRY_AFTER
 ExpectEntered(8,"diagnostic ordering preserved");
#endif
 observations<<"lifecycle "<<diagnostics.str()<<'\n';
}
void Behavior(){
 LoadLists();engine.pfnSPR_Set=CaptureSprite;engine.pfnSPR_DrawAdditive=CaptureDraw;engine.pfnFillRGBA=CaptureFill;engine.pfnFillRGBABlend=CaptureFill;
 downstream.HUD_PostRunCmd=[](local_state_t* f,local_state_t* t,usercmd_t*,int,double,unsigned){++stockCalls;Check(f->client.m_iId<32,"host predictor never indexes extended weapon");*t=*f;};
 Lifecycle();ResetWeapons(true);RegisterAll();WeaponVideoInit();
 for(int i=0;i<11;++i)for(int lw:{0,1})for(int buttons:{0,IN_ATTACK,IN_ATTACK2,IN_RELOAD}){
  ResetWeapons(false);localPrediction=float(lw);Select(ids[i],clips[i],reserves[i],i==9?7:80);
  auto state=State(ids[i],clips[i],reserves[i],i==9?7:0);state.client.weaponanim=0;state.weapondata[0].fuser1=.95f;state.weapondata[0].m_flTimeWeaponIdle=20;
  observations<<"case "<<ids[i]<<' '<<lw<<' '<<buttons<<'\n';Hud("reliable");
#if REGISTRY_AFTER
  ClearWatch();for(auto* m:WeaponModules())Watch(m->predict);observeCallbacks=true;
#endif
  const auto result=Dispatch(state,buttons);
#if REGISTRY_AFTER
  ExpectEntered(std::min(Order(ids[i])+1,8),"real prediction dispatch preserves full prefix including inactive cleanup");
#endif
  Result(result);Hud("predicted");capture=true;WeaponRedraw(now,0);capture=false;
  const auto cooling=Dispatch(result,buttons);Result(cooling);Hud("cooling");
  const auto replay=Dispatch(state,buttons,0);Result(replay);Hud("replay");
  Select(29,-1,0);const int before=animCount;Dispatch(state,buttons);Check(animCount==before,"late old prediction cannot animate selected knife");
  Hud("knife");Select(ids[i],clips[i],reserves[i],3);Hud("switch-back");
 }
 // Exercise unknown and stock chain tails, and the real pending handoff from K3.
 for(int id:{29,16,777}){auto state=State(id,-1,0);state.client.weaponanim=0;state.weapondata[0].m_flTimeWeaponIdle=20;
#if REGISTRY_AFTER
  ClearWatch();for(auto* m:WeaponModules())Watch(m->predict);observeCallbacks=true;
#endif
  Result(Dispatch(state,0));
#if REGISTRY_AFTER
  ExpectEntered(8,"stock/unknown visit every cleanup predictor");
#endif
 }
 ResetWeapons(false);localPrediction=1;Select(197,150,200);auto k3=State(197,150,200);k3.client.weaponanim=4;k3.weapondata[0].m_flTimeWeaponIdle=20;Dispatch(k3,0);
 Select(29,-1,0);auto knife=State(29,-1,0);knife.client.weaponanim=0;const int before=animCount;Dispatch(knife,0);
 Check(animCount<=before+1&&lastAnim==0,"idle K3 to knife reconciles idle, never heavy attack");
 const int settled=animCount;Dispatch(knife,0);Check(animCount==settled,"static handoff is consumed once");
 for(auto [path,callback]:events){event_args_t args{};args.entindex=1;callback(&args);}Check(animCount==settled,"all stale custom events stay silent after selecting knife");
 ResetWeapons(false);Check(CurrentWeaponHudState().id==0,"round reset clears selected HUD");for(int id:ids)Check(FindWeaponDefinition(id),"round reset preserves definitions");
 level="maps/de_dust2.bsp";SynchronizeFrameSession();for(int id:ids)Check(!FindWeaponDefinition(id),"map reset clears definitions");Check(CurrentWeaponHudState().id==0,"map reset clears HUD");
 std::ostringstream diagnostics;WeaponDiagnostics(diagnostics);observations<<"final "<<diagnostics.str()<<'\n';
#if REGISTRY_AFTER
 Check(WeaponModules().size()==8,"map reset leaves static module catalog intact");
 for(auto* m:WeaponModules()){if(m->predictedClip)Check(m->predictedClip()==-1,"map reset clears predicted clip");if(m->predictedReserve)Check(m->predictedReserve()==-1,"map reset clears predicted reserve");}
#endif
 Check(pushes==pops&&nativeResolves==1,"trace scopes balance and host RNG is resolved once");
}
void SyntheticModule(){
#if REGISTRY_AFTER
 static constexpr int customIds[]{900};static const WeaponModule module{.ids=customIds,.order=90,.ammoKind=hud_style::AmmoKind::Magazine,.ammoSprite="sprites/csol/640hud2.spr",.ammoRect={25,49,231,255},.ammoColor={17,37,57}};
 WeaponModuleRegistration registration(module);
 Check(WeaponModules().size()==9&&FindWeaponModule(900)==&module,"independent ninth module attaches without controller edits");
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
 api->GetClientBase=[](){return nativeModule;};api->ResolveGameSymbol=[](void* module,const char* name,mh_gamesymbol_kind_t kind,void** output){++nativeResolves;Check(module==nativeModule&&!std::strcmp(name,"UTIL_SharedRandomFloat")&&kind==MH_GAMESYMBOL_KIND_FUNCTION,"host RNG ABI");*output=nativeFunction;return MH_GAMESYMBOL_OK;};
 InitializeSharedRandom();Behavior();SyntheticModule();ShutdownSharedRandom();observations.close();std::cout<<"modules client checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
int main(int argc,char** argv){
 if(argc!=3)return 2;
 auto native=std::filesystem::path(argv[1]).wstring();auto output=std::filesystem::path(argv[2]).wstring();
 wchar_t* args[]{const_cast<wchar_t*>(L"module-test"),native.data(),output.data()};
 return RunModuleChecks(3,args);
}
