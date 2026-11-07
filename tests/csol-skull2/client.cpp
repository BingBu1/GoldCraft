// SPDX-License-Identifier: GPL-3.0-or-later
// Actual production TU and SDK types. Expected fields come from the fixed
// original SKULL2 event/MP captures, not from the implementation's helpers.
#include "../../native/csol/client/weapons/skull2.cpp"
#include "../../native/csol/client/shared_random.cpp"
#include "../../native/csol/client/weapons/weapon_module.cpp"
#include "../../native/csol/client/weapons/gun_smoke.cpp"
#include "../../native/csol/client/weapons/skull5.hpp"
#include <iostream>
#include <map>
#include <limits>

namespace csol::client {
cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};
metahook_api_t apiStorage{};metahook_api_t* api=&apiStorage;
WeaponDefinition fixtureDefinition{312,1,1,32,-1,35,-1,7,0,"weapon_skull2"};
bool fixturePresented=true;int fixtureActiveId=312;
const WeaponDefinition* FindWeaponDefinition(int id){return id==312?&fixtureDefinition:nullptr;}
bool WeaponPresentationAllowed(int id){return id==fixtureActiveId&&fixturePresented;}
}
namespace fixture {
using namespace csol;using namespace csol::client;
unsigned checks{},failures{},animationCount{},soundCount{},shellCount{},smokeCount{},pushes{},pops{},stockCount{},randomCalls{},oldSmokeCalls{};
int animation{},body{},pitch{},rngChoice{};float attenuation{},volume{},clWeapons=1;
float basisAngles[3]{},rayStart[3]{},rayEnd[3]{},shellOrigin[3]{},shellVelocity[3]{},shellAngles[3]{},shellLife{};int shellSound{};
std::string sound;std::map<std::string,void(*)(event_args_t*)> hooks;
std::map<std::string,xcommand_t> commands;std::map<int,std::string> models;
std::array<TEMPENTITY,1024> smoke{};TEMPENTITY shell{};cl_entity_t view{},local{};
event_api_t ev{};efx_api_t fx{};physent_t brush{};
struct Draw {char kind;float lo,hi;};std::vector<Draw> rng;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
bool Near(float x,float y){return std::abs(x-y)<.001f;}
void Same3(const float* a,const float* b,const char* message){for(int i=0;i<3;++i)Check(Near(a[i],b[i]),message);}
float __cdecl HostRandom(unsigned seed,float lo,float hi){++randomCalls;Check(seed>=123&&seed<=126&&lo==-.5f&&hi==.5f,"host shared RNG ABI");return float(seed-124)*.125f;}
void Setup(bool initializeModules=true){
 apiStorage.GetClientBase=[]()->void*{return reinterpret_cast<void*>(0x10000000);};
 apiStorage.ResolveGameSymbol=[](void*,const char* name,mh_gamesymbol_kind_t kind,void** p)->mh_gamesymbol_status_t{
  Check(!std::strcmp(name,"UTIL_SharedRandomFloat")&&kind==MH_GAMESYMBOL_KIND_FUNCTION,"native RNG symbol");*p=reinterpret_cast<void*>(&HostRandom);return MH_GAMESYMBOL_OK;};
 apiStorage.HookCmd=[](const char* name,xcommand_t fn)->xcommand_t{auto old=commands[name];commands[name]=fn;return old;};
 engine.pfnAddCommand=[](const char* name,xcommand_t fn){commands[name]=fn;return 1;};
 commands["gunsmoke"]=[](){++oldSmokeCalls;};
 local.index=1;view.curstate.body=9;
 for(int a=0;a<4;++a)for(int i=0;i<3;++i)view.attachment[a][i]=float(100*a+10*(i+1));
 engine.GetViewModel=[](){return &view;};engine.GetLocalPlayer=[](){return &local;};
 engine.pfnGetCvarFloat=[](const char* name){return !std::strcmp(name,"cl_lw")?clWeapons:1.f;};
 engine.pfnWeaponAnim=[](int a,int b){++animationCount;animation=a;body=b;};
 engine.pfnRandomLong=[](int lo,int hi){rng.push_back({'L',float(lo),float(hi)});return std::clamp(rngChoice,lo,hi);};
 engine.pfnRandomFloat=[](float lo,float hi){rng.push_back({'F',lo,hi});return (lo+hi)*.5f;};
 engine.pfnAngleVectors=[](const float* a,float* f,float* r,float* u){std::copy_n(a,3,basisAngles);std::fill_n(f,3,0);std::fill_n(r,3,0);std::fill_n(u,3,0);f[0]=1;r[1]=-1;u[2]=1;};
 engine.pfnHookEvent=[](const char* n,void(*fn)(event_args_t*)){hooks[n]=fn;};
 downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t*,int,double,unsigned){++stockCount;Check(from->client.m_iId==0,"stock predictor never sees 312");*to=*from;to->client.weaponanim=99;to->client.viewmodel=99;to->client.vuser4[1]=99;};
 engine.pEventAPI=&ev;engine.pEfxAPI=&fx;
 ev.EV_IsLocal=[](int n){return int(n==0);};ev.EV_LocalPlayerViewheight=[](float* p){p[0]=p[1]=0;p[2]=28;};
 ev.EV_PlaySound=[](int,float*,int channel,const char* name,float v,float a,int,int pi){Check(channel==CHAN_WEAPON,"weapon sound channel");++soundCount;sound=name;volume=v;attenuation=a;pitch=pi;};
 ev.EV_FindModelIndex=[](const char* name){const int id=int(models.size()+1);models[id]=name;return id;};
 ev.EV_SetUpPlayerPrediction=[](int a,int b){Check(a==0&&b==1,"event prediction flags");};
 ev.EV_PushPMStates=[](){++pushes;};ev.EV_PopPMStates=[](){++pops;};
 ev.EV_SetSolidPlayers=[](int n){Check(n==0||n==1,"entity player index");};ev.EV_SetTraceHull=[](int n){Check(n==2,"original visual point hull");};
 ev.EV_PlayerTrace=[](float* from,float* to,int,int,pmtrace_t* t){t->fraction=1;std::copy_n(from,3,rayStart);std::copy_n(to,3,rayEnd);};
 ev.EV_GetPhysent=[](int){return &brush;};
 fx.R_TempModel=[](float* o,float* v,float* a,float life,int id,int type)->TEMPENTITY*{
  ++shellCount;Check(models[id]=="models/pshell.mdl","original shell model");std::copy_n(o,3,shellOrigin);std::copy_n(v,3,shellVelocity);std::copy_n(a,3,shellAngles);
  shellLife=life;shellSound=type;shell={};shell.entity.index=123;shell.entity.baseline.body=77;
  shell.entity.baseline.angles[0]=2;shell.entity.baseline.angles[1]=3;shell.entity.baseline.angles[2]=4;return &shell;};
 fx.R_DefaultSprite=[](float* o,int,float rate)->TEMPENTITY*{Check(rate==35,"original smoke frame rate");auto& s=smoke.at(smokeCount++);s={};std::copy_n(o,3,s.entity.origin);return &s;};
 InitializeSharedRandom();if(!initializeModules)return;InitializeWeaponModules();
 Check(hooks.contains("events/skull2_a.sc")&&hooks.contains("events/skull2_left.sc")&&hooks.contains("events/skull2_right.sc"),"three exact event resources registered");
 const auto* m=FindWeaponModule(312);Check(m&&m->ammoKind==hud_style::AmmoKind::Magazine&&m->crosshair.gap==8&&m->crosshair.shotDelta==3&&m->crosshair.speed==0&&m->crosshair.flags==7,"original ordinary magazine and own crosshair fields");
}
event_args_t Args(bool localEvent=true){event_args_t a{};a.entindex=localEvent?1:2;a.origin[0]=100;a.origin[1]=200;a.origin[2]=300;a.angles[0]=10;a.angles[1]=20;a.angles[2]=30;a.velocity[0]=4;a.velocity[1]=8;a.velocity[2]=12;return a;}
void Events(){
 auto a=Args();a.iparam2=7;const auto initial=smokeCount;hooks.at("events/skull2_a.sc")(&a);
 Check(smokeCount==initial,"original gunsmoke BSS defaults disabled");
 commands.at("gunsmoke")();Check(oldSmokeCalls==1,"gunsmoke preserves downstream handler");
 for(int kind=0;kind<3;++kind)for(bool localEvent:{false,true})for(int clip:{0,1,2,14})for(int choice:{0,15}){
  auto args=Args(localEvent);args.fparam1=.125f;args.fparam2=.02f;args.iparam1=kind?3:125;args.iparam2=kind?clip:-250;
  args.bparam1=clip!=0;rngChoice=choice;const auto beforeA=animationCount,beforeS=shellCount,beforeP=smokeCount;
  const char* event=kind==0?"events/skull2_a.sc":kind==1?"events/skull2_left.sc":"events/skull2_right.sc";
  rng.clear();hooks.at(event)(&args);
  Check(sound=="weapons/skull2-1.wav"&&volume==1&&attenuation==(kind?.8f:.6f)&&pitch==94+choice,"original event sound/volume/attenuation/pitch");
  const float wantedAngles[]{kind?10.f:11.25f,kind?20.f:17.5f,30};Same3(basisAngles,wantedAngles,"A punch integers and B direction/clip separation");
  Check(animationCount==beforeA+(localEvent?1:0),"remote event never animates local model");
  if(localEvent)Check(animation==(kind==0?3:kind==1?(clip>1?7:9):(clip>1?10:12))&&body==9,"original shoot/last-round sequence preserves character body");
  Check(smokeCount==beforeP+(localEvent?3:0),"three local-only enabled smoke layers");
  if(localEvent)for(unsigned i=beforeP;i<smokeCount;++i)Same3(smoke[i].entity.origin,view.attachment[kind==2?1:0],"left/right muzzle attachment");
  Check(shellCount==beforeS+(kind?1:0),"A has no shell creation; B creates one");
  if(kind){
   Check(shellLife==2&&shellSound==1&&shell.entity.baseline.body==0&&shell.entity.index==123,"shell life2/body0 does not rewrite entity index");
   const float rotation[]{10,15,20};Same3(shell.entity.baseline.angles,rotation,"shell angular multiplier5");
   const float sa[]{0,20,0};Same3(shellAngles,sa,"unpunched B shell yaw");
   const float vel[]{localEvent?67.125f:89.5f,localEvent?(kind==1?-49.5f:55.5f):74.f,localEvent?123.25f:131.f};
   Same3(shellVelocity,vel,"original shell local/remote random-sign math");
   if(localEvent)Same3(shellOrigin,view.attachment[kind==1?2:3],"left/right ejection attachment");
   else{const float origin[]{120.4f,196.8f,317.2f};Same3(shellOrigin,origin,"remote shell offset and half-velocity");}
  }
  const float start[]{100,200.f+(kind==1?5.f:kind==2?-5.f:0.f),328};Same3(rayStart,start,"original bullet side offset");
  const float end[]{8292,start[1]-(kind?.02f:.125f)*8192,328+(kind?.03f:.02f)*8192};Same3(rayEnd,end,"event field conversion and8192 visual range");
 }
 // Both command hooks and all three events must remain stable on HUD reinit.
 InitializeSkull2();InitializeSkull5();InitializeWeaponModules();commands.at("gunsmoke")();Check(oldSmokeCalls==2&&!GunSmokeEnabled(),"both modules and registry reinit do not double-toggle or recurse");
 const auto off=smokeCount;hooks.at("events/skull2_left.sc")(&a);Check(smokeCount==off,"shared smoke toggle disables all modes");commands.at("gunsmoke")();
 fixturePresented=false;
 for(const char* name:{"events/skull2_a.sc","events/skull2_left.sc","events/skull2_right.sc"}){const auto beforeA=animationCount,beforeP=smokeCount;auto late=Args();hooks.at(name)(&late);Check(animationCount==beforeA&&smokeCount==beforeP,"late local event cannot animate/smoke next weapon");}
 fixturePresented=true;auto bad=Args();bad.origin[0]=std::numeric_limits<float>::quiet_NaN();const auto before=soundCount;hooks.at("events/skull2_a.sc")(&bad);Check(soundCount==before,"invalid event rejected");
}
local_state_t State(int mode=4,int clip=7,int reserve=35){local_state_t s{};s.client.m_iId=312;s.client.health=100;s.client.iuser3=1;s.client.flags=FL_ONGROUND;s.client.viewmodel=12;s.client.weaponanim=mode==8?14:2;s.client.vuser4[1]=float(reserve);auto& w=s.weapondata[0];w.m_iId=312;w.m_iWeaponState=mode;w.m_iClip=clip;w.fuser1=mode==8?.9f:.95f;w.m_flNextPrimaryAttack=w.m_flNextSecondaryAttack=w.m_flTimeWeaponIdle=-.001f;return s;}
local_state_t Predict(local_state_t s,int buttons,int run=1){local_state_t t{};usercmd_t c{};c.buttons=short(buttons);c.msec=10;
 Check(PredictSkull2(&s,&t,&c,run,100,123),"actual predictor handles312");Check(s.client.m_iId==312&&t.client.m_iId==312&&t.client.viewmodel==12,"restores native prediction identity/model");return t;}
