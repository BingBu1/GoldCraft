// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK Rules/Session/player authority/inventory and purchase hookchains.
// The engine world, account profile and media existence are explicit fixtures.
// No production bodies, readiness overrides or private rebuy state are copied.
#include "../csol-oberon-chain/resources.hpp"
#include "../../native/csol/server/scenario/core/scenario_gameplay.h"
#include "../../native/csol/server/scenario/monsters/scenario_monster_services.h"
#include "../../native/csol/server/scenario/monsters/scenario_zombie_bomb.h"
#include "../../native/csol/scenario/core/scenario_respawn_resources.hpp"
#include "../../native/csol/scenario/core/wall_upgrade.hpp"
#include "../../native/csol/scenario/bosses/boss_health.hpp"
#include "../../native/csol/scenario/monsters/monster_appearances.hpp"
#include "../../native/csol/scenario_part_effects.hpp"
#include "../../native/csol/scenario/monsters/frozen_death.hpp"
#include "../../native/csol/scenario/monsters/frozen_smoke.hpp"
#include "../../native/csol/server/buy_menu.h"
#include "../../native/csol/server/weapons/weapons.h"
#include "../../native/csol/server/weapons/weapon_service.h"
#include "../../native/csol/server/weapons/weapon_slots.h"
#include "../../native/csol/server/weapons/wpn_melee.h"
#include "../../native/csol/server/weapons/wpn_cannon.h"
#include "../../native/csol/server/weapons/wpn_k3.h"
#include "../../native/csol/server/weapons/wpn_skull3.h"
#include "../../native/csol/server/weapons/melee_purchase.h"
#include "../../native/csol/server/weapons/knife_selection.h"
#include "../../native/csol/buy_menu.hpp"
#include "../../native/csol/weapons/skull3.hpp"
#include "client.h"
#include "hostage/hostage.h"
#include "pm_math.h"
#include <filesystem>
#include <fstream>
#include <sstream>

extern bool g_bClientPrintEnable;

