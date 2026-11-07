// SPDX-License-Identifier: GPL-3.0-or-later
// Real SDK/menu/service/inventory objects. The scenario buy phase alone is a
// controlled gate; this does not simulate or accept the whole map startup.
#include "../csol-scenario-monster-skills/trap_fixture.hpp"
#include "../../native/csol/server/buy_menu.h"
#include "../../native/csol/server/weapons/weapon_service.h"
#include "../../native/csol/server/weapons/weapon_slots.h"
#include "../../native/csol/server/weapons/weapons.h"
#include "../../native/csol/server/weapons/wpn_melee.h"
#include "../../native/csol/server/weapons/melee_purchase.h"
#include "../../native/csol/server/weapons/knife_selection.h"
#include "../../native/csol/buy_menu.hpp"
#include "../../native/csol/weapons/melee_identity.hpp"
#include "../../native/csol/weapons/melee_prediction.hpp"
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
int assertions{},failed{},fault{},adds{},removes{},deploys{},pickupSounds{},msgId{};
CBasePlayerWeapon* original{};
std::vector<int> debits;
std::vector<std::string> args;
std::vector<std::vector<std::uint8_t>> packets;
std::function<void()> transition;
bool TraceEnabled(){return std::getenv("CSOL_PURCHASE_TRACE")!=nullptr;}
void Expect(bool good,const char* label){++assertions;if(!good){++failed;std::cerr<<"ASSERT "<<label<<'\n';}}
void Animation(IReGameHook_CBasePlayer_SetAnimation*,CBasePlayer*,PLAYER_ANIM) {}
void Account(IReGameHook_CBasePlayer_AddAccount* chain,CBasePlayer* p,int amount,RewardType reason,bool track)
{debits.push_back(amount);chain->callNext(p,amount,reason,track);}
BOOL Add(IReGameHook_CBasePlayer_AddPlayerItem* chain,CBasePlayer* p,CBasePlayerItem* item){
 if(!adds){
  Expect(CSOL_IsMeleeReplacement(*p,*item)==(original!=nullptr),"replacement suppression exists only for delivery with an owned prior knife");
  auto* unrelated=p->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT];
  Expect(unrelated&&!CSOL_IsMeleeReplacement(*p,*unrelated),"active replacement scope does not match an unrelated carried firearm");
 }
 ++adds;const auto ok=chain->callNext(p,item);
 if(TraceEnabled())std::cerr<<"ADD id="<<item->m_iId<<" ok="<<ok<<" active="<<(p->m_pActiveItem?p->m_pActiveItem->m_iId:0)<<'\n';
 if(item!=original&&transition){auto cb=std::move(transition);cb();}
 return item!=original&&fault==2?FALSE:ok;
}
BOOL Remove(IReGameHook_CBasePlayer_RemovePlayerItem* chain,CBasePlayer* p,CBasePlayerItem* item){
 ++removes;const auto ok=chain->callNext(p,item);if(TraceEnabled())std::cerr<<"REMOVE id="<<item->m_iId<<" ok="<<ok<<'\n';return item==original&&fault==3?FALSE:ok;
}
BOOL Deploy(IReGameHook_CBasePlayerWeapon_DefaultDeploy* chain,CBasePlayerWeapon* w,char* view,char* player,int sequence,char* extension,int skip){
 ++deploys;if(w!=original&&fault==1)return FALSE;const auto ok=chain->callNext(w,view,player,sequence,extension,skip);if(TraceEnabled())std::cerr<<"DEPLOY id="<<w->m_iId<<" ok="<<ok<<'\n';return w!=original&&fault==14?FALSE:ok;
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
 Require(!CSOL_RulesRejected(),CSOL_RulesRejectionReason());
 if(TraceEnabled()){
  Packet response{};if(!packets.empty())Decode(packets.back(),response);
  std::cerr<<"COMMAND";for(const auto& arg:args)std::cerr<<' '<<arg;
  std::cerr<<" response="<<int(response.kind)<<'/'<<int(response.result)<<" active="<<(p.m_pActiveItem?p.m_pActiveItem->m_iId:0)
   <<" last="<<(p.m_pLastItem?p.m_pLastItem->m_iId:0)<<" knife="<<(p.m_rgpPlayerItems[KNIFE_SLOT]?p.m_rgpPlayerItems[KNIFE_SLOT]->m_iId:0)<<'\n';
 }
}
Packet Last(){Packet p;Require(!packets.empty()&&Decode(packets.back(),p),"real response decodes");return p;}
Packet Offer(int id){for(auto it=packets.rbegin();it!=packets.rend();++it){Packet p;if(Decode(*it,p)&&p.kind==Kind::Item&&p.id==id)return p;}throw std::runtime_error("menu omitted requested offer");}
CBasePlayerWeapon* Held(CBasePlayer& p,int slot){return dynamic_cast<CBasePlayerWeapon*>(p.m_rgpPlayerItems[slot]);}
void Sole(CBasePlayer& p,int id){auto* w=Held(p,KNIFE_SLOT);Expect(w&&w->m_iId==id&&!w->m_pNext&&w->m_pPlayer==&p&&w->pev->owner==p.edict(),"actual purchase leaves exactly one owned knife");}
struct WeaponSnapshot {
 CBasePlayerWeapon& weapon;
 std::array<float,9> timers;
 std::array<int,5> state;
 bool delay;
 decltype(CBaseEntity::m_pfnThink) think;
 weapon_data_t prediction{};
 CSOLMeleePurchaseState privateState{};bool customMelee{};
 explicit WeaponSnapshot(CBasePlayerWeapon& value):weapon(value),
  timers{value.m_flNextPrimaryAttack,value.m_flNextSecondaryAttack,value.m_flTimeWeaponIdle,
   value.m_flLastFireTime,value.m_flPrevPrimaryAttack,value.m_flDecreaseShotsFired,value.m_flAccuracy,
   value.m_flNextReload,value.pev->nextthink},
  state{value.m_iShotsFired,value.m_fInReload,value.m_fInSpecialReload,value.m_iWeaponState,value.m_iClip},
  delay(value.m_bDelayFire),think(value.m_pfnThink){CSOL_PackMeleeWeapon(&value,&prediction);customMelee=CSOL_SaveMeleePurchaseState(value,privateState);}
 bool Unchanged()const{
  const WeaponSnapshot current(weapon);
  return timers==current.timers&&state==current.state&&delay==current.delay&&think==current.think&&
   customMelee==current.customMelee&&(!customMelee||(
    privateState.prediction.primaryOrdinal==current.privateState.prediction.primaryOrdinal&&
    privateState.prediction.serial==current.privateState.prediction.serial&&
    privateState.prediction.pendingAnimation==current.privateState.prediction.pendingAnimation&&
    privateState.prediction.pendingTime==current.privateState.prediction.pendingTime&&
    privateState.deploymentComplete==current.privateState.deploymentComplete))&&
   prediction.m_iWeaponState==current.prediction.m_iWeaponState&&prediction.fuser1==current.prediction.fuser1&&
   prediction.fuser2==current.prediction.fuser2;
 }
};
struct PlayerSnapshot {
 CBasePlayer& player;CBasePlayerItem *active,*last;float nextAttack;int viewmodel,weaponmodel,animation;
 explicit PlayerSnapshot(CBasePlayer& p):player(p),active(p.m_pActiveItem),last(p.m_pLastItem),
  nextAttack(p.m_flNextAttack),viewmodel(p.pev->viewmodel),weaponmodel(p.pev->weaponmodel),animation(p.pev->weaponanim){}
 bool UnchangedExceptLast(CBasePlayerItem* expectedLast)const{return player.m_pActiveItem==active&&player.m_pLastItem==expectedLast&&player.m_flNextAttack==nextAttack&&
  static_cast<int>(player.pev->viewmodel)==viewmodel&&static_cast<int>(player.pev->weaponmodel)==weaponmodel&&player.pev->weaponanim==animation;}
 bool Unchanged()const{return UnchangedExceptLast(last);}
};
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
 e.onSound=[](edict_t*,const char* sample){if(!std::strcmp(sample,"items/gunpickup2.wav"))++pickupSounds;};
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
 UTIL_PrecacheOtherWeapon("weapon_knife");
 Require(CSOL_WeaponsReady()&&CSOL_MeleeReady(1022)&&CSOL_MeleeReady(1023),"actual two-knife config/registry ready");
 Require(CSOL_PrecacheBuyMenu(),"actual menu resource validation and registration");
 auto own=[&](const char* name,int slot,int id){auto* ed=Engine::Create(Engine::String(name));auto* w=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));Require(w,"actual initial weapon factory");w->CBasePlayerWeapon::Spawn();w->m_iId=static_cast<WeaponIdType>(id);Require(w->GetItemInfo(&CBasePlayerItem::m_ItemInfoArray[id]),"actual native weapon metadata includes its real selection weight");w->m_pPlayer=&p;w->pev->owner=p.edict();p.m_rgpPlayerItems[slot]=w;return w;};
 const bool nativeFirst=mode==14||mode==15;
 const bool noKnife=mode==10||mode==11||nativeFirst;
 original=noKnife?nullptr:own("weapon_knife",KNIFE_SLOT,WEAPON_KNIFE);
 auto* primary=own("weapon_m4a1",PRIMARY_WEAPON_SLOT,WEAPON_M4A1);auto* pistol=own("weapon_usp",PISTOL_SLOT,WEAPON_USP);
 const bool holdingGun=mode==8||mode==9||mode==11||mode==12||mode==15;
 auto* gun=mode==9?pistol:primary;
 p.m_pActiveItem=holdingGun?gun:original;p.m_pLastItem=holdingGun?(mode==9?primary:pistol):pistol;
 if(mode==12)p.m_pLastItem=original;
 p.m_iAccount=mode?7500:0;p.m_flNextAttack=17;p.m_iAutoWepSwitch=1;
 if(original){original->m_flNextPrimaryAttack=18;original->m_flNextSecondaryAttack=19;original->m_flTimeWeaponIdle=20;}
 gun->m_flNextPrimaryAttack=31;gun->m_flNextSecondaryAttack=32;gun->m_flTimeWeaponIdle=33;
 gun->m_fInReload=TRUE;gun->m_iShotsFired=7;gun->m_flNextReload=34;gun->m_flAccuracy=.375f;
 const WeaponSnapshot firearm(*gun);const PlayerSnapshot beforePlayer(p);
 const auto keptGun=[&]{return firearm.Unchanged()&&(mode==12?beforePlayer.UnchangedExceptLast(Held(p,KNIFE_SLOT)):beforePlayer.Unchanged());};
 const auto weapons=f.session->Weapons();const auto authority=f.players->Find(EHANDLE(&p));
 Require(weapons&&authority,"actual registered player and weapon metadata services");
 Require(weapons->ReplaceSelectionMetadata(p,authority->generation,{{4016,71},{4010,72}},{{483,{11,12}},{363,{21,22}}}),
  "trusted complete player metadata snapshot is distinct from current weapon state");
 p.m_signals.Signal(SIGNAL_BUY);p.m_signals.Update();
 Require(p.CanPlayerBuy(false),"real native buyzone gate accepts eligible player in controlled phase");
 Hooks(true);fault=nativeFirst?0:mode;
 Command(p,{"csol_buy_menu","1"});
 Expect(Offer(1022).result==Result::Available&&Offer(1023).result==Result::Available&&Offer(1022).price==0&&Offer(1023).price==0,"actual Available and Price expose both free knives");
 Expect(Offer(29).result==Result::Available&&Offer(29).price==0,"real menu offers the default knife for zero cost");
 if(nativeFirst){
  Command(p,{"csol_buy","1","29"});
  Expect(Last().result==Result::Success&&p.m_iAccount==7500,"default knife first grant succeeds through the real menu");Sole(p,29);
  Expect(CSOL_MeleeSelectionItem(*Held(p,KNIFE_SLOT))==161,"first native grant has exact default catalog identity");
  Expect(pickupSounds==1,"first native grant keeps normal pickup effects");
  if(holdingGun)Expect(keptGun(),"native default first grant respects firearm weight and preserves its state");
  else Expect(p.m_pActiveItem==Held(p,KNIFE_SLOT)&&deploys==1,"no active item deploys first native knife once");
  Hooks(false);return;
 }
 if(mode==4)transition=[&]{const auto saved=args;Command(p,{"csol_buy","1","1023"});args=saved;};
 if(mode==5)transition=[&]{p.pev->deadflag=DEAD_DEAD;p.pev->health=0;};
 if(mode==6)transition=[&]{++p.m_iNumSpawns;};
 if(mode==7)transition=[&]{f.runtime->RequestClose();};
 Command(p,{"csol_buy","1","1022"});
 if(mode==0||mode==4||mode>=8){
  Expect(Last().result==Result::Success&&Last().price==0&&p.m_iAccount==(mode?7500:0),"zero-priced menu delivery succeeds without balance change");Sole(p,1022);
  Expect(pickupSounds==(noKnife?1:0),"only normal no-knife delivery emits its native pickup sound");
  if(holdingGun)Expect(keptGun(),"selecting a knife while holding a gun preserves active, mapped last, reload, presentation and cooldown");
  if(noKnife&&!holdingGun)Expect(p.m_pActiveItem==Held(p,KNIFE_SLOT),"native auto-selection deploys the first knife when there is no active item");
  if(!holdingGun&&!noKnife)Expect(p.m_pActiveItem==Held(p,KNIFE_SLOT),"changing the active knife deploys the replacement");
  auto* sword=Held(p,KNIFE_SLOT);const auto attempts=adds;const auto moneyCalls=debits.size();
  const auto attributeKey=weapons->CreateAttributes(483,17);
  Require(attributeKey&&weapons->SetAttributeKey(*sword,*attributeKey)&&weapons->SetEnhancementLevel(*sword,9)&&
   weapons->SetAddonWeaponInput(*sword,-17),"bind independent attribute, enhancement and addon state to the exact knife");
  Expect(weapons->Parts(*sword)==std::array<std::uint32_t,2>{11,12}&&weapons->PaintType(*sword)==71,
   "first selection consumes exact catalog parts and source-key paint from authority");
  Require(weapons->ReplaceSelectionMetadata(p,authority->generation,{{4016,81},{4010,82}},{{483,{31,32}},{363,{41,42}}}),
   "update authority without mutating current weapon until a fresh selection");
  Require(weapons->SelectItems(p,authority->generation,weapons->DefaultSelection()),
   "reselecting native loadout on the same player generation is valid");
  CSOLScenarioPlayerProfile refreshedProfile;refreshedProfile.equipment.classModsComplete=true;
  refreshedProfile.selectedWeapons=weapons->DefaultSelection();
  Require(f.players->SetProfile(p,std::move(refreshedProfile))&&f.players->Find(EHANDLE(&p))==authority,
   "real profile update preserves the exact registered player generation");
  Command(p,{"csol_buy","1","1022"});Expect(adds==attempts&&debits.size()==moneyCalls&&Held(p,KNIFE_SLOT)==sword,"repeated receipt never repeats entity or money transaction");
  Expect(weapons->Parts(*sword)==std::array<std::uint32_t,2>{11,12}&&weapons->PaintType(*sword)==71,
   "receipt replay does not execute the selection metadata tail twice");
  if(p.m_pActiveItem==sword){
   // This fixture owns Selection resources, not Combat's movement upgrades.
   // Start a real delayed attack, then restore the pre-existing rule phase;
   // the resulting prediction and scheduled Think remain untouched.
   const auto freeze=g_pGameRules->m_bFreezePeriod;const auto canShoot=p.m_bCanShoot;
   g_pGameRules->m_bFreezePeriod=FALSE;p.m_bCanShoot=true;sword->PrimaryAttack();
   g_pGameRules->m_bFreezePeriod=freeze;p.m_bCanShoot=canShoot;
   weapon_data_t pending{};CSOL_PackMeleeWeapon(sword,&pending);
   Expect(csol::melee::ReadPrediction(pending).pendingAnimation!=0&&sword->m_pfnThink&&sword->pev->nextthink>gpGlobals->time,
    "same-kind regression starts a real delayed melee attack before reselection");
  }
  const WeaponSnapshot pending(*sword);const PlayerSnapshot selection(p);const EHANDLE identity(sword);
  const auto created=e.allocations;const auto removed=removes;const auto deployed=deploys;const auto sounds=pickupSounds;
  Command(p,{"csol_buy_menu","2"});Expect(Offer(1022).result==Result::Available&&Offer(1023).result==Result::Available,"same-kind and alternate selection remain available");
  Command(p,{"csol_buy","2","1022"});Expect(Last().result==Result::Success&&identity.Get<CBaseEntity>()==sword&&Held(p,KNIFE_SLOT)==sword,
   "same-kind new request succeeds on the existing exact entity");
  Expect(pending.Unchanged()&&selection.Unchanged()&&e.allocations==created&&adds==attempts&&removes==removed&&deploys==deployed&&pickupSounds==sounds,
   "same-kind refresh preserves pending attack, serial, timers and presentation without Spawn, Touch or Deploy");
  Expect(weapons->Parts(*sword)==std::array<std::uint32_t,2>{31,32}&&weapons->PaintType(*sword)==81,
   "same-kind fresh selection actually refreshes metadata instead of returning a no-op success");
  Expect(weapons->AttributeKey(*sword)==*attributeKey&&weapons->EnhancementLevel(*sword)==9&&weapons->AddonWeaponInput(*sword)==-17,
   "selection metadata refresh does not erase unrelated weapon attributes, enhancement or addon input");
  Command(p,{"csol_buy_menu","3"});
  if(mode==13){
   fault=1;Command(p,{"csol_buy","3","1023"});
   Expect(Last().result==Result::Failed&&identity.Get<CBaseEntity>()==sword&&Held(p,KNIFE_SLOT)==sword,
    "failed alternate Deploy keeps the exact pending-attack knife owned");
   Expect(pending.Unchanged()&&selection.Unchanged(),"host transaction rollback restores the old real pending attack and presentation after Deploy refusal");
   fault=0;
  }
  Command(p,{"csol_buy","3","1023"});Expect(Last().result==Result::Success,"fresh catalog selects the opposite knife");Sole(p,1023);
  Expect(weapons->Parts(*Held(p,KNIFE_SLOT))==std::array<std::uint32_t,2>{41,42}&&weapons->PaintType(*Held(p,KNIFE_SLOT))==82,
   "alternate knife uses its own catalog and paint-source identity");
  if(holdingGun)Expect(keptGun(),"alternate knife selection while holding a firearm never switches or cancels reload");
  Expect(pickupSounds==sounds,"existing knife replacement does not add a pickup sound");
  Command(p,{"csol_buy_menu","4"});Command(p,{"csol_buy","4","1022"});Expect(Last().result==Result::Success,"opposite knife can be selected again through fresh catalog");Sole(p,1022);
  Expect(pickupSounds==sounds,"replacement sound suppression covers both directions");
  sword=Held(p,KNIFE_SLOT);const WeaponSnapshot beforeMissing(*sword);
  Require(weapons->ReplaceSelectionMetadata(p,authority->generation,{},{}),"explicit complete empty selection map represents a real lookup miss");
  Command(p,{"csol_buy_menu","5"});Command(p,{"csol_buy","5","1022"});
  Expect(Last().result==Result::Success&&beforeMissing.Unchanged()&&weapons->Parts(*sword)==std::array<std::uint32_t,2>{}&&weapons->PaintType(*sword)==0,
   "known map miss clears only selected metadata without resetting attacks");
  Expect(!CSOL_IsMeleeReplacement(p,*sword)&&!CSOL_IsMeleeReplacement(p,*primary),"replacement scope never escapes into later pickups");
  // Restore the initial knife through the actual menu, not a direct grant.
  Require(weapons->ReplaceSelectionMetadata(p,authority->generation,{{4000,91}},{{161,{51,52}}}),
   "default knife metadata is provided by the same authoritative selection service");
  if(!holdingGun){
   original=sword;
   const EHANDLE keep(sword);const WeaponSnapshot oldKnife(*sword);const PlayerSnapshot oldPlayer(p);
   for(int rejectFault:{1,14,2,3}){
    fault=rejectFault;Command(p,{"csol_buy_menu",std::to_string(20+rejectFault)});Command(p,{"csol_buy",std::to_string(20+rejectFault),"29"});
    Expect(Last().result==Result::Failed&&keep.Get<CBaseEntity>()==sword&&Held(p,KNIFE_SLOT)==sword,
     "native knife rejected Deploy/Add/Remove never commits or loses the previous knife");
    Expect(oldKnife.Unchanged()&&oldPlayer.Unchanged(),"default-knife rejection restores original attack and player presentation");
   }
   fault=0;
  }
  Command(p,{"csol_buy_menu","40"});Command(p,{"csol_buy","40","29"});
  Expect(Last().result==Result::Success,"default knife can be bought back after a custom knife");Sole(p,29);
  auto* normal=Held(p,KNIFE_SLOT);const EHANDLE normalIdentity(normal);const WeaponSnapshot normalState(*normal);const PlayerSnapshot normalPlayer(p);
  Expect(CSOL_MeleeSelectionItem(*normal)==161&&weapons->Parts(*normal)==std::array<std::uint32_t,2>{51,52}&&weapons->PaintType(*normal)==91,
   "default knife consumes its real catalog161 and paint4000 metadata");
  if(holdingGun)Expect(keptGun(),"restoring default knife preserves a held firearm and mapped lastinv");
  const auto nativeAdds=adds,nativeDeploys=deploys,nativeSounds=pickupSounds;const auto nativeAllocations=e.allocations;
  Require(weapons->ReplaceSelectionMetadata(p,authority->generation,{{4000,92}},{{161,{61,62}}}),"fresh default metadata available");
  Command(p,{"csol_buy_menu","41"});Command(p,{"csol_buy","41","29"});
  Expect(Last().result==Result::Success&&normalIdentity.Get<CBaseEntity>()==normal&&normalState.Unchanged()&&normalPlayer.Unchanged()&&
   adds==nativeAdds&&deploys==nativeDeploys&&pickupSounds==nativeSounds&&e.allocations==nativeAllocations,
   "same default knife preserves entity, timers and presentation without Spawn Touch Deploy");
  Expect(weapons->Parts(*normal)==std::array<std::uint32_t,2>{61,62}&&weapons->PaintType(*normal)==92,
   "same default knife runs the real common metadata tail");
  Command(p,{"csol_buy_menu","42"});Command(p,{"csol_buy","42","1022"});
  Expect(Last().result==Result::Success,"special knife remains purchasable after default restoration");Sole(p,1022);sword=Held(p,KNIFE_SLOT);
  const EHANDLE beforeUnknown(sword);const WeaponSnapshot unknownState(*sword);
  weapons->RemovePlayer(EHANDLE(&p),authority->generation);
  Expect(!weapons->ReplaceSelectionMetadata(p,authority->generation,{{4016,99}},{{483,{91,92}}})&&
   !weapons->RefreshMeleeSelection(p,authority->generation,*sword),
   "retired selection cannot accept imports or refresh under its old generation");
  Command(p,{"csol_buy_menu","6"});Command(p,{"csol_buy","6","1022"});
  Expect(Last().result==Result::Failed&&beforeUnknown.Get<CBaseEntity>()==sword&&Held(p,KNIFE_SLOT)==sword&&unknownState.Unchanged(),
   "missing authority is a failed selection, not an invented empty-map success");
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