void Ready(local_state_t& s){s.weapondata[0].m_flNextPrimaryAttack=s.weapondata[0].m_flNextSecondaryAttack=-.001f;s.client.m_flNextAttack=0;}
void Prediction(){
 for(int mode:{4,8}){
  ResetSkull2();auto s=State(mode,mode==8?14:7);if(mode==8)s.weapondata[0].m_iWeaponState|=2;
  auto fired=Predict(s,IN_ATTACK);Check(fired.weapondata[0].m_iClip==s.weapondata[0].m_iClip-1,"exactly one round consumed");
  Check(animation==(mode==8?7:3)&&Near(fired.weapondata[0].m_flNextPrimaryAttack,mode==8?.15f:.23f),"original A/B animation and interval");
  Ready(fired);auto held=Predict(fired,IN_ATTACK);Check(held.weapondata[0].m_iClip==fired.weapondata[0].m_iClip,"held trigger remains semiautomatic after cooldown");
  auto released=Predict(held,0);Ready(released);auto again=Predict(released,IN_ATTACK);Check(again.weapondata[0].m_iClip==held.weapondata[0].m_iClip-1,"release permits next shot");if(mode==8)Check(animation==10,"dual pistol alternates to right after release");
  const auto a=animationCount,so=soundCount,sp=smokeCount;auto replay=Predict(s,IN_ATTACK,0);Check(replay.weapondata[0].m_iClip==s.weapondata[0].m_iClip-1&&animationCount==a&&soundCount==so&&smokeCount==sp,"prediction replay has no effects");
  auto reload=Predict(State(mode,2,3),IN_RELOAD);Check(reload.weapondata[0].m_fInReload&&animation==(mode==8?13:1)&&Near(reload.client.m_flNextAttack,mode==8?3.29f:2.49f),"original reload starts with correct time/sequence");
  reload.client.m_flNextAttack=0;reload=Predict(reload,0);Check(reload.weapondata[0].m_iClip==5&&reload.client.vuser4[1]==0&&!(reload.weapondata[0].m_iWeaponState&2),"odd final clip clears next-left after real reload completion");
  auto even=State(mode,3,3);even.weapondata[0].m_fInReload=1;even=Predict(even,0);Check(even.weapondata[0].m_iClip==6&&(even.weapondata[0].m_iWeaponState&2),"even final clip sets next-left after completion for either mode");
 }
 auto s=Predict(State(4,7,3),IN_ATTACK2);Check((s.weapondata[0].m_iWeaponState&0x3C)==16&&animation==15&&s.weapondata[0].m_iClip==7,"A toB starts pending without premature ammo");
 s.weapondata[0].m_flNextReload=0;auto equal=Predict(s,0);Check((equal.weapondata[0].m_iWeaponState&0x3C)==16&&equal.weapondata[0].m_iClip==7,"switch equality stays pending");
 auto after=Predict(equal,0);Check((after.weapondata[0].m_iWeaponState&0x3C)==8&&after.weapondata[0].m_iClip==10&&after.client.vuser4[1]==0&&(after.weapondata[0].m_iWeaponState&2),"strictly expired switch loads available reserve and recomputes side");
 auto back=Predict(State(8,14,35),IN_ATTACK2);back.weapondata[0].m_flNextReload=-.001f;back=Predict(back,0);Check((back.weapondata[0].m_iWeaponState&0x3C)==4&&back.weapondata[0].m_iClip==7&&back.client.vuser4[1]==42,"B toA returns all excess without clipping reserve");
 for(int gate=0;gate<4;++gate){auto locked=State();if(gate==0)locked.client.health=0;else if(gate==1)locked.client.iuser3=0;else if(gate==2)locked.client.iuser3=3;else locked.client.m_flNextAttack=.5f;Check(Predict(locked,IN_ATTACK).weapondata[0].m_iClip==7,"life/round/deploy fire gate");}
 clWeapons=0;const auto soundBefore=soundCount;Check(Predict(State(),IN_ATTACK).weapondata[0].m_iClip==7&&soundCount==soundBefore,"cl_lw0 preserves authoritative ammo without duplicate fire");clWeapons=1;
 auto foreign=State();foreign.client.m_iId=29;local_state_t result{};usercmd_t cmd{};Check(!PredictSkull2(&foreign,&result,&cmd,1,100,123)&&Skull2PredictedAmmo()==-1,"inactive identity clears presentation cache");
 Check(pushes==pops&&randomCalls>=16&&stockCount>20,"trace stack balanced and actual prediction exercised");
}
}
#ifndef SKULL2_CLIENT_FIXTURE_NO_MAIN
int main(){using namespace fixture;Setup();Events();Prediction();csol::client::ShutdownSharedRandom();std::cout<<"SKULL2 actual client "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
#endif