namespace rebuy_test {
using sdk_test::Require;
using sdk_test::Engine;
using namespace csol::buy;
int behaviorChecks{};
void Check(bool good,const char* label){++behaviorChecks;sdk_test::Check(good,label);}
std::string Read(const std::filesystem::path& path){
 std::ifstream input(path,std::ios::binary);Require(bool(input),"recorded fixture resource exists");
 std::ostringstream result;result<<input.rdbuf();return result.str();
}
std::string Resource(const std::filesystem::path& root,const char* relative){
 auto path=root/relative;if(!std::filesystem::exists(path))path=root/std::filesystem::path(relative).filename();
 return Read(path);
}
bool Equal(const RebuyStruct& a,const RebuyStruct& b){
 return a.m_primaryWeapon==b.m_primaryWeapon&&a.m_primaryAmmo==b.m_primaryAmmo&&
  a.m_secondaryWeapon==b.m_secondaryWeapon&&a.m_secondaryAmmo==b.m_secondaryAmmo&&
  a.m_heGrenade==b.m_heGrenade&&a.m_flashbang==b.m_flashbang&&a.m_smokeGrenade==b.m_smokeGrenade&&
  a.m_defuser==b.m_defuser&&a.m_nightVision==b.m_nightVision&&a.m_armor==b.m_armor;
}

struct Fixture {
 inline static Fixture* active{};
 std::unique_ptr<Engine> storage=std::make_unique<Engine>();Engine& engine=*storage;
 std::shared_ptr<CCSOLScenarioRuntime> runtime;
 CBasePlayer* player{};
 std::vector<std::string> args,events;
 std::vector<std::vector<std::uint8_t>> packets;
 std::vector<int> debits;
 std::vector<int> addedIds,removedIds;
 std::function<void()> transition;
 std::function<void(CBasePlayer&)> intermediate;
 int message{},userId=71,adds{},deploys{},removes{},gunAmmoCalls{},rejectItem{},rejectDeployId{};
 bool hooks{},observe{},rejectWeapon{},rejectAmmo{},insideMessage{};
 static void Animation(IReGameHook_CBasePlayer_SetAnimation*,CBasePlayer*,PLAYER_ANIM){}
 static CBaseEntity* Weapon(IReGameHook_BuyWeaponByWeaponID* chain,CBasePlayer* p,WeaponIdType id){
  auto& f=*active;f.events.push_back("weapon:"+std::to_string(id));
  if(f.transition){auto callback=std::move(f.transition);callback();}
  return f.rejectWeapon?nullptr:chain->callNext(p,id);
 }
 static bool Ammo(IReGameHook_BuyGunAmmo* chain,CBasePlayer* p,CBasePlayerItem* w,bool blink){
  auto& f=*active;++f.gunAmmoCalls;
  f.events.push_back("ammo:"+std::to_string(w->iItemSlot()));
  return f.rejectAmmo?false:chain->callNext(p,w,blink);
 }
 static void Item(IReGameHook_BuyItem* chain,CBasePlayer* p,int slot){
  auto& f=*active;f.events.push_back("item:"+std::to_string(slot));
  if(f.rejectItem==slot)return;
  chain->callNext(p,slot);
 }
 static void Account(IReGameHook_CBasePlayer_AddAccount* chain,CBasePlayer* p,int amount,RewardType reason,bool track){
  auto& f=*active;f.debits.push_back(amount);if(!amount)f.events.push_back("knife");
  chain->callNext(p,amount,reason,track);
  if(f.intermediate)f.intermediate(*p);
 }
 static BOOL Add(IReGameHook_CBasePlayer_AddPlayerItem* chain,CBasePlayer* p,CBasePlayerItem* w){
  ++active->adds;active->addedIds.push_back(w->m_iId);const auto result=chain->callNext(p,w);
  if(active->intermediate)active->intermediate(*p);return result;
 }
 static BOOL Remove(IReGameHook_CBasePlayer_RemovePlayerItem* chain,CBasePlayer* p,CBasePlayerItem* w){
  ++active->removes;active->removedIds.push_back(w->m_iId);return chain->callNext(p,w);
 }
 static BOOL Deploy(IReGameHook_CBasePlayerWeapon_DefaultDeploy* chain,CBasePlayerWeapon* w,
  char* view,char* model,int seq,char* extension,int skip){
  ++active->deploys;if(w->m_iId==active->rejectDeployId)return FALSE;
  return chain->callNext(w,view,model,seq,extension,skip);
 }
 void Hooks(){
  g_ReGameHookchains.m_CBasePlayer_SetAnimation.registerHook(Animation,192);
  g_ReGameHookchains.m_BuyWeaponByWeaponID.registerHook(Weapon,192);
  g_ReGameHookchains.m_BuyGunAmmo.registerHook(Ammo,192);
  g_ReGameHookchains.m_BuyItem.registerHook(Item,192);
  g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(Account,192);
  g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.registerHook(Add,192);
  g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.registerHook(Remove,192);
  g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.registerHook(Deploy,192);hooks=true;
 }
 explicit Fixture(const std::filesystem::path& root){
  Require(!active,"one actual rebuy SDK fixture per process");active=this;
  engine.files=oberon_chain::BaseResources();
  // Required loader fallback, authored with zero champion schedules. The
  // actual parser validates all 23 columns; no readiness state is overridden.
  engine.files["maps/ZBS_Champ_ZT_Default.cso"]=difficulty_test::Encrypt(
   ";Difficulty,spawnstarttime,interval,spawnendtime,maxnum,zombitype,appearance,attackDamage,attackRange,attackInterval,health,armor,speed,bonusMoney,priority,skill,color,Killtype,EventClass,propensity,clearpoint,dropItem,exp\r\n");
  // Authored non-map profile capacity; no production eligibility predicate is changed.
  std::string levels="level,experience,unused,health,damage,speed,armor,ammo\r\n";
  for(int i=0;i<=100;++i)levels+=std::to_string(i)+",100,0,1,1,1,1,1000\r\n";
  engine.files["resource/ZBSVER2/ZBS_neededexp.cso"]=difficulty_test::Encrypt(levels);
  engine.files["resource/item.csv"]+="483,DragonSword,dragonsword,Equipment,11,4016\r\n363,SkullAxe,skullaxe,Equipment,11,4010\r\n525,k3,k3,Equipment,5,197\r\n";
  auto menu=root/"menu.json";
  const auto menuText=std::filesystem::exists(menu)?Read(menu):Resource(root,PresentationResource);
  engine.files[PresentationResource]=menuText;
  for(const auto& path:Resources(ParsePresentation(menuText)))
   if(path!=PresentationResource)engine.files[path]="controlled-menu-existence";
  for(const char* name:{"dragonsword","skullaxe","cannon","skull3","k3"}){
   const std::string path="csol/weapons/"+std::string(name)+"_damage.json";
   engine.files[path]=Resource(root,path.c_str());
  }
  for(const auto& image:csol::scenario_respawn_ui::Outcomes)engine.files[image.path]="controlled-image";
  for(const auto& image:csol::scenario_respawn_ui::RetryImages)engine.files[image.path]="controlled-image";
  for(const auto* path:{csol::scenario_respawn_ui::CenterBackground.path,csol::scenario_respawn_ui::ItemSprite,
   csol::scenario_respawn_ui::LocalizationPath})engine.files[path]="controlled-image";
  for(const char* sound:csol::scenario_respawn_ui::RetrySounds)engine.files[sound]="controlled-sound";
  for(const char* path:csol::part_effects::Sprites)engine.files[path]="controlled-sprite";
  for(const char* path:{CSOLZombieBombModel,csol::monster_effects::ZombieBombSprite,"models/grenade.mdl",
   csol::frozen_death::IceModel,csol::frozen_smoke::SmokeSprite,CSOLZombieBombEvent})engine.files[path]="controlled-model";
  for(const char* sound:CSOLZombieBombBounceSounds)engine.files["sound/"+std::string(sound)]="controlled-sound";
  engine.files["resource/Hud/ZBS/zbslevelupwallbg.tga"]="controlled-image";
  engine.files["resource/csol/wall/v1/WallUpgradeScheme.res"]="controlled-scheme";
  // Exact existence closure read from the current Combat Session precacher.
  // Decode/selection remains production code; no unknown-file fallback.
  for(const char* path:{csol::wall_upgrade::Model,csol::wall_upgrade::Sprite})engine.files[path]="controlled-wall-upgrade-model";
  for(const char* path:csol::boss_health::Resources)engine.files[path]="controlled-boss-HUD";
  for(const char* path:csol::boss_health::AbilityResources)engine.files[path]="controlled-boss-ability-HUD";
  const auto appearanceResources=[&](const csol::scenario::MonsterType& type){
   const auto* appearance=csol::scenario::FindMonsterAppearance(type.appearance);
   Require(appearance,"authored fixture monster appearance resolves through actual catalog");
   engine.files[std::string(appearance->model)]="controlled-monster-studio";
   if(!appearance->deathSound.empty())engine.files["sound/"+std::string(appearance->deathSound)]="controlled-monster-death-sound";
   if(!appearance->ambientSound.empty())engine.files["sound/"+std::string(appearance->ambientSound)]="controlled-monster-ambient-sound";
   if(appearance->name=="zombiedog")engine.files["sound/zombi/zombiedog_howls.wav"]="controlled-monster-sound";
   if(appearance->healthSummary)if(const char* image=csol::boss_health::LeftImage(appearance->index))engine.files[image]="controlled-boss-left-HUD";
  };
  csol::scenario::MonsterTypeTable authoredTypes;csol::scenario::ConfigError resourceError;
  Require(csol::scenario::ReadMonsterTypeResource(difficulty_test::Bytes(engine.files.at("maps/ZBS_ZT_zs_deadend.cso")),authoredTypes,resourceError),
   "actual parser accepts fixture monster table before existence closure");
  for(const auto& type:authoredTypes.rows)appearanceResources(type);
  csol::scenario::ChampionTable champions;csol::scenario::ResourceError championError;std::string championPath;
  const bool championsLoaded=csol::scenario::LoadChampions("zs_deadend",{Engine::Load,Engine::Free},champions,championPath,championError);
  if(!championsLoaded)std::cerr<<"champion resource "<<championError.path<<": "<<championError.detail.message<<'\n';
  Require(championsLoaded,"actual required default champion loader resolves authored fixture closure");
  for(const auto& champion:champions.rows)appearanceResources(champion.type);
  engine.globals.maxClients=4;engine.nextEntity=5;engine.globals.time=1000;
  engine.globals.mapname=Engine::String("zs_deadend");
  engine.Set("csol_scenario_difficulty","1");engine.Set("sv_maxvelocity","10000");engine.Prepare("2");
  g_psv_maxvelocity=g_engfuncs.pfnCVarGetPointer("sv_maxvelocity");
  g_engfuncs.pfnFreeFile=[](void* value){if(value)Engine::Free(value);};
  g_engfuncs.pfnCanSkipPlayer=[](const edict_t*){return 0;};
  // Third-party studio decoding stays outside this purchase fixture. A
  // nullptr prevents opportunistic sequence lookup without inventing labels.
  g_engfuncs.pfnMakeVectors=[](const float* angles){AngleVectors(angles,gpGlobals->v_forward,gpGlobals->v_right,gpGlobals->v_up);};
  engine.onTrace=[](edict_t*,TraceResult&){};
  // Explicit authored horizontal floor for native GiveNamedItem item Spawn.
  // Preserve the engine's no-floor/start-solid outcomes outside its 256-unit
  // descent; this is controlled world geometry, not a purchase/readiness shim.
  g_engfuncs.pfnDropToFloor=[](edict_t* ed){
   Require(ed&&!ed->free,"drop-to-floor requires live fixture edict");
   const float bottom=ed->v.origin.z+ed->v.mins.z;
   if(!std::isfinite(bottom)||bottom<0)return -1;
   if(bottom>256)return 0;
   Vector origin=ed->v.origin;origin.z=-ed->v.mins.z;
   g_engfuncs.pfnSetOrigin(ed,origin);ed->v.flags|=FL_ONGROUND;ed->v.groundentity=&Engine::self->edicts[0];return 1;
  };
  g_engfuncs.pfnGetModelPtr=[](edict_t*)->void*{return nullptr;};
  g_engfuncs.pfnGetPlayerUserId=[](edict_t*){return active->userId;};
  g_engfuncs.pfnInfoKeyValue=[](char*,const char* name)->char*{
   return const_cast<char*>(!std::strcmp(name,"csol_protocol")?"1":"");};
  g_engfuncs.pfnCmd_Argc=[](){return int(active->args.size());};
  g_engfuncs.pfnCmd_Argv=[](int i){return active->args.at(i).c_str();};
  g_engfuncs.pfnMessageBegin=[](int,int id,const float*,edict_t*){
   Require(!active->insideMessage,"real engine messages cannot nest");active->insideMessage=true;active->message=id;
   if(active->engine.messages.contains(Message)&&id==active->engine.messages.at(Message))active->packets.emplace_back();};
  g_engfuncs.pfnWriteByte=[](int value){
   if(active->engine.messages.contains(Message)&&active->message==active->engine.messages.at(Message))
    active->packets.back().push_back(std::uint8_t(value));};
  g_engfuncs.pfnMessageEnd=[](){Require(active->insideMessage,"real message closes once");active->insideMessage=false;active->message=0;};
  gmsgCurWeapon=0;LinkUserMessages();
  // Real Regamedll_Game_Init initializes these mutable native tables before
  // any weapon precache or purchase. Use its actual reset consumer.
  WeaponInfoReset();
  Require(GetWeaponInfo(WEAPON_AWP)&&GetWeaponInfo(WEAPON_M4A1),"real native weapon information initialized");
  CSOL_InitWeapons();engine.Set("csol_weapons","1");CSOL_ResetWeaponRegistry();CSOL_PrecacheWeapons();
  for(const char* name:{"weapon_knife","weapon_m4a1","weapon_usp","weapon_hegrenade","weapon_flashbang","weapon_smokegrenade"})UTIL_PrecacheOtherWeapon(name);
  Require(CSOL_WeaponsReady()&&CSOL_MeleeReady(1022)&&CSOL_MeleeReady(1023)&&CSOL_CannonReady()&&CSOL_Skull3Ready()&&CSOL_K3Ready(),
   "actual extension configs and weapon factories ready");
  g_pGameRules=InstallGameRules();CSOL_PrecacheGameRules();
  if(CSOL_RulesRejected())for(const auto& line:engine.events)std::cerr<<line<<'\n';
  Require(g_pGameRules&&!CSOL_RulesRejected(),"actual combat rules precache");
  {CSOLScenarioSdkScope scope;runtime=CSOL_ScenarioRuntimeForAccess(scope.EntityAccess().get());
   Require(scope.Current()&&runtime&&runtime->gameplay&&runtime->waveEvents,"actual combat service graph installed");}
  for(const char* name:{"playerspawn","zombiespawn","zbstrigger"})for(int id:{1,2,30}){
   if(std::strcmp(name,"zbstrigger")&&id!=1)continue;
   auto* ed=Engine::Create(Engine::String(name));auto* entity=CBaseEntity::Instance(ed);
   if(auto* p=dynamic_cast<CCSOLPlayerSpawn*>(entity)){p->m_iId=id;p->m_iTeam=CT;}
   if(auto* p=dynamic_cast<CCSOLMonsterSpawn*>(entity)){p->m_iId=id;p->m_iTeam=TERRORIST;p->m_iMaxSpawn=5;}
   if(auto* p=dynamic_cast<CCSOLScenarioTrigger*>(entity)){p->m_iId=id;ed->v.model=Engine::String("*1");}
   Require(engine.api.pfnSpawn(ed)==0,"actual map marker dispatch");ed->v.mins=Vector(-8,-8,-8);ed->v.maxs=Vector(8,8,8);
  }
  Require(CSOL_ActivateGameRules(),"actual full combat rules activation");
  auto* ed=engine.Reserve(1,"player");player=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
  ed->v.flags=FL_CLIENT;ed->v.health=100;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_YES;
  player->m_iTeam=CT;player->m_iNumSpawns=1;player->m_bJustConnected=false;player->m_iJoiningState=JOINED;player->has_disconnected=false;
  Profile();player->m_iAccount=16000;player->m_iAutoWepSwitch=0;
  player->m_signals.Signal(SIGNAL_BUY);player->m_signals.Update();
  Require(CSOL_PrecacheBuyMenu()&&CSOL_ScenarioCanBuy()&&player->CanPlayerBuy(false),"unmodified production buy eligibility accepts real combat player");
  // Admit the fresh connection before test-owned snapshot seeding. The host
  // adapter deliberately clears an old native record on its first command.
  Require(Command({"cl_rebuy"}),"empty fresh-connection replay initializes actual identity slot");
  Hooks();events.clear();packets.clear();
 }
 void Profile(){CSOLScenarioPlayerProfile profile;profile.equipment.classModsComplete=true;
  Require(runtime->players->SetProfile(*player,std::move(profile)),"actual current player authority registered");}
 CBasePlayerWeapon* Own(const char* name,int slot,int id){
  auto* ed=Engine::Create(Engine::String(name));auto* w=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));
  Require(w,"actual owned weapon factory");w->CBasePlayerWeapon::Spawn();w->m_iId=static_cast<WeaponIdType>(id);
  ItemInfo info{};Require(w->GetItemInfo(&info),"actual weapon metadata");CBasePlayerItem::m_ItemInfoArray[id]=info;
  w->m_iPrimaryAmmoType=info.pszAmmo1?CBasePlayer::GetAmmoIndex(info.pszAmmo1):-1;
  w->m_pPlayer=player;w->pev->owner=player->edict();player->m_rgpPlayerItems[slot]=w;
  return w;
 }
 int& Ammo(CBasePlayerWeapon& w){Require(w.m_iPrimaryAmmoType>=0&&w.m_iPrimaryAmmoType<MAX_AMMO_SLOTS,"real ammo registry index");return player->m_rgAmmo[w.m_iPrimaryAmmoType];}
 int& Grenade(const char* name){const int i=CBasePlayer::GetAmmoIndex(name);Require(i>=0&&i<MAX_AMMO_SLOTS,"real grenade ammo index");return player->m_rgAmmo[i];}
 bool Command(std::initializer_list<std::string> command){args.assign(command);return CSOL_BuyMenuCommand(player->edict(),args.front().c_str());}
 void Replay(const char* name="cl_rebuy"){const auto count=packets.size();Require(Command({name}),"real replay command consumed");
  Check(packets.size()==count,"replay emits no catalog or purchase responses");}
 void Buy(int token,int id){Require(Command({"csol_buy_menu",std::to_string(token)}),"actual catalog command");
  Require(Command({"csol_buy",std::to_string(token),std::to_string(id)}),"actual admitted ordinary purchase");}
 Packet Last(){Packet result;Require(!packets.empty()&&Decode(packets.back(),result),"real CSOBuy result decodes");return result;}
 ~Fixture(){
  if(hooks){
   g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);
   g_ReGameHookchains.m_BuyWeaponByWeaponID.unregisterHook(Weapon);
   g_ReGameHookchains.m_BuyGunAmmo.unregisterHook(Ammo);g_ReGameHookchains.m_BuyItem.unregisterHook(Item);
   g_ReGameHookchains.m_CBasePlayer_AddAccount.unregisterHook(Account);
   g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.unregisterHook(Add);
   g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.unregisterHook(Remove);
   g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.unregisterHook(Deploy);
  }
  try{engine.Clear();}catch(const std::exception& error){sdk_test::Check(false,error.what());}
  runtime.reset();active=nullptr;
 }
};

