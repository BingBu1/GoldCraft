// SPDX-License-Identifier: GPL-3.0-or-later
// SDK names/IDs are checked by the production TU compilation; these local
// polymorphic classes exercise the exact type/identity dispatch and rejection.
#include <cstdlib>
#include <iostream>
#include <typeinfo>
#include <memory>
#include <vector>
#include <string>
struct Vars { int flags{}; };
struct CBaseEntity { virtual ~CBaseEntity() = default; Vars vars{}, *pev=&vars; };
struct CBasePlayer : CBaseEntity {} owner;
struct CCSOLScenarioMonster : CBaseEntity {} monster;
struct CBasePlayerWeapon : CBaseEntity { int m_iId{}; CBasePlayer* m_pPlayer=&owner; };
#define TYPE(name, id, token) constexpr int token=id; struct name : CBasePlayerWeapon { name(){m_iId=id;} }
TYPE(CUSP,16,WEAPON_USP); TYPE(CMP5N,19,WEAPON_MP5N); TYPE(CSG552,27,WEAPON_SG552);
TYPE(CAK47,28,WEAPON_AK47); TYPE(CAUG,8,WEAPON_AUG); TYPE(CAWP,18,WEAPON_AWP);
TYPE(CC4,6,WEAPON_C4); TYPE(CDEAGLE,26,WEAPON_DEAGLE); TYPE(CFlashbang,25,WEAPON_FLASHBANG);
TYPE(CG3SG1,24,WEAPON_G3SG1); TYPE(CGLOCK18,17,WEAPON_GLOCK18); TYPE(CHEGrenade,4,WEAPON_HEGRENADE);
TYPE(CKnife,29,WEAPON_KNIFE); TYPE(CM249,20,WEAPON_M249); TYPE(CM3,21,WEAPON_M3);
TYPE(CM4A1,22,WEAPON_M4A1); TYPE(CMAC10,7,WEAPON_MAC10); TYPE(CP228,1,WEAPON_P228);
TYPE(CP90,30,WEAPON_P90); TYPE(CSCOUT,3,WEAPON_SCOUT); TYPE(CSmokeGrenade,9,WEAPON_SMOKEGRENADE);
TYPE(CTMP,23,WEAPON_TMP); TYPE(CXM1014,5,WEAPON_XM1014); TYPE(CELITE,10,WEAPON_ELITE);
TYPE(CFiveSeven,11,WEAPON_FIVESEVEN); TYPE(CUMP45,12,WEAPON_UMP45); TYPE(CSG550,13,WEAPON_SG550);
TYPE(CGalil,14,WEAPON_GALIL); TYPE(CFamas,15,WEAPON_FAMAS);
#undef TYPE
#include "actual_native_weapons.inc"
constexpr int FL_KILLME=1;
struct CSOLWeaponDamageSource { virtual ~CSOLWeaponDamageSource()=default; virtual int DamageWeaponId() const=0; };
struct CSOLWeaponMonsterKillCallbacks
{
    virtual ~CSOLWeaponMonsterKillCallbacks()=default;
    virtual void ActiveMonsterKilled(CCSOLScenarioMonster&,CBaseEntity*)=0;
    virtual void SourceMonsterKilled(CCSOLScenarioMonster&,CBaseEntity*)=0;
};
bool CSOL_CannonEmptyMonsterKillCallbacks(const CBasePlayerWeapon&) { return false; }
bool CSOL_BeamEmptyMonsterKillCallbacks(const CBasePlayerWeapon&) { return false; }
bool CSOL_TKnifeEmptyMonsterKillCallbacks(const CBasePlayerWeapon&) { return false; }
namespace csol::scenario { struct ConfigError { std::string message; }; }
struct WeaponCatalog { bool Current()const{return true;} int ProjectileSource(CBaseEntity&)const{return 0;} } catalog;
struct Session { WeaponCatalog* Weapons()const{return &catalog;} } session;
struct CCSOLScenarioPlayerRuntime
{
    Session* m_session=&session;
    bool ReadEntitySource(CBaseEntity&,int&,csol::scenario::ConfigError&);
};
#include "actual_read_source.inc"
bool currentCall=true;
struct Call
{
    CCSOLScenarioMonster* Current()const{return currentCall?&monster:nullptr;}
    CBaseEntity* Inflictor()const{return nullptr;}
} call;
csol::scenario::ConfigError error;
bool Notify(CBasePlayerWeapon* w,bool active)
{
    CBasePlayer* player=&owner;
    const auto current=[](){return currentCall;};
#include "actual_notify_weapon.inc"
    return notifyWeapon(w,active);
}
int checks{};
void Check(bool value) { ++checks; if(!value){std::cerr<<"FAIL native weapon "<<checks<<'\n';std::exit(1);} }
template<class T> void Verify()
{
    T w; Check(CSOL_NativeWeaponMonsterCallbacks(w));
    Check(Notify(&w,true)); Check(Notify(&w,false));
    CCSOLScenarioPlayerRuntime runtime; int source{};
    Check(runtime.ReadEntitySource(w,source,error) && source==w.m_iId);
    const int id=w.m_iId; w.m_iId=725; Check(!CSOL_NativeWeaponMonsterCallbacks(w));
    w.m_iId=0; Check(!CSOL_NativeWeaponMonsterCallbacks(w));
    struct Custom : T {}; Custom custom; custom.m_iId=id;
    Check(!CSOL_NativeWeaponMonsterCallbacks(custom));
}
int main()
{
    Verify<CUSP>(); Verify<CMP5N>(); Verify<CSG552>(); Verify<CAK47>(); Verify<CAUG>(); Verify<CAWP>();
    Verify<CC4>(); Verify<CDEAGLE>(); Verify<CFlashbang>(); Verify<CG3SG1>(); Verify<CGLOCK18>();
    Verify<CHEGrenade>(); Verify<CKnife>(); Verify<CM249>(); Verify<CM3>(); Verify<CM4A1>();
    Verify<CMAC10>(); Verify<CP228>(); Verify<CP90>(); Verify<CSCOUT>(); Verify<CSmokeGrenade>();
    Verify<CTMP>(); Verify<CXM1014>(); Verify<CELITE>(); Verify<CFiveSeven>(); Verify<CUMP45>();
    Verify<CSG550>(); Verify<CGalil>(); Verify<CFamas>();
    CBasePlayerWeapon unknown; unknown.m_iId=WEAPON_AK47; Check(!CSOL_NativeWeaponMonsterCallbacks(unknown));
    error={}; Check(!Notify(&unknown,true) && error.message=="scenario active weapon kill callback is unsupported");
    error={}; Check(!Notify(&unknown,false) && error.message=="scenario source weapon kill callback is unsupported");
    CCSOLScenarioPlayerRuntime runtime; int source{}; error={};
    Check(!runtime.ReadEntitySource(unknown,source,error) && error.message=="scenario source entity has no verified source adapter");
    CUSP mismatched; mismatched.m_pPlayer=nullptr; error={};
    Check(!Notify(&mismatched,true) && error.message=="scenario kill notification weapon owner is invalid");
    Check(Notify(nullptr,true)); unknown.pev->flags=FL_KILLME; Check(Notify(&unknown,true));
    struct CustomCallback : CUSP, CSOLWeaponMonsterKillCallbacks
    {
        int active{},source{}; bool cancel{};
        void ActiveMonsterKilled(CCSOLScenarioMonster&,CBaseEntity*)override{++active;if(cancel)currentCall=false;}
        void SourceMonsterKilled(CCSOLScenarioMonster&,CBaseEntity*)override{++source;}
    } custom;
    Check(Notify(&custom,true) && custom.active==1);
    Check(Notify(&custom,false) && custom.source==1);
    custom.cancel=true; Check(!Notify(&custom,true) && custom.active==2);
    std::cout<<"PASS actual native weapon dispatch: "<<checks<<" checks\n";
}
