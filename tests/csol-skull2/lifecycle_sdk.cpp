// SPDX-License-Identifier: GPL-3.0-or-later
// Real ReGameDLL objects and complete weapon/base methods. The engine owns
// controlled edicts/messages/traces; only player animation rendering is spied.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/weapons/wpn_skull2.h"
#include "../../native/csol/server/weapons/weapons.h"
#include "../../native/csol/server/weapons/weapon_slots.h"
#include "../../native/csol/weapons/skull2.hpp"
#include <bit>

namespace {
using namespace sdk_test;
namespace sk = csol::skull2;
unsigned assertions{}, failed{}, animationCalls{}, fireEvents{};
std::function<void(CBasePlayer*, PLAYER_ANIM)> onAnimation;
std::function<void()> onEvent;
std::function<void(CBasePlayerWeapon*)> onBaseReturn;
void Expect(bool value, const char* reason)
{ ++assertions; if (!value) { ++failed; std::cerr << "ASSERT " << reason << '\n'; } }
void Animation(IReGameHook_CBasePlayer_SetAnimation*, CBasePlayer* player, PLAYER_ANIM animation)
{ ++animationCalls; if (onAnimation) onAnimation(player, animation); }
void BaseFrame(IReGameHook_CBasePlayerWeapon_ItemPostFrame* chain,CBasePlayerWeapon* weapon)
{ chain->callNext(weapon); if(onBaseReturn)onBaseReturn(weapon); }
void Prepare(Engine& e)
{
    e.Prepare("0");
    g_pGameRules = InstallGameRules();
    Require(g_pGameRules && !CSOL_RulesRejected(), "real native rules installed");
    LinkUserMessages();
    g_engfuncs.pfnInfoKeyValue = [](char*, const char* name)->char* {
        return const_cast<char*>(!std::strcmp(name,"csol_protocol") ? "1" : ""); };
    g_engfuncs.pfnMakeVectors = [](const float*) {
        gpGlobals->v_forward = Vector(1,0,0); gpGlobals->v_right = Vector(0,1,0); gpGlobals->v_up = Vector(0,0,1); };
    g_engfuncs.pfnCanSkipPlayer = [](const edict_t*) { return 0; };
    // Controlled empty world has no texture hit; the real FireBullets3 path
    // still performs its material query before checking trace fraction.
    g_engfuncs.pfnTraceTexture = [](edict_t*,const float*,const float*) -> const char* { return nullptr; };
    g_engfuncs.pfnPlaybackEvent = [](int,const edict_t*,unsigned short,float,float*,float*,float,float,int,int,int,int) {
        ++fireEvents; if (onEvent) onEvent(); };
    e.onTrace = [](edict_t*, TraceResult&) {};
    g_ReGameHookchains.m_CBasePlayer_SetAnimation.registerHook(Animation,192);
    g_ReGameHookchains.m_CBasePlayerWeapon_ItemPostFrame.registerHook(BaseFrame,192);
}
CBasePlayer* Player(Engine& e, int index)
{
    auto* ed = e.Reserve(index,"player");
    auto* p = GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
    ed->v.flags = FL_CLIENT|FL_ONGROUND; ed->v.health = 100; ed->v.deadflag = DEAD_NO;
    ed->v.takedamage = DAMAGE_AIM; p->m_iTeam = CT; p->m_iJoiningState = JOINED;
    p->m_iNumSpawns = 1; p->m_bJustConnected = false; p->has_disconnected = false;
    p->m_bCanShoot = true; p->CSPlayer()->m_bCanShootOverride = true;
    return p;
}
CBasePlayerWeapon* Gun(CBasePlayer& p)
{
    auto* ed = Engine::Create(Engine::String(sk::Classname));
    auto* w = dynamic_cast<CBasePlayerWeapon*>(CBaseEntity::Instance(ed));
    Require(w, "real exported SKULL2 factory returns a weapon");
    // Base Spawn copies the actual GetItemInfo to this entity's ReAPI wrapper.
    // Full map precache/readiness and pickup are outside this lifecycle fixture.
    w->CBasePlayerWeapon::Spawn();
    w->m_pPlayer = &p; ed->v.owner = p.edict(); w->m_iPrimaryAmmoType = 15;
    w->m_iSecondaryAmmoType = -1; p.m_rgpPlayerItems[PISTOL_SLOT] = w;
    p.m_pActiveItem = w; w->m_flTimeWeaponIdle = 100;
    Expect(CSOL_IsSkull2(*w), "actual dynamic type and original item identity match");
    return w;
}
void State(CBasePlayerWeapon& w, int flags, int clip, int reserve, float deadline=0)
{
    w.m_pPlayer->m_rgAmmo[w.m_iPrimaryAmmoType] = reserve;
    CSOL_RestoreSkull2PurchaseState(w,{flags,clip,deadline,true});
    w.m_iShotsFired = 0; w.m_flLastFire = 0; w.m_fInReload = false;
    w.m_flNextPrimaryAttack = w.m_flNextSecondaryAttack = 0;
    w.m_flTimeWeaponIdle = 100; w.m_pPlayer->m_flNextAttack = 0;
    w.m_pPlayer->pev->button = 0;
}
void CapacityAndCancel(Engine& e)
{
    auto* first = Gun(*Player(e,1)); auto* second = Gun(*Player(e,2));
    State(*first,sk::Dual,11,35); State(*second,sk::Single,3,4);
    Expect(first->iMaxClip()==14 && second->iMaxClip()==7, "real ReAPI iMaxClip is per instance");
    first->SecondaryAttack();
    Expect(first->iMaxClip()==7 && first->m_iClip==11 && second->iMaxClip()==7,
        "pending B to A changes only instance capacity and retains overfull magazine");
    second->SecondaryAttack();
    const auto other = CSOL_SaveSkull2PurchaseState(*second);
    first->Holster();
    const auto cancelled = CSOL_SaveSkull2PurchaseState(*first);
    Expect(cancelled.flags==sk::Dual && cancelled.clip==11 && first->iMaxClip()==14,
        "real Holster cancels conversion and restores source capacity without ammunition loss");
    Expect(CSOL_SaveSkull2PurchaseState(*second).flags==other.flags && second->m_iClip==3,
        "another weapon's transition remains independent");
    e.globals.time = other.deadline;
    second->m_flNextPrimaryAttack=second->m_flNextSecondaryAttack=100;
    second->ItemPostFrame();
    Expect(CSOL_SaveSkull2PurchaseState(*second).flags==sk::ToDual && second->m_iClip==3,
        "real ItemPostFrame does not commit at exact deadline");
    e.globals.time = other.deadline+.001f;
    second->ItemPostFrame();
    Expect(CSOL_SaveSkull2PurchaseState(*second).flags==sk::Dual && second->m_iClip==7 &&
        second->m_pPlayer->m_rgAmmo[15]==0 && second->iMaxClip()==14,
        "real ItemPostFrame commits once and consumes available reserve only");
    first->ItemPostFrame();
    Expect(first->m_iClip==11 && first->m_pPlayer->m_rgAmmo[15]==35 && first->iMaxClip()==14,
        "cancelled deadline cannot later move ammunition");
}
void ReloadAndFire(Engine& e)
{
    auto* w=Gun(*Player(e,1));
    for(int mode:{sk::Single,sk::Dual}) for(int reserve:{1,2,20})
    {
        State(*w,mode,0,reserve); w->Reload();
        const int loaded=std::min(sk::MaxClip(mode),reserve);
        Expect(w->m_fInReload && w->m_iClip==0 && w->m_pPlayer->m_rgAmmo[15]==reserve,
            "actual DefaultReload begins without moving ammunition");
        bool observed{};
        onAnimation=[&](CBasePlayer*,PLAYER_ANIM animation){
            if(animation!=PLAYER_ATTACK1 && animation!=PLAYER_ATTACK2)return;
            observed=true;const auto state=CSOL_SaveSkull2PurchaseState(*w);
            const bool expectedLeft=(loaded%2)==0;
            const bool side=(state.flags&sk::Left)!=0;
            Expect(!w->m_fInReload && w->m_pPlayer->m_rgAmmo[15]==reserve-loaded && w->m_iClip==loaded-1,
                "actual same-frame Primary observes completed reload, reserve debit and decremented clip");
            Expect(side==(mode==sk::Dual?!expectedLeft:expectedLeft),
                "reload callback derives side from final pre-fire clip before same-frame Primary toggles dual side");
        };
        w->m_pPlayer->m_flNextAttack=0;w->m_pPlayer->pev->button=IN_ATTACK;
        w->ItemPostFrame();
        onAnimation={};
        Expect(observed,"actual base ItemPostFrame fires on the reload-completion frame");
    }
}
void Drop(Engine& e)
{
    auto* p=Player(e,1); auto* w=Gun(*p);
    State(*w,sk::Single|sk::Left,3,20);
    const auto before=CSOL_SaveSkull2PurchaseState(*w);
    auto* box=dynamic_cast<CWeaponBox*>(p->DropPlayerItem(""));
    Expect(box && box->m_rgpPlayerItems[PISTOL_SLOT]==w,"real DropPlayerItem packs the same weapon entity");
    const auto after=CSOL_SaveSkull2PurchaseState(*w);
    Expect(after.flags==(sk::Dual|sk::Left) && w->m_iClip==10 && p->m_rgAmmo[15]==13 && w->iMaxClip()==14,
        "pre-pack callback converts single mode, loads exactly seven and preserves side");
    Expect(p->m_rgpPlayerItems[PISTOL_SLOT]==nullptr,"real PackWeapon removes inventory ownership");
    // Transaction snapshot restores private state, independently of ammo which
    // is managed by the purchase transaction. Full purchase is a separate test.
    w->m_pPlayer=p;CSOL_RestoreSkull2PurchaseState(*w,before);
    Expect(CSOL_SaveSkull2PurchaseState(*w).flags==before.flags && w->m_iClip==3 && w->iMaxClip()==7,
        "rollback helper restores original single state and per-instance capacity");
}
void Pack(Engine& e)
{
    auto* p=Player(e,1);auto* w=Gun(*p);State(*w,sk::ToSingle|sk::Left,11,7,e.globals.time+1.2345f);
    w->m_iShotsFired=12345;w->m_flAccuracy=.81f;w->m_flLastFire=e.globals.time-.125f;
    std::array<weapon_data_t,MAX_WEAPONS> data{};
    Expect(e.api.pfnGetWeaponData(p->edict(),data.data())==1,"actual DLL GetWeaponData entry runs");
    const auto& packed=data[0];
    Expect(packed.m_iId==312 && packed.m_iClip==11 && packed.m_iWeaponState==(sk::ToSingle|sk::Left|sk::HasPreviousFire),
        "actual GetWeaponData puts active extended weapon in slot zero with private mode flags");
    Expect(packed.fuser1==.81f && packed.fuser2==-.125f && sk::UnpackShots(packed.fuser3)==12345,
        "actual Pack hook preserves accuracy, previous fire and semiautomatic gate");
    Expect(std::abs(packed.m_flNextReload-1.2345f)<.00001f,"actual Pack path uses countdown field registered by installed delta");
}
void Reentry(Engine& e)
{
    auto* p=Player(e,1);auto* alternate=Player(e,2);auto* w=Gun(*p);
    for(int operation=0;operation<10;++operation)
    {
        State(*w,sk::ToDual,3,20,e.globals.time-1);
        const int life=p->m_iNumSpawns,ownerSerial=p->edict()->serialnumber,weaponSerial=w->edict()->serialnumber;
        p->m_pActiveItem=w;const auto original=CSOL_SaveSkull2PurchaseState(*w);
        unsigned calls{};
        onBaseReturn=[&](CBasePlayerWeapon* observed){
            ++calls;Expect(observed==w,"actual ReAPI base-frame callback receives exact derived weapon");
            switch(operation){
            case 0:p->pev->health=0;break;
            case 1:p->pev->flags|=FL_DORMANT;break;
            case 2:w->pev->flags|=FL_KILLME;break;
            case 3:w->m_pPlayer=alternate;break;
            case 4:++p->m_iNumSpawns;break;
            case 5:p->has_disconnected=true;break;
            case 6:p->m_pActiveItem=nullptr;break;
            case 7:++w->edict()->serialnumber;break;
            case 8:++p->edict()->serialnumber;break;
            case 9:p->pev->flags|=FL_KILLME;break;
            }
        };
        w->ItemPostFrame();onBaseReturn={};
        const auto after=CSOL_SaveSkull2PurchaseState(*w);
        Expect(calls==1,"one actual base ItemPostFrame completed before lifecycle mutation");
        Expect(after.flags==original.flags && after.clip==original.clip &&
            p->m_rgAmmo[15]==20 && alternate->m_rgAmmo[15]==0,
            "invalidated owner/life/entity/active state cannot commit a pending switch after base returns");
        w->m_pPlayer=p;p->m_pActiveItem=w;p->m_iNumSpawns=life;p->has_disconnected=false;
        p->pev->health=100;p->pev->flags=FL_CLIENT|FL_ONGROUND;w->pev->flags=0;
        p->edict()->serialnumber=ownerSerial;w->edict()->serialnumber=weaponSerial;
    }
}
}
int main(int argc,char** argv)
{
    try{
        auto engine=std::make_unique<sdk_test::Engine>();Prepare(*engine);
        const std::string group=argc>1?argv[1]:"capacity";
        if(group=="capacity")CapacityAndCancel(*engine);
        else if(group=="reload")ReloadAndFire(*engine);
        else if(group=="drop")Drop(*engine);
        else if(group=="pack")Pack(*engine);
        else if(group=="reentry")Reentry(*engine);
        else throw std::runtime_error("unknown lifecycle group");
        onAnimation={};onEvent={};onBaseReturn={};g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);
        g_ReGameHookchains.m_CBasePlayerWeapon_ItemPostFrame.unregisterHook(BaseFrame);
        engine.reset();
        std::cout<<"behavior="<<assertions<<" failed="<<failed<<" sdk="<<sdk_test::checks<<" sdkFailed="<<sdk_test::failures<<'\n';
        return failed||sdk_test::failures?1:0;
    }catch(const std::exception& error){std::cerr<<"EXCEPTION "<<error.what()<<" behavior="<<assertions<<" failed="<<failed<<'\n';return 2;}
}