void Snapshot(Fixture& f){
 auto& p=*f.player;auto* primary=f.Own("weapon_m4a1",PRIMARY_WEAPON_SLOT,WEAPON_M4A1);
 auto* secondary=f.Own("weapon_usp",PISTOL_SLOT,WEAPON_USP);
 f.Ammo(*primary)=67;f.Ammo(*secondary)=23;primary->m_iClip=7;secondary->m_iClip=4;
 f.Grenade("HEGrenade")=1;f.Grenade("Flashbang")=2;f.Grenade("SmokeGrenade")=1;
 p.m_bHasDefuser=true;p.m_bHasNightVision=true;p.m_iKevlar=ARMOR_VESTHELM;p.pev->armorvalue=3;
 p.BuildRebuyStruct();const auto saved=p.m_rebuyStruct;
 Check(saved.m_primaryWeapon==WEAPON_M4A1&&saved.m_primaryAmmo==67&&saved.m_secondaryWeapon==WEAPON_USP&&saved.m_secondaryAmmo==23,
  "native Build entry captures actual inventory reserve rather than clip or purchase history");
 Check(saved.m_heGrenade==1&&saved.m_flashbang==2&&saved.m_smokeGrenade==1&&saved.m_defuser==1&&saved.m_nightVision==1&&saved.m_armor==ARMOR_VESTHELM,
  "snapshot captures grenade quantities and equipment flags/armor type");
 f.Ammo(*primary)=65535;f.Ammo(*secondary)=32768;f.Grenade("Flashbang")=65534;p.BuildRebuyStruct();
 Check(p.m_rebuyStruct.m_primaryAmmo==-1&&p.m_rebuyStruct.m_secondaryAmmo==-32768&&p.m_rebuyStruct.m_flashbang==-2,
  "native int storage preserves original signed16 snapshot conversion");
 p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=nullptr;p.m_rgpPlayerItems[PISTOL_SLOT]=nullptr;p.BuildRebuyStruct();
 Check(!p.m_rebuyStruct.m_primaryWeapon&&!p.m_rebuyStruct.m_primaryAmmo&&!p.m_rebuyStruct.m_secondaryWeapon&&!p.m_rebuyStruct.m_secondaryAmmo,
  "absent slot clears both snapshot identity and ammo");
 p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=primary;p.m_rgpPlayerItems[PISTOL_SLOT]=secondary;primary->pev->owner=nullptr;p.BuildRebuyStruct();
 Check(!p.m_rebuyStruct.m_primaryWeapon,"borrowed weapon owner cannot enter rebuy snapshot");primary->pev->owner=p.edict();
 p.m_rebuyStruct=saved;p.m_bIsInRebuy=true;f.Ammo(*primary)=9;p.BuildRebuyStruct();
 Check(Equal(p.m_rebuyStruct,saved),"native rebuy guard suppresses capture");p.m_bIsInRebuy=false;
}

