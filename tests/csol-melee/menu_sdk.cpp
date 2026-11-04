// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK/menu/service/inventory objects. The scenario buy phase alone is a
// controlled gate; this does not simulate or accept the whole map startup.
#include "../csol-scenario-monster-skills/trap_fixture.hpp"
#include "../../native/csol/server/buy_menu.h"
#include "../../native/csol/server/weapon_service.h"
#include "../../native/csol/server/weapon_slots.h"
#include "../../native/csol/server/weapons.h"
#include "../../native/csol/server/wpn_melee.h"
#include "../../native/csol/buy_menu.hpp"
#include "../../native/csol/melee_identity.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

bool fixtureBuyPhase=true;
bool CSOL_TestBuyReady() { return fixtureBuyPhase; }
std::shared_ptr<CCSOLScenarioRuntime> CSOL_TestScenarioRuntimeForAccess(const CSOLScenarioEntityAccess* access)
{ return CSOL_ScenarioRuntimeForAccess(access); }
namespace {
using namespace sdk_test;
using namespace csol::buy;
int assertions{},failed{},fault{},adds{},removes{},deploys{},msgId{};
CBasePlayerWeapon* original{};
std::vector<int> debits;
std::vector<std::string> args;
std::vector<std::vector<std::uint8_t>> packets;
std::function<void()> transition;
void Expect(bool good,const char* label){++assertions;if(!good){++failed;std::cerr<<"ASSERT "<<label<<'\n';}}
void Animation(IReGameHook_CBasePlayer_SetAnimation*,CBasePlayer*,PLAYER_ANIM) {}
void Account(IReGameHook_CBasePlayer_AddAccount* chain,CBasePlayer* p,int amount,RewardType reason,bool track)
{debits.push_back(amount);chain->callNext(p,amount,reason,track);}
BOOL Add(IReGameHook_CBasePlayer_AddPlayerItem* chain,CBasePlayer* p,CBasePlayerItem* item){
 ++adds;const auto ok=chain->callNext(p,item);
 if(item!=original&&transition){auto cb=std::move(transition);cb();}
 return item!=original&&fault==2?FALSE:ok;
}
BOOL Remove(IReGameHook_CBasePlayer_RemovePlayerItem* chain,CBasePlayer* p,CBasePlayerItem* item){
 ++removes;const auto ok=chain->callNext(p,item);return item==original&&fault==3?FALSE:ok;
}
BOOL Deploy(IReGameHook_CBasePlayerWeapon_DefaultDeploy* chain,CBasePlayerWeapon* w,char* view,char* player,int sequence,char* extension,int skip){
 ++deploys;if(w!=original&&fault==1)return FALSE;return chain->callNext(w,view,player,sequence,extension,skip);
}
void Hooks(bool on){
 if(on){
  g_ReGameHookchains.m_CBasePlayer_SetAnimation.registerHook(Animation,192);
  g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(Account,192);
  g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.registerHook(Add,192);
  g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.registerHook(Remove,192);
  g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.registerHook(Deploy,192);
 }else{
  g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);
  g_ReGameHookchains.m_CBasePlayer_AddAccount.unregisterHook(Account);
  g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.unregisterHook(Add);
  g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.unregisterHook(Remove);
  g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.unregisterHook(Deploy);
 }
}
std::string Read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);Require(bool(f),"frozen test resource exists");std::ostringstream text;text<<f.rdbuf();return text.str();}
void Command(CBasePlayer& p,std::initializer_list<std::string> command){
 args.assign(command);Require(CSOL_BuyMenuCommand(p.edict(),args[0].c_str()),"real menu consumes command");
}
Packet Last(){Packet p;Require(!packets.empty()&&Decode(packets.back(),p),"real response decodes");return p;}
Packet Offer(int id){for(auto it=packets.rbegin();it!=packets.rend();++it){Packet p;if(Decode(*it,p)&&p.kind==Kind::Item&&p.id==id)return p;}throw std::runtime_error("menu omitted requested offer");}
CBasePlayerWeapon* Held(CBasePlayer& p,int slot){return dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[slot]);}
void Sole(CBasePlayer& p,int id){auto* w=Held(p,KNIFE_SLOT);Expect(w&&w->m_iId==id&&!w->m_pNext&&w->m_pPlayer==&p&&w->pev->owner==p.edict()&&p.m_pActiveItem==w,"actual purchase selects exactly one owned knife");}
struct Fixture {
 std::unique_ptr<Engine> storage=std::make_unique<Engine>();Engine& engine=*storage;
 std::shared_ptr<CCSOLScenarioRuntime> runtime;
 std::shared_ptr<CCSOLScenarioSession> session;
 std::shared_ptr<CCSOLScenarioPlayerRuntime> players;
 explicit Fixture(std::function<void(std::map<std::string,std::string>&)> configure){
  engine.files=trap_test::Resources(0);configure(engine.files);engine.Prepare("2");LinkUserMessages();
  g_pGameRules=InstallGameRules();Require(g_pGameRules&&!CSOL_RulesRejected(),"actual scenario rules registered");
  {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());Require(scope.Current()&&runtime,"actual registered runtime");}
  session=runtime->session;Require(session->Precache("map_session_test",1,CSOLScenarioResourceScope::Selection),"actual selection resources");
  for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int id:{1,2,30}){
   if(std::strcmp(name,"zbstrigger")&&id!=1)continue;
   auto* ed=Engine::Create(Engine::String(name));auto* base=CBaseEntity::Instance(ed);
   if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(base)){p->m_iId=id;p->m_iTeam=2;}
   if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(base)){p->m_iId=id;p->m_iTeam=1;p->m_iMaxSpawn=5;}
   if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(base)){p->m_iId=id;ed->v.model=Engine::String("*1");}
   Require(engine.api.pfnSpawn(ed)==0,"actual map marker dispatch");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
  }
  Require(session->Activate(),"actual selection session activation");
  players=runtime->players=std::make_shared<CCSOLScenarioPlayerRuntime>(session);Require(players->Activate(),"actual player authority activation");
  g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return nullptr;};
 }
 CBasePlayer& Player(){auto* ed=engine.Reserve(1,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.deadflag=DEAD_NO;p->m_iTeam=CT;p->m_iNumSpawns=1;p->m_bJustConnected=false;p->m_iJoiningState=JOINED;p->has_disconnected=false;
  CSOLScenarioPlayerProfile profile;profile.equipment.classModsComplete=true;Require(players->SetProfile(*p,std::move(profile)),"actual player authority identity");return *p;}
 ~Fixture(){engine.Clear();players.reset();session.reset();runtime.reset();}
};
void Run(const std::filesystem::path& frozen,int mode){
 const auto menu=Read(frozen/"menu.json");
 Fixture f([&](auto& files){
  files["resource/item.csv"]+="483,DragonSword,dragonsword,Equipment,11,4016\r\n363,SkullAxe,skullaxe,Equipment,11,4010\r\n";
  files[PresentationResource]=menu;
  for(const auto& path:Resources(ParsePresentation(menu)))if(path!=PresentationResource)files[path]="fixture-menu-resource";
  for(const char* name:{"dragonsword","skullaxe"})files[std::string("csol/weapons/")+name+"_damage.json"]=Read(frozen/(std::string(name)+"_damage.json"));
 });
 auto& e=f.engine;auto& p=f.Player();
 g_engfuncs.pfnInfoKeyValue=[](char*,const char* name)->char*{return const_cast<char*>(!std::strcmp(name,"csol_protocol")?"1":"");};
 g_engfuncs.pfnCanSkipPlayer=[](const edict_t*){return 0;};
 g_engfuncs.pfnRandomLong=[](int32 low,int32)->int32{return low;};
 g_engfuncs.pfnCmd_Argc=[](){return int(args.size());};
 g_engfuncs.pfnCmd_Argv=[](int i){return args.at(i).c_str();};
 g_engfuncs.pfnMessageBegin=[](int,int id,const float*,edict_t*){msgId=id;if(id==Engine::self->messages.at(Message))packets.emplace_back();};
 g_engfuncs.pfnWriteByte=[](int value){if(msgId==Engine::self->messages.at(Message))packets.back().push_back(std::uint8_t(value));};
 g_engfuncs.pfnMessageEnd=[](){msgId=0;};
 // Init creates messages before menu registration; install a real entry first.
 e.messages[Message]=230;
 CSOL_InitWeapons();e.Set("csol_weapons","1");CSOL_ResetWeaponRegistry();CSOL_PrecacheWeapons();
 Require(CSOL_WeaponsReady()&&CSOL_MeleeReady(1022)&&CSOL_MeleeReady(1023),"actual two-knife config/registry ready");
 Require(CSOL_PrecacheBuyMenu(),"actual menu resource validation and registration");
 auto own=[&](const char* name,int slot){auto* ed=Engine::Create(Engine::String(name));auto* w=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));Require(w,"actual initial weapon factory");w->CBasePlayerWeapon::Spawn();w->m_pPlayer=&p;w->pev->owner=p.edict();p.m_rgpPlayerItems[slot]=w;return w;};
 original=own("weapon_knife",KNIFE_SLOT);auto* primary=own("weapon_m4a1",PRIMARY_WEAPON_SLOT);auto* pistol=own("weapon_usp",PISTOL_SLOT);
 p.m_pActiveItem=original;p.m_iAccount=mode?7500:0;p.m_flNextAttack=17;
 original->m_flNextPrimaryAttack=18;original->m_flNextSecondaryAttack=19;original->m_flTimeWeaponIdle=20;
 p.m_signals.Signal(SIGNAL_BUY);p.m_signals.Update();
 Require(p.CanPlayerBuy(false),"real native buyzone gate accepts eligible player in controlled phase");
 Hooks(true);fault=mode;
 Command(p,{"csol_buy_menu","1"});
 Expect(Offer(1022).result==Result::Available&&Offer(1023).result==Result::Available&&Offer(1022).price==0&&Offer(1023).price==0,"actual Available and Price expose both free knives");
 if(mode==4)transition=[&]{const auto saved=args;Command(p,{"csol_buy","1","1023"});args=saved;};
 if(mode==5)transition=[&]{p.pev->deadflag=DEAD_DEAD;p.pev->health=0;};
 if(mode==6)transition=[&]{++p.m_iNumSpawns;};
 if(mode==7)transition=[&]{f.runtime->RequestClose();};
 Command(p,{"csol_buy","1","1022"});
 if(mode==0||mode==4){
  Expect(Last().result==Result::Success&&Last().price==0&&p.m_iAccount==(mode?7500:0),"zero-priced menu delivery succeeds without balance change");Sole(p,1022);
  auto* sword=Held(p,KNIFE_SLOT);const auto attempts=adds;const auto moneyCalls=debits.size();
  Command(p,{"csol_buy","1","1022"});Expect(adds==attempts&&debits.size()==moneyCalls&&Held(p,KNIFE_SLOT)==sword,"repeated receipt never repeats entity or money transaction");
  sword->m_flNextPrimaryAttack=91;sword->m_flTimeWeaponIdle=92;
  Command(p,{"csol_buy_menu","2"});Expect(Offer(1022).result==Result::Unavailable&&Offer(1023).result==Result::Available,"current knife unavailable and other knife available");
  Command(p,{"csol_buy","2","1022"});Expect(Last().result==Result::Unavailable&&Held(p,KNIFE_SLOT)==sword&&sword->m_flNextPrimaryAttack==91&&sword->m_flTimeWeaponIdle==92,"same-kind new request does not reset cooldown");
  Command(p,{"csol_buy","2","1023"});Expect(Last().result==Result::Success,"failed same-kind attempt does not lock different knife purchase");Sole(p,1023);
  Command(p,{"csol_buy_menu","3"});Command(p,{"csol_buy","3","1022"});Expect(Last().result==Result::Success,"opposite knife can be selected again through fresh catalog");Sole(p,1022);
 }else{
  Expect(Last().kind!=Kind::Result||Last().result!=Result::Success,"native hook failure or lifecycle transition cannot publish success");
  Expect(p.m_iAccount==7500,"failed free delivery cannot charge/refund extra funds");
  if(mode<=3)Expect(Held(p,KNIFE_SLOT)==original&&!original->m_pNext&&p.m_pActiveItem==original&&original->m_flNextPrimaryAttack==18&&original->m_flNextSecondaryAttack==19&&original->m_flTimeWeaponIdle==20&&p.m_flNextAttack==17,"rollback restores original knife identity and all captured timers");
 }
 if(mode!=7)Expect(Held(p,PRIMARY_WEAPON_SLOT)==primary&&Held(p,PISTOL_SLOT)==pistol,"knife purchase preserves both firearm slots");
 Hooks(false);transition={};
}
}
int main(int argc,char** argv){try{Require(argc==2,"one case argument");Run(std::filesystem::path(argv[0]).parent_path()/"resources",std::stoi(argv[1]));}catch(const std::exception& error){++failed;std::cerr<<"EXCEPTION "<<error.what()<<'\n';Hooks(false);}
 std::cout<<"melee-menu-sdk assertions="<<assertions<<" failures="<<failed<<" SDK checks="<<sdk_test::checks<<" SDK failures="<<sdk_test::failures<<'\n';return failed||sdk_test::failures?1:0;}
