// SPDX-License-Identifier: GPL-3.0-or-later
// Production player/rules/base health, armor, death and native hookchains.
// Engine transport/studio data are fixtures. This is not a live AMXX VM/server.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/scenario/core/scenario_breakable_guard.h"
#include "player.h"
// Keep the legacy Ham EHANDLE declaration separate from the SDK alias.
namespace ham_probe {
#include "../../external/AMXModX/public/HLTypeConversion.h"
globalvars_t* gpGlobals{};
}
#include <functional>
#include <string>
#include <vector>
using namespace sdk_test;
extern void LinkUserMessages();
namespace {
struct Fixture {
 inline static Fixture* active{};
 Engine& e;
 CHalfLifeMultiplay* rules{};CBasePlayer* victim{};CBasePlayer* attacker{};CBaseEntity* gift{};
 std::vector<entvars_t*> damageBefore,damageAfter,killed,ruleKilled,deathNotice;
 std::vector<CBaseEntity*> permission,deathMessage;
 std::vector<int> deathBytes;int message{},winChecks{},allocationsBefore{};
 bool pluginNull{},retire{};
 static void Animation(IReGameHook_CBasePlayer_SetAnimation*,CBasePlayer*,PLAYER_ANIM){}
 static BOOL Damage(IReGameHook_CBasePlayer_TakeDamage* chain,CBasePlayer* p,entvars_t* inflictor,entvars_t* source,float& amount,int bits){
  auto& f=*active;f.damageBefore.push_back(source);
  if(f.retire){Engine::Remove(p->edict());return chain->callNext(p,inflictor,source,amount,bits);}
  if(f.pluginNull)source=nullptr;
  const auto result=chain->callNext(p,inflictor,source,amount,bits);f.damageAfter.push_back(source);return result;
 }
 static void Killed(IReGameHook_CBasePlayer_Killed* chain,CBasePlayer* p,entvars_t* source,int gib){active->killed.push_back(source);chain->callNext(p,source,gib);}
 static BOOL Permission(IReGameHook_CSGameRules_FPlayerCanTakeDamage* chain,CBasePlayer* p,CBaseEntity* source){active->permission.push_back(source);return chain->callNext(p,source);}
 static void PlayerKilled(IReGameHook_CSGameRules_PlayerKilled* chain,CBasePlayer* p,entvars_t* source,entvars_t* inflictor){active->ruleKilled.push_back(source);chain->callNext(p,source,inflictor);}
 static void DeathNotice(IReGameHook_CSGameRules_DeathNotice* chain,CBasePlayer* p,entvars_t* source,entvars_t* inflictor){active->deathNotice.push_back(source);chain->callNext(p,source,inflictor);}
 static void DeathMessage(IReGameHook_CSGameRules_SendDeathMessage* chain,CBaseEntity* killer,CBasePlayer* p,CBasePlayer* assister,entvars_t* inflictor,const char* name,int flags,int rarity){active->deathMessage.push_back(killer);chain->callNext(killer,p,assister,inflictor,name,flags,rarity);}
 static void Wins(IReGameHook_CSGameRules_CheckWinConditions* chain){++active->winChecks;chain->callNext();}
 CBasePlayer* Player(int slot,TeamName team){
  auto* ed=e.Reserve(slot,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
  ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.max_health=100;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_AIM;
  ed->v.movetype=MOVETYPE_WALK;ed->v.modelindex=1;ed->v.size=Vector(32,32,72);ed->v.netname=Engine::String("fixture");
  p->m_iTeam=team;p->m_iJoiningState=JOINED;p->m_iNumSpawns=1;p->m_iKevlar=ARMOR_VESTHELM;p->m_LastHitGroup=HITGROUP_CHEST;p->m_bNotKilled=true;
  return p;
 }
 explicit Fixture(Engine& engine):e(engine){
  Require(!active,"serial NULL SDK fixtures");active=this;e.Prepare("0");g_pGameRules=InstallGameRules();
  rules=dynamic_cast<CHalfLifeMultiplay*>(g_pGameRules);Require(rules&&!CSOL_RulesRejected()&&!CSOL_UsesScenarioRules(),"actual ordinary rules installed; no changed Models layout instantiated");
  Require(CSOL_ActivateGameRules(),"native rules activate");gmsgCurWeapon=0;LinkUserMessages();
  victim=Player(1,CT);attacker=Player(2,TERRORIST);auto* ed=e.Reserve(e.nextEntity++,"teddybeargiftbox");gift=GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&ed->v));
  g_engfuncs.pfnGetCurrentPlayer=[]{return -1;};
  g_engfuncs.pfnSetView=[](const edict_t*,const edict_t*){};
  g_engfuncs.pfnClientPrintf=[](edict_t*,PRINT_TYPE,const char*){};
  g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{static studiohdr_t model{};model.length=sizeof(model);return &model;};
  g_engfuncs.pfnMessageBegin=[](int,int id,const float*,edict_t*){active->message=id;};
  g_engfuncs.pfnWriteByte=[](int v){if(active->message==gmsgDeathMsg)active->deathBytes.push_back(v);};
  g_engfuncs.pfnMessageEnd=[]{active->message=0;};
  g_ReGameHookchains.m_CBasePlayer_SetAnimation.registerHook(Animation,255);
  g_ReGameHookchains.m_CBasePlayer_TakeDamage.registerHook(Damage,192);
  g_ReGameHookchains.m_CBasePlayer_Killed.registerHook(Killed,192);
  g_ReGameHookchains.m_CSGameRules_FPlayerCanTakeDamage.registerHook(Permission,192);
  g_ReGameHookchains.m_CSGameRules_PlayerKilled.registerHook(PlayerKilled,192);
  g_ReGameHookchains.m_CSGameRules_DeathNotice.registerHook(DeathNotice,192);
  g_ReGameHookchains.m_CSGameRules_SendDeathMessage.registerHook(DeathMessage,192);
  g_ReGameHookchains.m_CSGameRules_CheckWinConditions.registerHook(Wins,192);
  allocationsBefore=e.allocations;
 }
 ~Fixture(){
  g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);
  g_ReGameHookchains.m_CBasePlayer_TakeDamage.unregisterHook(Damage);
  g_ReGameHookchains.m_CBasePlayer_Killed.unregisterHook(Killed);
  g_ReGameHookchains.m_CSGameRules_FPlayerCanTakeDamage.unregisterHook(Permission);
  g_ReGameHookchains.m_CSGameRules_PlayerKilled.unregisterHook(PlayerKilled);
  g_ReGameHookchains.m_CSGameRules_DeathNotice.unregisterHook(DeathNotice);
  g_ReGameHookchains.m_CSGameRules_SendDeathMessage.unregisterHook(DeathMessage);
  g_ReGameHookchains.m_CSGameRules_CheckWinConditions.unregisterHook(Wins);
  e.Clear();active=nullptr;
 }
};
void Run(){
 auto storage=std::make_unique<Engine>();auto& engine=*storage;
 for(bool nullSource:{true,false}){
  Fixture f(engine);auto* source=nullSource?nullptr:f.attacker->pev;f.victim->pev->armorvalue=50;
  const auto health=f.victim->pev->health,armor=f.victim->pev->armorvalue;
  f.victim->TakeDamage(f.gift->pev,source,20,DMG_BURN);
  Check(f.victim->pev->health<health&&f.victim->pev->armorvalue<armor,"real NULL/non-NULL hit applies actual armor and health");
  Check(f.damageBefore==std::vector<entvars_t*>{source}&&f.damageAfter==std::vector<entvars_t*>{source},"native pre/post hooks preserve actual attacker pointer");
  Check(!f.permission.empty()&&f.permission.back()==(nullSource?nullptr:static_cast<CBaseEntity*>(f.attacker)),"actual rule hook receives absent or real attacker");
  Check(f.e.allocations==f.allocationsBefore,"NULL does not allocate a surrogate edict/private object");
 }
 for(bool pluginNull:{false,true}){
  Fixture f(engine);f.pluginNull=pluginNull;f.victim->pev->armorvalue=0;f.victim->pev->frags=7;f.attacker->pev->frags=9;
  auto* initial=pluginNull?f.attacker->pev:nullptr;
  f.victim->TakeDamage(f.gift->pev,initial,200,DMG_BURN);
  Check(f.victim->pev->health<=0&&f.victim->pev->deadflag!=DEAD_NO&&f.victim->m_iDeaths==1,"actual lethal NULL path completes victim death");
  Check(f.killed==std::vector<entvars_t*>{nullptr}&&f.ruleKilled==f.killed&&f.deathNotice==f.killed,"NULL crosses real Killed PlayerKilled DeathNotice hooks");
  Check(f.deathMessage==std::vector<CBaseEntity*>{nullptr}&&!f.deathBytes.empty()&&f.deathBytes.front()==0,"actual SendDeathMessage keeps absent object while wire killer is byte0");
  Check(f.victim->pev->frags==7&&f.attacker->pev->frags==9,"no absent-killer frag debit or unrelated player reward");
  Check(f.winChecks>0&&f.victim->m_hObserverTarget.Get<CBaseEntity>()==nullptr,"real win check runs and NULL observer target remains absent");
  Check(f.e.allocations==f.allocationsBefore,"lethal NULL path creates no surrogate attacker");
 }
 for(bool worldSource:{false,true}){
  Fixture f(engine);f.victim->pev->armorvalue=0;f.victim->pev->frags=7;f.attacker->pev->frags=9;
  auto* world=GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&f.e.edicts[0].v));world->pev->frags=3;
  ham_probe::gpGlobals=gpGlobals;ham_probe::HLTypeConversion ham;ham.init();
  Check(ham.entvars_to_id(nullptr)==-1&&ham.id_to_entvars(-1)==nullptr&&ham.entvars_to_id(world->pev)==0,"actual active Ham conversion preserves NULL=-1 separately from world0");
  auto* source=worldSource?world->pev:f.attacker->pev;
  f.victim->TakeDamage(f.gift->pev,source,200,DMG_BURN);
  Check(f.victim->m_iDeaths==1&&f.killed==std::vector<entvars_t*>{source},"non-NULL player and actual world death preserve original pointer identity");
  Check(f.attacker->pev->frags==(worldSource?9:10)&&world->pev->frags==(worldSource?2:3),"non-NULL player reward and real-world frag debit remain unchanged");
  Check(!f.deathBytes.empty()&&f.deathBytes.front()==(worldSource?0:2),"non-NULL death message preserves player or world wire identity");
 }
 {Fixture f(engine);const EHANDLE target(f.victim);f.retire=true;auto* ed=f.victim->edict();
  Check(!f.victim->TakeDamage(f.gift->pev,nullptr,20,DMG_BURN)&&!target.Get<CBaseEntity>()&&ed->free,"pre-hook entity retirement rejects the old SDK body without stale access");}
}
}
int main(){try{Run();}catch(const std::exception& ex){++failures;std::cerr<<ex.what()<<'\n';}std::cout<<"null_attacker_sdk checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;}