void FailedCapture(Fixture& f){
 auto& p=*f.player;auto* primary=f.Own("weapon_m4a1",PRIMARY_WEAPON_SLOT,WEAPON_M4A1);f.Ammo(*primary)=41;
 p.m_rebuyStruct={};p.m_iAccount=0;f.Buy(1,WEAPON_AWP);
 Check(f.Last().result==Result::Insufficient&&p.m_iAccount==0,"ordinary insufficient purchase fails without debit");
 Check(p.m_rebuyStruct.m_primaryWeapon==WEAPON_M4A1&&p.m_rebuyStruct.m_primaryAmmo==41,
  "admitted failed purchase common tail still snapshots actual existing inventory");
 f.Ammo(*primary)=17;p.BuildRebuyStruct();
 Check(p.m_rebuyStruct.m_primaryAmmo==17,"native Build consumer updates same authoritative snapshot");
 const auto saved=p.m_rebuyStruct;f.Ammo(*primary)=9;p.m_iAccount=16000;int suppressed{};
 f.intermediate=[&](CBasePlayer& value){value.BuildRebuyStruct();if(Equal(value.m_rebuyStruct,saved))++suppressed;};
 f.Buy(2,1022);f.intermediate={};
 Check(f.Last().result==Result::Success&&suppressed>0,"real internal knife delivery reenters Build without overwriting snapshot");
 Check(p.m_rebuyStruct.m_primaryWeapon==WEAPON_M4A1&&p.m_rebuyStruct.m_primaryAmmo==9,
  "ordinary successful transaction captures after guarded delivery returns");
}

void Ordinary(Fixture& f){
 auto& p=*f.player;const auto before=p.m_iAccount;f.Buy(1,WEAPON_M4A1);
 Check(f.Last().result==Result::Success&&p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]&&
  p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]->m_iId==WEAPON_M4A1&&p.m_iAccount==before-GetWeaponInfo(WEAPON_M4A1)->cost,
  "ordinary native firearm retains actual entity delivery and item debit");
 const int adds=f.adds;const auto debits=f.debits.size(),packets=f.packets.size();
 f.Command({"csol_buy","1",std::to_string(WEAPON_M4A1)});
 Check(f.adds==adds&&f.debits.size()==debits&&f.packets.size()==packets+1&&f.Last().result==Result::Success,
  "ordinary duplicate receipt responds without repeating actual inventory or money effects");
 const auto responses=f.packets.size();f.Command({"csol_buy","65535",std::to_string(WEAPON_AWP)});
 Check(f.packets.size()==responses&&f.debits.size()==debits,"ordinary wrong-token purchase cannot settle or reply");
 auto* gun=dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);Require(gun,"real firearm retained");
 f.Ammo(*gun)=0;f.Buy(2,2001);
 Check(f.Last().result==Result::Success&&f.Ammo(*gun)>0&&p.m_rebuyStruct.m_primaryAmmo==f.Ammo(*gun),
  "ordinary primary ammo helper buys through native ammo and captures final reserve");
 f.Buy(3,1002);Check(f.Last().result==Result::Success&&p.m_iKevlar==ARMOR_VESTHELM,
  "ordinary equipment helper retains actual armor purchase and common tail");
 f.Buy(4,1004);Check(f.Last().result==Result::Success&&f.Grenade("Flashbang")==1&&p.m_rebuyStruct.m_flashbang==1,
  "ordinary grenade helper retains native quantity and snapshot update");
 f.Buy(5,1022);Check(f.Last().result==Result::Success&&p.m_rgpPlayerItems[KNIFE_SLOT]&&
  p.m_rgpPlayerItems[KNIFE_SLOT]->m_iId==1022,"ordinary extension helper still delivers real free knife");
 p.m_iAccount=0;f.Buy(6,WEAPON_AWP);Check(f.Last().result==Result::Insufficient&&
  p.m_rebuyStruct.m_primaryWeapon==WEAPON_M4A1,"ordinary insufficient purchase preserves real held snapshot");
}

std::vector<std::string> CollapsedAmmo(const std::vector<std::string>& input){
 std::vector<std::string> result;for(const auto& event:input)
  if(result.empty()||event.rfind("ammo:",0)!=0||result.back()!=event)result.push_back(event);return result;
}
void Order(Fixture& f){
 auto& p=*f.player;auto* primary=f.Own("weapon_m4a1",PRIMARY_WEAPON_SLOT,WEAPON_M4A1);
 auto* secondary=f.Own("weapon_usp",PISTOL_SLOT,WEAPON_USP);
 f.Ammo(*primary)=1;f.Ammo(*secondary)=1;p.m_bHasDefuser=false;p.m_bHasNightVision=false;
 p.m_iKevlar=ARMOR_NONE;p.pev->armorvalue=0;
 f.Grenade("HEGrenade")=f.Grenade("Flashbang")=f.Grenade("SmokeGrenade")=0;
 p.m_rebuyStruct={WEAPON_M4A1,2,WEAPON_USP,2,1,2,1,1,1,ARMOR_VESTHELM};
 CSOL_RebuyMeleeSelected(p,1022);const auto saved=p.m_rebuyStruct;
 f.rejectWeapon=true;f.rejectItem=MENU_SLOT_ITEM_FLASHGREN;
 int suppressed{};f.intermediate=[&](CBasePlayer& value){value.BuildRebuyStruct();if(Equal(value.m_rebuyStruct,saved))++suppressed;};
 f.events.clear();const bool printing=g_bClientPrintEnable;f.Replay("rebuy_in");f.intermediate={};
 const std::vector<std::string> expected{"weapon:22","ammo:1","item:7","item:2","item:4","item:3","item:3","item:5","weapon:16","ammo:2","item:6","knife"};
 if(CollapsedAmmo(f.events)!=expected){for(const auto& event:f.events)std::cerr<<"ORDER "<<event<<'\n';}
 Check(CollapsedAmmo(f.events)==expected,"original fixed primary/ammo/defuser/armor/HE/flash/smoke/secondary/ammo/NVG/knife order");
 Check(std::count(f.events.begin(),f.events.end(),"item:3")==2,"fixed grenade difference attempts continue even when each flash purchase fails");
 Check(p.m_rgpPlayerItems[KNIFE_SLOT]&&p.m_rgpPlayerItems[KNIFE_SLOT]->m_iId==1022,"normal weapon/grenade failures do not block later free knife");
 Check(f.Ammo(*primary)>2&&f.Ammo(*secondary)>2,"native ammo purchase fills real capacity instead of writing saved count");
 Check(Equal(p.m_rebuyStruct,saved)&&suppressed>0,"intermediate real purchase callbacks cannot replace replay snapshot");
 Check(g_bClientPrintEnable==printing,"replay restores prior native message-print state");
}

void Insufficient(Fixture& f){
 auto& p=*f.player;p.m_rebuyStruct={WEAPON_AWP,0,WEAPON_DEAGLE,0,1,2,1,1,1,ARMOR_VESTHELM};
 p.m_iAccount=0;CSOL_RebuyMeleeSelected(p,1022);const auto before=p.m_iAccount;const auto packets=f.packets.size();p.Rebuy();
 Check(f.packets.size()==packets,"native player Rebuy diverts to synchronous scenario replay without catalog/results");
 Check(p.m_iAccount==before&&f.debits==std::vector<int>{0},"each insufficient paid item fails independently while free knife debits zero");
 Check(p.m_rgpPlayerItems[KNIFE_SLOT]&&p.m_rgpPlayerItems[KNIFE_SLOT]->m_iId==1022,
  "insufficient primary/secondary/armor/grenades still reaches free knife");
}

