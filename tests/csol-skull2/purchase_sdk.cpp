// SPDX-License-Identifier: GPL-3.0-or-later
// Link the full production SDK objects. Only engine boundaries and real ReAPI
// plugin callbacks are controlled; transaction and inventory methods are intact.
#define main PreviousLifecycleMain
#include "lifecycle_sdk.cpp"
#undef main
#include "../../native/csol/server/primary_purchase.h"
#include "../../native/csol/server/weapon_service.h"
#include "../../native/include/goldcraft/csol_weapon_api.hpp"
#include "../../native/csol/server/wpn_skull1.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace {
CBasePlayerWeapon *oldGun{},*newGun{};
EHANDLE newHandle;
int fault{},targetId{},dropCalls{},addCalls{},deployCalls{};
bool observedDropReserve{};
CBasePlayer* recipient{};
bool transferring{};
unsigned staleTailLookups{};
unsigned poisonedReleases{};
std::vector<int> debits;
BOOL Add(IReGameHook_CBasePlayer_AddPlayerItem* chain,CBasePlayer* p,CBasePlayerItem* item)
{
    if(transferring)return chain->callNext(p,item);
    ++addCalls;
    if(item!=oldGun){newGun=dynamic_cast<CBasePlayerWeapon*>(item);newHandle=item;}
    if(item!=oldGun && fault==2)return FALSE;
    const auto result=chain->callNext(p,item);
    if(item!=oldGun && fault==3)return FALSE;
    if(item!=oldGun && fault==4){Engine::Remove(item->edict());return FALSE;}
    if(item!=oldGun && fault==9){UTIL_Remove(item);return FALSE;}
    if(item!=oldGun && fault==7){
        Require(p->RemovePlayerItem(item),"plugin unlinks inventory before immediate release");
        Engine::Remove(item->edict());return FALSE;
    }
    if(item!=oldGun && fault==8){
        Require(p->RemovePlayerItem(item),"plugin unlinks inventory before owner transfer");
        transferring=true;const auto transferred=recipient->AddPlayerItem(item);transferring=false;
        Require(transferred,"actual second-player AddPlayerItem accepts transfer");
        item->AttachToPlayer(recipient);return FALSE;
    }
    return result;
}
BOOL Remove(IReGameHook_CBasePlayer_RemovePlayerItem* chain,CBasePlayer* p,CBasePlayerItem* item)
{
    if(item==oldGun && fault==5)return FALSE;
    const auto result=chain->callNext(p,item);
    if(item==oldGun && fault==6)return FALSE;
    return result;
}
CBaseEntity* DropHook(IReGameHook_CBasePlayer_DropPlayerItem* chain,CBasePlayer* p,const char* name)
{
    ++dropCalls;
    const int before=p->m_rgAmmo[oldGun->m_iPrimaryAmmoType];
    const auto result=chain->callNext(p,name);
    observedDropReserve|=p->m_rgAmmo[oldGun->m_iPrimaryAmmoType]==before-7;
    return result;
}
BOOL DeployHook(IReGameHook_CBasePlayerWeapon_DefaultDeploy* chain,CBasePlayerWeapon* w,
    char* view,char* player,int sequence,char* extension,int skip)
{
    ++deployCalls;
    if(w!=oldGun && fault==1)return FALSE;
    return chain->callNext(w,view,player,sequence,extension,skip);
}
void Account(IReGameHook_CBasePlayer_AddAccount* chain,CBasePlayer* p,int value,RewardType reason,bool track)
{debits.push_back(value);chain->callNext(p,value,reason,track);}
void Hooks(bool enable)
{
    if(enable){
        g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.registerHook(Add,192);
        g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.registerHook(Remove,192);
        g_ReGameHookchains.m_CBasePlayer_DropPlayerItem.registerHook(DropHook,192);
        g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.registerHook(DeployHook,192);
        g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(Account,192);
    }else{
        g_ReGameHookchains.m_CBasePlayer_AddPlayerItem.unregisterHook(Add);
        g_ReGameHookchains.m_CBasePlayer_RemovePlayerItem.unregisterHook(Remove);
        g_ReGameHookchains.m_CBasePlayer_DropPlayerItem.unregisterHook(DropHook);
        g_ReGameHookchains.m_CBasePlayerWeapon_DefaultDeploy.unregisterHook(DeployHook);
        g_ReGameHookchains.m_CBasePlayer_AddAccount.unregisterHook(Account);
    }
}
void ReadConfig(Engine& e,const std::string& root,const char* name)
{
    const auto path=root+"/native/csol/config/"+name+"_damage.json";
    std::ifstream f(path,std::ios::binary);Require(bool(f),"frozen damage configuration exists");
    std::ostringstream text;text<<f.rdbuf();e.files[std::string("csol/weapons/")+name+"_damage.json"]=text.str();
}
void Transaction(Engine& e,int mode,int failure,int incoming)
{
    auto* p=Player(e,1);recipient=Player(e,2);
    e.afterFreeCallback=[](edict_t* ed){
        if(newGun && newHandle.Get<CBaseEntity>()==newGun && ed==newGun->edict()){
            ++poisonedReleases;ed->v.target=Engine::String("skull2_freed_tail_probe");
        }
    };
    e.onFind=[](edict_t*,const char* field,const char* value){
        if(!std::strcmp(field,"targetname")&&!std::strcmp(value,"skull2_freed_tail_probe"))++staleTailLookups;
    };
    auto* service=static_cast<csol::IWeaponService*>(CreateInterface(csol::WeaponServiceVersion,nullptr));
    Require(service,"actual weapon service factory");service->SetZombieMode(true);service->SetPlayerRole(1,1);
    if(mode<0){
        oldGun=dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(Engine::Create(Engine::String("weapon_usp"))));
        Require(oldGun,"actual native USP factory");oldGun->CBasePlayerWeapon::Spawn();
        oldGun->m_pPlayer=p;oldGun->pev->owner=p->edict();p->m_rgpPlayerItems[PISTOL_SLOT]=oldGun;
        p->m_pActiveItem=oldGun;oldGun->m_iPrimaryAmmoType=CBasePlayer::GetAmmoIndex("45acp");
        oldGun->m_iSecondaryAmmoType=-1;oldGun->m_iClip=3;p->m_rgAmmo[oldGun->m_iPrimaryAmmoType]=20;
        p->m_rgAmmo[CBasePlayer::GetAmmoIndex(sk::AmmoName)]=9;
    }else{
        oldGun=Gun(*p);
        oldGun->m_iPrimaryAmmoType=CBasePlayer::GetAmmoIndex(sk::AmmoName);
        State(*oldGun,mode|sk::Left,3,20,e.globals.time+3.5f);
    }
    p->m_rgAmmo[MAX_AMMO_SLOTS-1]=99;
    p->m_iAccount=10000;p->m_flNextAttack=25;
    oldGun->m_flNextPrimaryAttack=21;oldGun->m_flNextSecondaryAttack=22;oldGun->m_flTimeWeaponIdle=23;
    oldGun->m_flLastFire=24;oldGun->m_flAccuracy=.65f;oldGun->m_iShotsFired=6;
    const auto prior=CSOL_SaveSkull2PurchaseState(*oldGun);
    std::array<int,MAX_AMMO_SLOTS> reserve{};
    std::copy(std::begin(p->m_rgAmmo),std::end(p->m_rgAmmo),reserve.begin());
    fault=failure;targetId=incoming;
    Hooks(true);
    const int success=service->Buy(1,incoming);
    Hooks(false);
    if(!failure){
        auto* fresh=dynamic_cast<CBasePlayerWeapon*>(newHandle.Get<CBaseEntity>());
        Expect(success==1 && fresh && fresh->m_iId==incoming && p->m_pActiveItem==fresh,
            "real public service commits exact new weapon and actual Deploy");
        Expect(p->m_rgpPlayerItems[PISTOL_SLOT]==fresh && !fresh->m_pNext && fresh->m_iClip==7,
            "actual Touch/Add leaves exactly one secondary with seven initial rounds");
        Expect((fresh->m_iPrimaryAmmoType==oldGun->m_iPrimaryAmmoType)==(mode>=0) &&
            !std::strcmp(fresh->pszAmmo1(),"50AZ"),"actual registry shares 50AZ while preserving distinct USP 45acp index");
        if(mode<0)Expect(p->m_rgAmmo[oldGun->m_iPrimaryAmmoType]==20,
            "new 50AZ pistol does not consume distinct prior USP ammunition");
        p->m_signals.Signal(SIGNAL_BUY);p->m_signals.Update();buytime.value=-1;
        p->m_rgAmmo[fresh->m_iPrimaryAmmoType]=0;debits.clear();Hooks(true);
        const bool bought=BuyGunAmmo(p,fresh,false);Hooks(false);
        Expect(bought && p->m_rgAmmo[fresh->m_iPrimaryAmmoType]==7 && debits==std::vector<int>{-70},
            "actual BuyGunAmmo adds seven shared rounds and charges 70 once");
    }else{
        const auto after=CSOL_SaveSkull2PurchaseState(*oldGun);
        Expect(!success,"actual public purchase rejects injected native plugin failure");
        Expect(p->m_rgpPlayerItems[PISTOL_SLOT]==oldGun && !oldGun->m_pNext && oldGun->m_pPlayer==p &&
            oldGun->pev->owner==p->edict() && !(oldGun->pev->flags&FL_KILLME),
            "rejected replacement preserves exact original secondary entity and ownership");
        Expect(after.flags==prior.flags && after.clip==prior.clip && after.deadline==prior.deadline &&
            oldGun->m_iClip==3 && (mode<0 || oldGun->iMaxClip()==sk::MaxClip(prior.flags)),
            "transaction restores original private mode, clip, deadline and capacity");
        Expect(p->m_pActiveItem==oldGun && p->m_flNextAttack==25 && oldGun->m_flNextPrimaryAttack==21 &&
            oldGun->m_flNextSecondaryAttack==22 && oldGun->m_flTimeWeaponIdle==23 && oldGun->m_flLastFire==24 &&
            oldGun->m_flAccuracy==.65f && oldGun->m_iShotsFired==6,"rollback restores all observed attack timers and fire state");
        Expect(std::equal(std::begin(p->m_rgAmmo),std::end(p->m_rgAmmo),std::begin(reserve)),
            "rollback restores old and incoming ammo while preserving unrelated slots");
        auto* fresh=newHandle.Get<CBaseEntity>();
        if(failure==8)Expect(fresh && !(fresh->pev->flags&FL_KILLME) &&
            recipient->m_rgpPlayerItems[PISTOL_SLOT]==fresh && fresh->pev->owner==recipient->edict(),
            "rollback never deletes or reclaims a weapon legitimately transferred to another player");
        else Expect(!fresh || (fresh->pev->flags&FL_KILLME),"failed fresh weapon removed or scheduled for native removal");
    }
    if((mode==sk::Single||mode==sk::ToDual) && failure!=5 && failure!=6)
        Expect(observedDropReserve,"actual Drop callback moved seven reserves before transaction outcome");
    Expect(dropCalls==1,"replacement traverses actual DropPlayerItem once");
    if(failure==4||failure==7)Expect(poisonedReleases==1,"actual immediate release reached the poisoned-target boundary exactly once");
    Expect(staleTailLookups==0,"freed weapon never reaches DefaultTouch target tail after release callback poisons its target");
    e.afterFreeCallback={};e.onFind={};
}
}
int main(int argc,char** argv)
{
    try{
        Require(argc==2,"one transaction case argument required");
        const int code=std::stoi(argv[1]);
        const int modes[]{sk::Single,sk::ToDual,sk::ToSingle,-1};
        auto e=std::make_unique<Engine>();Prepare(*e);
        const auto frozen=(std::filesystem::path(argv[0]).parent_path()/"source").string();
        ReadConfig(*e,frozen,"skull1");ReadConfig(*e,frozen,"skull2");
        CSOL_InitWeapons();e->Set("csol_weapons","1");CSOL_ResetWeaponRegistry();CSOL_PrecacheWeapons();
        UTIL_PrecacheOtherWeapon("weapon_usp");
        Require(CSOL_WeaponsReady()&&CSOL_Skull1Ready()&&CSOL_Skull2Ready(),"actual precache and item/ammo registration succeeded");
        Transaction(*e,modes[code/20],code%10,(code%20<10)?312:130);
        Hooks(false);g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);
        g_ReGameHookchains.m_CBasePlayerWeapon_ItemPostFrame.unregisterHook(BaseFrame);
        e.reset();
        std::cout<<"behavior="<<assertions<<" failed="<<failed<<" sdk="<<sdk_test::checks<<" sdkFailed="<<sdk_test::failures<<'\n';
        return failed||sdk_test::failures?1:0;
    }catch(const std::exception& error){std::cerr<<"EXCEPTION "<<error.what()<<" behavior="<<assertions<<" failed="<<failed<<'\n';return 2;}
}
