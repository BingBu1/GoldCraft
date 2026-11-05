// SPDX-License-Identifier: GPL-3.0-or-later
#define SKULL3_CLIENT_EVENTS_NO_MAIN
#include "client_events.cpp"

namespace prediction_fixture {
using namespace csol;using namespace csol::client;
unsigned checks{},failures{},stockCalls{},spreadCalls{};
float clWeapons=1;int crtCalls{};
void Check(bool value,const char* message){++checks;if(!value){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
bool Near(float x,float y){return std::abs(x-y)<.00001f;}
float __cdecl RandomFloat(unsigned seed,float lo,float hi)
{++spreadCalls;Check(seed>=123&&seed<=126&&lo==-.5f&&hi==.5f,"shared native RNG ABI");return float(int(seed)-124)*.125f;}
int __cdecl CRTRandom(){++crtCalls;return 0;}
using Rand=int(__cdecl*)();Rand crt=CRTRandom;
void Setup()
{
 fixture::Setup();singleDefinition.id=142;singleDefinition.name="weapon_skull3";
 dualDefinition.id=143;dualDefinition.name="weapon_skull3d";
 api->GetClientBase=[]()->void*{return reinterpret_cast<void*>(0x10000000);};
 api->ResolveGameSymbol=[](void*,const char* n,mh_gamesymbol_kind_t,void** p)->mh_gamesymbol_status_t{
  if(!std::strcmp(n,"UTIL_SharedRandomFloat"))*p=reinterpret_cast<void*>(&RandomFloat);
  else if(!std::strcmp(n,"CSOL_ClientCRTRand"))*p=&crt;else return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;
  return MH_GAMESYMBOL_OK;};
 InitializeSharedRandom();fixture::vm.curstate.body=9;
 engine.pfnGetCvarFloat=[](const char* n){return !std::strcmp(n,"cl_lw")?clWeapons:1.f;};
 downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t*,int,double,unsigned){++stockCalls;Check(from->client.m_iId==0,"extended id hidden from stock array");*to=*from;to->client.weaponanim=99;to->client.viewmodel=99;to->client.vuser4[1]=99;};
}
local_state_t State(bool dual,int clip=5,int reserve=10)
{
 local_state_t s{};s.client.m_iId=dual?143:142;s.client.health=100;s.client.iuser3=1;
 s.client.flags=FL_ONGROUND;s.client.viewmodel=12;s.client.weaponanim=dual?15:2;s.client.fov=90;
 s.client.punchangle[0]=1.25f;s.client.punchangle[1]=-2.5f;s.client.vuser4[1]=float(reserve);
 auto& w=s.weapondata[ActiveWeaponSlot];w.m_iId=s.client.m_iId;w.m_iClip=clip;w.fuser1=.4f;
 w.m_flNextPrimaryAttack=w.m_flNextSecondaryAttack=w.m_flTimeWeaponIdle=-.001f;
 return s;
}
local_state_t Predict(local_state_t source,int buttons,int run=1)
{
 csol::client::activeId=source.client.m_iId;local_state_t dest{};usercmd_t cmd{};cmd.msec=10;cmd.buttons=short(buttons);
 Check(PredictSkull3(&source,&dest,&cmd,run,100,123),"both ids use one predictor");
 Check(source.client.m_iId==dest.client.m_iId&&dest.client.viewmodel==12,"source identity restored and viewmodel authoritative");return dest;
}
void Ready(local_state_t& s)
{s.client.m_flNextAttack=0;s.weapondata[0].m_flNextPrimaryAttack=s.weapondata[0].m_flNextSecondaryAttack=-.001f;}
void Run()
{
 const auto* single=FindWeaponModule(142);const auto* dualModule=FindWeaponModule(143);
 Check(single&&dualModule&&single!=dualModule,"paired ids have distinct presentation descriptors");
 Check(single->predict==PredictSkull3&&!dualModule->predict&&!dualModule->initialize&&!dualModule->receive,"one prediction and event lifecycle for both ids");
 for(bool dual:{false,true})for(int clip:{1,2,5,35})for(int flags:{0,1,4,5})
 {
  ResetSkull3();auto s=State(dual,clip);s.weapondata[0].m_iWeaponState=flags;
  auto out=Predict(s,IN_ATTACK);const auto& w=out.weapondata[0];
  Check(w.m_iClip==clip-1&&out.client.vuser4[1]==10,"one active magazine round; common reserve unchanged");
  Check(Near(w.m_flNextPrimaryAttack,dual?.06f:.075f)&&Near(w.m_flNextSecondaryAttack,dual?.06f:.075f)&&Near(w.m_flTimeWeaponIdle,1.99f),"original cycle and idle clocks");
  Check(w.m_iWeaponState==((dual?(flags^1):flags)|2),"left toggles only for dual; other flags preserved");
  Check(skull3::UnpackShots(w.fuser3)==1&&Near(w.fuser1,dual?.60526317358f:.48026314377f),"first shot accuracy and count");
  const bool left=dual&&!(flags&1);const int sequence=!dual?3:left?(clip-1<=1?10:8):13;
  Check(fixture::anim==sequence&&fixture::body==2,"exact original dual right boolean branch and fire body");
  Check(out.client.punchangle[0]==1.25f&&out.client.punchangle[1]==-2.5f&&crtCalls==0,"client no predicted KickBack");
  fixture::anim=-1;const int oldSound=fixture::calls;auto replay=Predict(s,IN_ATTACK,0);
  Check(replay.weapondata[0].m_iClip==clip-1&&fixture::anim==-1&&fixture::calls==oldSound,"replay simulates state without effects");
 }
 for(bool dual:{false,true})
 {
  ResetSkull3();auto s=State(dual,2,3);s.weapondata[0].m_iWeaponState=5;s.weapondata[0].fuser3=skull3::PackShots(7);s.client.fov=64;
  const int before=fixture::calls;auto out=Predict(s,IN_RELOAD);
  Check(out.weapondata[0].m_fInReload&&out.weapondata[0].m_iClip==2&&out.client.vuser4[1]==3,"reload starts without early ammo move");
  Check(fixture::anim==(dual?14:1)&&Near(out.client.m_flNextAttack,dual?3.29f:2.29f)&&Near(out.weapondata[0].m_flTimeWeaponIdle,dual?3.79f:2.79f),"original reload sequence and clocks");
  Check(out.weapondata[0].fuser1==0&&skull3::UnpackShots(out.weapondata[0].fuser3)==0&&out.weapondata[0].m_iWeaponState==5,"reload accuracy/shots reset without guessed mode-bit mutation");
  Check(out.client.fov==(dual?64:90)&&fixture::calls==before,"only A reload unzooms; prediction adds no sound");
  out.client.m_flNextAttack=0;out=Predict(out,0);Check(out.weapondata[0].m_iClip==5&&out.client.vuser4[1]==0&&!out.weapondata[0].m_fInReload,"reload completion consumes available common reserve");
  for(int clip:{0,1,2}){s=State(dual,clip,0);auto idle=Predict(s,0);Check(idle.client.weaponanim==(dual?(clip?(clip==1?7:6):15):0),"idle empty/last-round branch");Check(Near(idle.weapondata[0].m_flTimeWeaponIdle,dual?(clip?59.99f:-.001f):19.99f),"original idle duration or no reschedule");}
  s=State(dual);const int sounds=fixture::calls;auto zoom=Predict(s,IN_ATTACK2);Check(zoom.client.fov==(dual?90:64)&&fixture::calls==sounds,"A zooms silently; D secondary is masked");
  Ready(zoom);zoom=Predict(zoom,IN_ATTACK2);Check(zoom.client.fov==90,"A zoom toggles back");
  for(int gate=0;gate<4;++gate){s=State(dual);if(gate==0)s.client.health=0;else if(gate==1)s.client.iuser3=0;else if(gate==2)s.client.iuser3=3;else s.client.m_flNextAttack=.5f;Check(Predict(s,IN_ATTACK).weapondata[0].m_iClip==5,"life/round/deploy gate");}
  clWeapons=0;s=State(dual);Check(Predict(s,IN_ATTACK).weapondata[0].m_iClip==5,"cl_lw0 authoritative clip");clWeapons=1;
  for(int count:{0,1,11,15,16,20})
  {
   s=State(dual);s.weapondata[0].fuser3=skull3::PackShots(count);s.weapondata[0].fuser1=.923f;
   s.weapondata[0].fuser2=.3f;s.weapondata[0].m_iWeaponState=skull3::DelayedFire|skull3::Direction;
   auto released=Predict(s,dual?IN_ATTACK2:0);
   Check(skull3::UnpackShots(released.weapondata[0].fuser3)==(count<16?count+4:count),"original client release count differs from server timed decay");
   Check(released.weapondata[0].fuser1==.923f&&!(released.weapondata[0].m_iWeaponState&skull3::DelayedFire),"release keeps accuracy and clears delayFire");
   Check(released.weapondata[0].m_iWeaponState&skull3::Direction,"release preserves direction flag");
   auto again=Predict(released,0);Check(again.weapondata[0].fuser3==released.weapondata[0].fuser3,"release count is not added every idle frame");
  }
  s=State(dual,0,3);s.weapondata[0].m_flNextPrimaryAttack=0;
  Check(!Predict(s,0).weapondata[0].m_fInReload,"auto reload excludes exact next-primary deadline");
  s=State(dual,0,3);Check(Predict(s,dual?IN_ATTACK2:0).weapondata[0].m_fInReload,"auto reload occurs after deadline and masks D secondary");
  s=State(dual);auto both=Predict(s,IN_ATTACK|IN_ATTACK2);
  Check(both.weapondata[0].m_iClip==(dual?4:5)&&both.client.fov==(dual?90:64),"supported secondary priority only for single mode");
 }
 auto ordinary=State(false);ordinary.client.m_iId=29;local_state_t out{};usercmd_t cmd{};
 Check(!PredictSkull3(&ordinary,&out,&cmd,1,100,123)&&Skull3PredictedAmmo()==-1,"inactive predictor leaves native weapon to downstream");
}
}
int main(){using namespace prediction_fixture;Setup();Run();std::cout<<"{\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"stockCalls\":"<<stockCalls<<",\"sharedRngCalls\":"<<spreadCalls<<"}\n";return failures?1:0;}