void AmmoArmor(Fixture& f){
 auto& p=*f.player;auto* gun=f.Own("weapon_m4a1",PRIMARY_WEAPON_SLOT,WEAPON_M4A1);
 f.Ammo(*gun)=20;gun->m_iClip=1;p.m_rebuyStruct={};p.m_rebuyStruct.m_primaryAmmo=20;
 p.m_iKevlar=ARMOR_VESTHELM;p.pev->armorvalue=1;p.m_rebuyStruct.m_armor=ARMOR_VESTHELM;
 f.events.clear();f.Replay();
 Check(f.gunAmmoCalls==0&&f.events.empty(),"equal reserve and equal armor type skip refill despite depleted clip and armor health");
 p.m_iKevlar=ARMOR_KEVLAR;p.pev->armorvalue=100;f.events.clear();f.Replay();
 Check(std::find(f.events.begin(),f.events.end(),"item:2")!=f.events.end()&&p.m_iKevlar==ARMOR_VESTHELM,
  "helmet upgrade compares armor type even at full armor health");
 p.m_rebuyStruct={};f.Grenade("Flashbang")=1;p.m_rebuyStruct.m_flashbang=3;f.rejectItem=MENU_SLOT_ITEM_FLASHGREN;
 f.events.clear();f.Replay();Check(std::count(f.events.begin(),f.events.end(),"item:3")==2,
  "grenade replay uses positive saved-minus-current count without recomputing after failure");
 p.m_rebuyStruct.m_flashbang=0;f.events.clear();f.Replay();Check(f.events.empty(),"nonpositive grenade difference does not attempt purchases");
}

void Specials(Fixture& f){
 auto& p=*f.player;auto* cannon=f.Own("weapon_cannon",PRIMARY_WEAPON_SLOT,CSOL_WEAPON_CANNON);
 auto* knife=f.Own("weapon_tknifeex2",PISTOL_SLOT,CSOL_WEAPON_TKNIFEEX2);
 cannon->m_iClip=3;knife->m_iClip=1;f.Ammo(*cannon)=7;f.Ammo(*knife)=11;p.BuildRebuyStruct();
 Check(p.m_rebuyStruct.m_primaryAmmo==7&&p.m_rebuyStruct.m_secondaryAmmo==11,
  "original cannon and tknife v21C false snapshots reserve, independently of host clip-ammo service");
 p.m_rebuyStruct.m_primaryWeapon=p.m_rebuyStruct.m_secondaryWeapon=0;
 const auto reserve=f.Ammo(*cannon);const auto cannonClip=cannon->m_iClip;
 f.rejectAmmo=true;f.events.clear();f.Replay();
 Check(f.gunAmmoCalls>0&&std::find(f.events.begin(),f.events.end(),"ammo:2")!=f.events.end()&&
  std::find(f.events.begin(),f.events.end(),"ammo:1")==f.events.end(),
  "tknife v218 forces a secondary ammo attempt but cannon does not force primary ammo");
 Check(f.Ammo(*cannon)==reserve&&cannon->m_iClip==cannonClip,"rebuy does not strongly write cannon clip or reserve");
 auto* dual=f.Own(csol::skull3::DualClassname,PRIMARY_WEAPON_SLOT,csol::skull3::DualId);
 f.Ammo(*dual)=83;dual->m_iClip=6;p.BuildRebuyStruct();
 Check(p.m_rebuyStruct.m_primaryWeapon==csol::skull3::Id&&p.m_rebuyStruct.m_primaryAmmo==83,
  "held SKULL3D snapshots paired purchase identity142 and actual shared reserve");
}

std::vector<CBasePlayerWeapon*> Inventory(CBasePlayer& player,int slot){
 std::vector<CBasePlayerWeapon*> result;
 for(auto* item=player.m_rgpPlayerItems[slot];item;item=item->m_pNext){
  auto* gun=dynamic_cast<CBasePlayerWeapon*>(item);
  Require(gun&&result.size()<MAX_WEAPONS&&std::find(result.begin(),result.end(),gun)==result.end(),
   "observed real inventory chain is bounded and acyclic");
  result.push_back(gun);
 }
 return result;
}

void OwnedKnife(Fixture& f){
 f.Buy(65000,1022);
 Require(f.Last().result==Result::Success&&f.player->m_rgpPlayerItems[KNIFE_SLOT]&&
  f.player->m_rgpPlayerItems[KNIFE_SLOT]->m_iId==1022,"real initial knife purchase supplies the original replay tail");
}

std::size_t Charges(const Fixture& f){
 return std::count_if(f.debits.begin(),f.debits.end(),[](int amount){return amount<0;});
}

void OwnedGun(Fixture& f,const std::string& kind){
 // Original MP1140D853..D959 rejects the actual ID in every non-knife chain
 // before its balance/debit path. Original rebuy still issues that purchase.
 auto& p=*f.player;OwnedKnife(f);
 const int id=kind=="cannon"?CSOL_WEAPON_CANNON:kind=="k3"?CSOL_WEAPON_K3:CSOL_WEAPON_TKNIFEEX2;
 const int slot=kind=="tknife"?PISTOL_SLOT:PRIMARY_WEAPON_SLOT;
 f.Buy(1,id);Require(f.Last().result==Result::Success,"initial real extended gun purchase succeeds");
 auto guns=Inventory(p,slot);Require(guns.size()==1&&guns.front()->m_iId==id,"real purchase owns exact extended gun");
 auto* gun=guns.front();if(p.m_pActiveItem!=gun)Require(p.SwitchWeapon(gun),"real owned gun becomes active");
 ItemInfo info{};Require(gun->GetItemInfo(&info)&&info.iMaxAmmo1>0,"real gun capacity available");
 gun->m_iClip=kind=="tknife"?1:3;f.Ammo(*gun)=kind=="tknife"?info.iMaxAmmo1:7;
 gun->m_flNextPrimaryAttack=1031;gun->m_flNextSecondaryAttack=1032;gun->m_flTimeWeaponIdle=1033;
 gun->m_iShotsFired=9;p.m_flNextAttack=1034;p.BuildRebuyStruct();
 const auto saved=p.m_rebuyStruct;const EHANDLE identity(gun);
 const std::array<float,4> timers{gun->m_flNextPrimaryAttack,gun->m_flNextSecondaryAttack,gun->m_flTimeWeaponIdle,p.m_flNextAttack};
 const int clip=gun->m_iClip,reserve=f.Ammo(*gun),account=p.m_iAccount;
 const int allocations=f.engine.allocations,adds=f.adds,removes=f.removes,deploys=f.deploys,ammoCalls=f.gunAmmoCalls;
 const auto debits=Charges(f);const float now=gpGlobals->time;
 f.Replay();f.Replay("rebuy_in");f.Replay();
 Check(gpGlobals->time==now&&identity.Get<CBaseEntity>()==gun&&Inventory(p,slot)==guns&&p.m_pActiveItem==gun,
  "three same-time F2 commands retain the exact actual owned gun and inventory chain");
 Check(f.engine.allocations==allocations&&f.adds==adds&&f.removes==removes&&f.deploys==deploys,
  "owned firearm replay does not allocate, Add, Remove or Deploy replacement entities");
 Check(p.m_iAccount==account&&Charges(f)==debits,"owned firearm replay never charges its purchase price");
 Check(gun->m_iClip==clip&&f.Ammo(*gun)==reserve&&gun->m_iShotsFired==9&&
  timers==std::array<float,4>{gun->m_flNextPrimaryAttack,gun->m_flNextSecondaryAttack,gun->m_flTimeWeaponIdle,p.m_flNextAttack},
  "owned firearm replay preserves clip, reserve, shots and actual cooldowns");
 Check(Equal(p.m_rebuyStruct,saved),"owned firearm failures retain the native replay snapshot");
 if(kind=="tknife")Check(f.gunAmmoCalls==ammoCalls+3,"owned tknife retains each real forced ammo attempt at full reserve");
 f.Buy(2,id);Check(f.Last().result==Result::Failed&&f.Last().request==2&&p.m_iAccount==account,
  "new normal-menu token rejects an already owned extended gun rather than replaying an old success receipt");
 p.m_iAccount=0;f.Buy(3,id);
 Check(f.Last().result==Result::Failed&&f.Last().balance==0&&p.m_iAccount==0,
  "actual ownership failure precedes insufficient funds at zero balance");
 Check(identity.Get<CBaseEntity>()==gun&&Inventory(p,slot)==guns&&gun->m_iClip==clip&&
  f.engine.allocations==allocations&&f.adds==adds&&f.removes==removes&&f.deploys==deploys&&Charges(f)==debits,
  "ordinary same-gun refusals also preserve actual weapon identity and avoid delivery callbacks");
}

void OwnedSkull3(Fixture& f){
 auto& p=*f.player;OwnedKnife(f);f.Buy(1,csol::skull3::Id);
 Require(f.Last().result==Result::Success,"real shop purchase creates SKULL3 pair");
 const auto guns=Inventory(p,PRIMARY_WEAPON_SLOT);CBasePlayerWeapon *single{},*dual{};
 for(auto* gun:guns){if(gun->m_iId==csol::skull3::Id)single=gun;if(gun->m_iId==csol::skull3::DualId)dual=gun;}
 Require(guns.size()==2&&single&&dual&&CSOL_IsSkull3(*single)&&CSOL_IsSkull3(*dual),"actual shop grants both concrete SKULL3 entity types");
 Require(p.SwitchWeapon(dual)&&p.m_pActiveItem==dual,"actual SwitchWeapon selects SKULL3D");
 single->m_iClip=7;dual->m_iClip=13;f.Ammo(*single)=83;p.m_flNextAttack=1040;
 single->m_flNextPrimaryAttack=1041;single->m_flNextSecondaryAttack=1042;single->m_flTimeWeaponIdle=1043;
 dual->m_flNextPrimaryAttack=1044;dual->m_flNextSecondaryAttack=1045;dual->m_flTimeWeaponIdle=1046;
 p.BuildRebuyStruct();Require(p.m_rebuyStruct.m_primaryWeapon==csol::skull3::Id,"real paired snapshot retains purchase ID while D is active");
 const auto saved=p.m_rebuyStruct;const EHANDLE singleIdentity(single),dualIdentity(dual);
 const auto singleState=CSOL_SaveSkull3PurchaseState(*single),dualState=CSOL_SaveSkull3PurchaseState(*dual);
 const std::array<float,7> timers{p.m_flNextAttack,single->m_flNextPrimaryAttack,single->m_flNextSecondaryAttack,single->m_flTimeWeaponIdle,
  dual->m_flNextPrimaryAttack,dual->m_flNextSecondaryAttack,dual->m_flTimeWeaponIdle};
 const int allocations=f.engine.allocations,adds=f.adds,removes=f.removes,deploys=f.deploys,account=p.m_iAccount;
 const auto debits=Charges(f);const float now=gpGlobals->time;
 f.Replay();f.Replay("rebuy_in");f.Replay();
 Check(gpGlobals->time==now&&singleIdentity.Get<CBaseEntity>()==single&&dualIdentity.Get<CBaseEntity>()==dual&&
  p.m_pActiveItem==dual&&Inventory(p,PRIMARY_WEAPON_SLOT)==guns,
  "same-time F2 with active SKULL3D retains its actual linked A and D entities");
 Check(single->m_iClip==7&&dual->m_iClip==13&&f.Ammo(*single)==83&&
  timers==std::array<float,7>{p.m_flNextAttack,single->m_flNextPrimaryAttack,single->m_flNextSecondaryAttack,single->m_flTimeWeaponIdle,
   dual->m_flNextPrimaryAttack,dual->m_flNextSecondaryAttack,dual->m_flTimeWeaponIdle}&&
  CSOL_SaveSkull3PurchaseState(*single).flags==singleState.flags&&CSOL_SaveSkull3PurchaseState(*dual).flags==dualState.flags,
  "owned SKULL3 replay preserves both clips, shared reserve, mode flags and cooldowns");
 Check(p.m_iAccount==account&&Charges(f)==debits&&f.engine.allocations==allocations&&
  f.adds==adds&&f.removes==removes&&f.deploys==deploys,"owned SKULL3 pair is neither charged nor rebuilt");
 Check(Equal(p.m_rebuyStruct,saved),"paired owned-gun refusals do not replace native snapshot");
 f.Buy(2,csol::skull3::Id);Check(f.Last().result==Result::Failed&&p.m_iAccount==account,
  "normal new-token SKULL3 purchase finds retained A while D is active");
 p.m_iAccount=0;f.Buy(3,csol::skull3::Id);Check(f.Last().result==Result::Failed&&p.m_iAccount==0,
  "SKULL3 ownership refusal also precedes zero-balance failure");
 Check(singleIdentity.Get<CBaseEntity>()==single&&dualIdentity.Get<CBaseEntity>()==dual&&
  Inventory(p,PRIMARY_WEAPON_SLOT)==guns&&f.engine.allocations==allocations&&Charges(f)==debits,
  "normal duplicate SKULL3 commands retain both actual purchased entities");
}

void OwnedChain(Fixture& f){
 auto& p=*f.player;f.Buy(1,CSOL_WEAPON_CANNON);Require(f.Last().result==Result::Success,"real initial cannon purchase for chain test");
 auto* cannon=dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);Require(cannon,"real cannon exists");
 auto* head=dynamic_cast<CBasePlayerWeapon*>(p.GiveNamedItem("weapon_m4a1"));
 Require(head&&p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]==head&&head->m_pNext==cannon&&
  head->pev->owner==p.edict()&&cannon->pev->owner==p.edict(),"native GiveNamedItem/AddPlayerItem links M4 before the actual owned cannon");
 const auto guns=Inventory(p,PRIMARY_WEAPON_SLOT);const EHANDLE identity(cannon);const int account=p.m_iAccount;
 const int allocations=f.engine.allocations,adds=f.adds,removes=f.removes,deploys=f.deploys;const auto debits=f.debits.size();
 cannon->m_iClip=3;cannon->m_flNextPrimaryAttack=1050;
 f.Buy(2,CSOL_WEAPON_CANNON);
 Check(f.Last().result==Result::Failed&&p.m_iAccount==account,"ordinary purchase scans beyond a different inventory head for matching actual ID");
 p.m_iAccount=0;f.Buy(3,CSOL_WEAPON_CANNON);
 Check(f.Last().result==Result::Failed&&p.m_iAccount==0,"non-head ownership takes precedence over insufficient funds");
 Check(identity.Get<CBaseEntity>()==cannon&&Inventory(p,PRIMARY_WEAPON_SLOT)==guns&&cannon->m_iClip==3&&cannon->m_flNextPrimaryAttack==1050&&
  f.engine.allocations==allocations&&f.adds==adds&&f.removes==removes&&f.deploys==deploys&&f.debits.size()==debits,
  "non-head refusal cannot debit, replace inventory or reset held cannon state");
}

void OwnedContinue(Fixture& f){
 auto& p=*f.player;OwnedKnife(f);f.Buy(1,CSOL_WEAPON_K3);Require(f.Last().result==Result::Success,"real K3 purchase for ammo/equipment continuation");
 auto* gun=dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]);Require(gun,"actual owned K3 exists");
 f.Ammo(*gun)=40;p.m_iKevlar=ARMOR_VESTHELM;p.pev->armorvalue=100;f.Grenade("Flashbang")=1;p.BuildRebuyStruct();
 const auto saved=p.m_rebuyStruct;f.Ammo(*gun)=0;p.m_iKevlar=ARMOR_NONE;p.pev->armorvalue=0;f.Grenade("Flashbang")=0;
 gun->m_iClip=3;gun->m_flNextPrimaryAttack=1060;gun->m_flNextSecondaryAttack=1061;gun->m_flTimeWeaponIdle=1062;p.m_flNextAttack=1063;
 const EHANDLE identity(gun);const int account=p.m_iAccount;
 const auto added=f.addedIds.size(),removed=f.removedIds.size();
 int ammoPrice{},ammoAmount{};Require(CSOL_AmmoPurchase(CSOL_WEAPON_K3,ammoPrice,ammoAmount)&&ammoPrice>0&&ammoAmount>0,"actual K3 ammo pricing available");
 const int capacity=gun->iMaxAmmo1();Require(capacity>=40,"real K3 capacity admits saved reserve");
 const int expectedCost=((capacity+ammoAmount-1)/ammoAmount)*ammoPrice+Find(1002)->extendedPrice+Find(1004)->extendedPrice;
 f.events.clear();f.debits.clear();const float now=gpGlobals->time;f.Replay();
 Check(identity.Get<CBaseEntity>()==gun&&p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]==gun&&
  std::find(f.addedIds.begin()+added,f.addedIds.end(),CSOL_WEAPON_K3)==f.addedIds.end()&&
  std::find(f.removedIds.begin()+removed,f.removedIds.end(),CSOL_WEAPON_K3)==f.removedIds.end(),
  "owned-gun failure continues purchases without rebuilding the existing K3");
 Check(f.Ammo(*gun)==capacity&&p.m_iKevlar==ARMOR_VESTHELM&&f.Grenade("Flashbang")==1&&
  CollapsedAmmo(f.events)==std::vector<std::string>{"ammo:1","item:2","item:3","knife"},
  "same-gun refusal still reaches real native ammo refill, armor and grenade in original order");
 Check(p.m_iAccount==account-expectedCost&&std::count(f.debits.begin(),f.debits.end(),-Find(CSOL_WEAPON_K3)->extendedPrice)==0,
  "continuation charges exactly actual ammo boxes and equipment with no gun purchase debit");
 const int settled=p.m_iAccount;f.Replay("rebuy_in");f.Replay();
 Check(gpGlobals->time==now&&p.m_iAccount==settled&&Equal(p.m_rebuyStruct,saved),
  "further same-time F2 commands retain snapshot and do not charge already restored ammo/equipment");
 Check(gun->m_iClip==3&&gun->m_flNextPrimaryAttack==1060&&gun->m_flNextSecondaryAttack==1061&&
  gun->m_flTimeWeaponIdle==1062&&p.m_flNextAttack==1063,
  "ammo/equipment continuation never resets held firearm clip or attack cooldowns");
}

void Knife(Fixture& f){
 auto& p=*f.player;f.Buy(1,1022);Require(f.Last().result==Result::Success,"real first sword purchase succeeds");
 auto* sword=dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[KNIFE_SLOT]);Require(sword,"real sword entity owned");
 const auto service=f.runtime->session->Weapons();const auto life=f.runtime->players->Find(EHANDLE(&p));
 Require(service&&life,"real metadata owner and player generation");
 Require(service->ReplaceSelectionMetadata(p,life->generation,{{4016,81}},{{483,{31,32}}}),"trusted selection metadata update");
 p.m_pActiveItem=sword;p.m_pLastItem=nullptr;p.m_flNextAttack=77;sword->m_flNextPrimaryAttack=78;
 sword->m_flNextSecondaryAttack=79;sword->m_flTimeWeaponIdle=80;sword->m_iShotsFired=9;
 const auto freeze=g_pGameRules->m_bFreezePeriod;g_pGameRules->m_bFreezePeriod=FALSE;p.m_bCanShoot=true;sword->PrimaryAttack();
 g_pGameRules->m_bFreezePeriod=freeze;
 CSOLMeleePurchaseState before{};Require(CSOL_SaveMeleePurchaseState(*sword,before),"actual pending melee state available");
 weapon_data_t packedBefore{};CSOL_PackMeleeWeapon(sword,&packedBefore);
 const EHANDLE identity(sword);const int allocations=f.engine.allocations,adds=f.adds,deploys=f.deploys,removes=f.removes;
 Check(before.prediction.pendingAnimation!=0&&sword->m_pfnThink&&sword->pev->nextthink>gpGlobals->time,
  "same-knife regression begins an actual delayed attack before replay");
 const auto think=sword->m_pfnThink;const float nextThink=sword->pev->nextthink;
 const std::array<float,4> timers{sword->m_flNextPrimaryAttack,sword->m_flNextSecondaryAttack,sword->m_flTimeWeaponIdle,p.m_flNextAttack};
 const auto saved=p.m_rebuyStruct;f.Replay();
 CSOLMeleePurchaseState after{};Require(CSOL_SaveMeleePurchaseState(*sword,after),"actual retained melee state available");
 weapon_data_t packedAfter{};CSOL_PackMeleeWeapon(sword,&packedAfter);
 Check(identity.Get<CBaseEntity>()==sword&&p.m_rgpPlayerItems[KNIFE_SLOT]==sword&&p.m_pActiveItem==sword,
  "same-knife replay retains exact owned active entity");
 Check(f.engine.allocations==allocations&&f.adds==adds&&f.deploys==deploys&&f.removes==removes,
  "same-knife replay does not Spawn/Add/Remove/Deploy");
 Check(timers==std::array<float,4>{sword->m_flNextPrimaryAttack,sword->m_flNextSecondaryAttack,sword->m_flTimeWeaponIdle,p.m_flNextAttack}&&
  sword->m_pfnThink==think&&sword->pev->nextthink==nextThink&&before.prediction.serial==after.prediction.serial&&
  before.prediction.pendingAnimation==after.prediction.pendingAnimation&&before.prediction.pendingTime==after.prediction.pendingTime&&
  packedBefore.m_iWeaponState==packedAfter.m_iWeaponState&&packedBefore.fuser1==packedAfter.fuser1&&packedBefore.fuser2==packedAfter.fuser2,
  "same-knife replay preserves actual pending attack, prediction, Think and cooldowns");
 Check(service->PaintType(*sword)==81&&service->Parts(*sword)==std::array<std::uint32_t,2>{31,32},
  "same-knife replay executes real common metadata tail instead of no-op success");
 Check(Equal(p.m_rebuyStruct,saved),"knife common tail does not overwrite guarded full snapshot");
 // The successful independent setter remains live while Build is suppressed.
 p.m_bIsInRebuy=true;CSOL_RebuyMeleeSelected(p,1023);p.BuildRebuyStruct();p.m_bIsInRebuy=false;
 f.Replay();Check(p.m_rgpPlayerItems[KNIFE_SLOT]&&p.m_rgpPlayerItems[KNIFE_SLOT]->m_iId==1023,
  "independent knife selection updates replay record through full-snapshot native guard");
}

void Paint(Fixture& f){
 auto& p=*f.player;f.Buy(1,1022);Require(f.Last().result==Result::Success,"real initial sword grant for paint-tail regression");
 auto* sword=dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[KNIFE_SLOT]);Require(sword,"actual initial owned sword");
 auto service=f.runtime->session->Weapons();const auto life=f.runtime->players->Find(EHANDLE(&p));
 Require(service&&life,"actual paint authority and current player generation");
 Require(service->ReplaceSelectionMetadata(p,life->generation,{{4000,55},{4016,81},{4010,82}},{{483,{11,12}},{363,{21,22}}}),
  "three distinct trusted directory paint values");
 Require(service->RefreshMeleeSelection(p,life->generation,*sword),"actual initial parts and paint binding");
 const auto parts=service->Parts(*sword);p.m_pActiveItem=sword;p.m_pLastItem=nullptr;
 const auto freeze=g_pGameRules->m_bFreezePeriod;g_pGameRules->m_bFreezePeriod=FALSE;p.m_bCanShoot=true;sword->PrimaryAttack();
 g_pGameRules->m_bFreezePeriod=freeze;
 CSOLMeleePurchaseState before{};Require(CSOL_SaveMeleePurchaseState(*sword,before),"actual melee state before paint-only tail");
 Check(before.prediction.pendingAnimation!=0&&sword->m_pfnThink&&sword->pev->nextthink>gpGlobals->time,
  "paint-only regression starts a real delayed attack");
 const EHANDLE identity(sword);const auto think=sword->m_pfnThink;const auto nextThink=sword->pev->nextthink;
 const auto timers=std::array<float,4>{sword->m_flNextPrimaryAttack,sword->m_flNextSecondaryAttack,sword->m_flTimeWeaponIdle,p.m_flNextAttack};
 const auto preserved=[&](){
  CSOLMeleePurchaseState after{};return CSOL_SaveMeleePurchaseState(*sword,after)&&identity.Get<CBaseEntity>()==sword&&
   p.m_rgpPlayerItems[KNIFE_SLOT]==sword&&p.m_pActiveItem==sword&&service->Parts(*sword)==parts&&
   timers==std::array<float,4>{sword->m_flNextPrimaryAttack,sword->m_flNextSecondaryAttack,sword->m_flTimeWeaponIdle,p.m_flNextAttack}&&
   sword->m_pfnThink==think&&sword->pev->nextthink==nextThink&&before.prediction.serial==after.prediction.serial&&
   before.prediction.pendingAnimation==after.prediction.pendingAnimation&&before.prediction.pendingTime==after.prediction.pendingTime;
 };
 // Real precache resets only the menu connection/snapshot table. Existing
 // registered weapon metadata remains in the actual current map service.
 Require(CSOL_PrecacheBuyMenu(),"real menu precache reopens ready after snapshot table reset");
 const int allocated=f.engine.allocations,added=f.adds,deployed=f.deploys,removed=f.removes;
 f.events.clear();f.Replay();
 Check(f.events.empty()&&service->PaintType(*sword)==55,
  "snapshotFF still paints actual sword through current default selection4000 rather than entity4016");
 Check(preserved()&&f.engine.allocations==allocated&&f.adds==added&&f.deploys==deployed&&f.removes==removed,
  "empty-record paint tail changes no parts, entity, deployment or attack state");
 Require(service->ReplaceSelectionMetadata(p,life->generation,{},{{483,{31,32}},{363,{41,42}}}),"trusted map genuinely lacks paint entries");
 f.Replay();Check(service->PaintType(*sword)==0&&preserved(),"missing paint map entry clears only paint and preserves prior parts/attacks");
 Require(service->ReplaceSelectionMetadata(p,life->generation,{{4000,65},{4016,91},{4010,92}},{{483,{51,52}},{363,{61,62}}}),
  "distinct latest selection metadata before failed alternative purchase");
 CSOL_RebuyMeleeSelected(p,1023);f.rejectDeployId=1023;bool changed{};
 f.intermediate=[&](CBasePlayer& current){if(!changed){changed=true;CSOL_RebuyMeleeSelected(current,1022);}};
 f.events.clear();f.Replay();f.intermediate={};f.rejectDeployId=0;
 Check(changed&&f.deploys>deployed&&identity.Get<CBaseEntity>()==sword&&p.m_rgpPlayerItems[KNIFE_SLOT]==sword,
  "real rejected axe deployment retains old sword while independent setter changes current selection");
 Check(service->PaintType(*sword)==91&&preserved(),
  "failed knife purchase still paints retained sword using current selected directory4016 instead of attempted4010");
 Check(Equal(p.m_rebuyStruct,RebuyStruct{}),"paint tail and failed purchase do not overwrite empty native snapshot");
 // Reset again gives a genuine FF snapshot. With no owned knife the tail
 // must neither materialize a default knife nor mutate the detached sword.
 p.m_rgpPlayerItems[KNIFE_SLOT]=nullptr;p.m_pActiveItem=nullptr;Require(CSOL_PrecacheBuyMenu(),"real second menu reset for empty-slot paint edge");
 const int noKnifeAllocated=f.engine.allocations,noKnifeAdded=f.adds;
 f.events.clear();f.Replay();Check(!p.m_rgpPlayerItems[KNIFE_SLOT]&&f.engine.allocations==noKnifeAllocated&&f.adds==noKnifeAdded,
  "empty knife slot and FF record never create or give a default knife");
 Check(service->PaintType(*sword)==91,"empty-slot paint tail does not update a detached weapon");
 p.m_rgpPlayerItems[KNIFE_SLOT]=sword;p.m_pActiveItem=sword;
}

void Stale(Fixture& f,const std::string& kind){
 auto& p=*f.player;f.Own("weapon_m4a1",PRIMARY_WEAPON_SLOT,WEAPON_M4A1);
 p.m_rebuyStruct={WEAPON_M4A1,0,WEAPON_USP,0,0,0,0,0,0,ARMOR_NONE};CSOL_RebuyMeleeSelected(p,1022);
 const auto saved=p.m_rebuyStruct;const int account=p.m_iAccount;f.rejectWeapon=true;
 f.transition=[&]{
  if(kind=="death"){p.pev->health=0;p.pev->deadflag=DEAD_DEAD;}
  else if(kind=="respawn")++p.m_iNumSpawns;
  else if(kind=="connection")++f.userId;
  else if(kind=="authority"){f.runtime->players->Remove(EHANDLE(&p));f.Profile();}
  else if(kind=="entity")++p.edict()->serialnumber;
  else if(kind=="map")f.runtime->RequestClose();
  else throw std::runtime_error("unknown stale group");
 };
 f.events.clear();f.Replay();
 Check(f.events==std::vector<std::string>{"weapon:22"},"hook-driven stale authority aborts all later replay items");
 Check(p.m_iAccount==account&&!p.m_rgpPlayerItems[KNIFE_SLOT],"stale replay cannot debit or deliver later free knife");
 Check(Equal(p.m_rebuyStruct,saved),"stale replay does not rewrite prior native snapshot");
 if(kind=="death"||kind=="respawn"){
  p.pev->health=100;p.pev->deadflag=DEAD_NO;f.events.clear();f.Replay();
  Check(std::find(f.events.begin(),f.events.end(),"knife")!=f.events.end(),"death/respawn cancellation retains record for a fresh valid transaction");
 }
 if(kind=="connection"){
  f.events.clear();f.Replay();Check(f.events.empty()&&!p.m_rebuyStruct.m_primaryWeapon&&!p.m_rebuyStruct.m_secondaryWeapon,
   "replacement userid cannot inherit native equipment snapshot or private knife selection");
 }
}

void NativeFallback(){
 auto engine=std::make_unique<Engine>();engine->Prepare("0");g_pGameRules=InstallGameRules();
 Require(g_pGameRules&&!CSOL_UsesScenarioRules(),"real ordinary CS rules installed");
 auto* ed=engine->Reserve(1,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
 Check(!CSOL_BuildRebuyStruct(p)&&!CSOL_Rebuy(p),"scenario rebuy adapters decline ordinary CS native entry points");
 Check(!CSOL_BuyMenuCommand(ed,"cl_rebuy")&&!CSOL_BuyMenuCommand(ed,"rebuy_in"),"ordinary server retains command fallback");
 p->m_rebuyStruct.m_primaryWeapon=WEAPON_AWP;p->BuildRebuyStruct();
 Check(!p->m_rebuyStruct.m_primaryWeapon,"ordinary native Build executes its original empty-inventory consumer");
 engine->Clear();
}
}

int main(int argc,char** argv){
 try{
  rebuy_test::Require(argc==3,"explicit group and recorded resource directory required");
  const std::string group=argv[1];
  if(group=="native-fallback")rebuy_test::NativeFallback();
  else{
   rebuy_test::Fixture f(argv[2]);
   if(group=="snapshot")rebuy_test::Snapshot(f);
   else if(group=="failed-capture")rebuy_test::FailedCapture(f);
   else if(group=="ordinary")rebuy_test::Ordinary(f);
   else if(group=="order")rebuy_test::Order(f);
   else if(group=="insufficient")rebuy_test::Insufficient(f);
   else if(group=="ammo-armor")rebuy_test::AmmoArmor(f);
   else if(group=="specials")rebuy_test::Specials(f);
   else if(group=="owned-cannon"||group=="owned-k3"||group=="owned-tknife")rebuy_test::OwnedGun(f,group.substr(6));
   else if(group=="owned-skull3")rebuy_test::OwnedSkull3(f);
   else if(group=="owned-chain")rebuy_test::OwnedChain(f);
   else if(group=="owned-continue")rebuy_test::OwnedContinue(f);
   else if(group=="knife")rebuy_test::Knife(f);
   else if(group=="paint")rebuy_test::Paint(f);
   else if(group.rfind("stale-",0)==0)rebuy_test::Stale(f,group.substr(6));
   else throw std::runtime_error("unknown rebuy group");
  }
 }catch(const std::exception& error){sdk_test::Check(false,error.what());std::cerr<<"EXCEPTION "<<error.what()<<'\n';}
 std::cout<<"behaviorChecks="<<rebuy_test::behaviorChecks<<" checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';
 return sdk_test::failures?1:0;
}
