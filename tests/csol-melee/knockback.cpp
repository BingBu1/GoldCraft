// SPDX-License-Identifier: GPL-3.0-or-later
// Production predicates are extracted verbatim. Entity access, lease validity,
// vectors and monster state are controlled boundaries, not the SDK ABI.
#include "native/csol/melee_identity.hpp"
#include "native/csol/melee_resources.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <typeinfo>

struct Vector {
    float x{},y{},z{};
    Vector()=default; Vector(float a,float b,float c):x(a),y(b),z(c){}
    Vector operator-(Vector b)const{return {x-b.x,y-b.y,z-b.z};}
    Vector operator*(float f)const{return {x*f,y*f,z*f};}
};
struct Vars { int flags{}; const char* classname=""; Vector origin{},velocity{2,3,4}; };
struct CBaseEntity { Vars storage,*pev=&storage; virtual ~CBaseEntity()=default; };
struct CBasePlayerWeapon:CBaseEntity {int m_iId{};};
struct CDragonSword final:CBasePlayerWeapon {};
struct CSkullAxe final:CBasePlayerWeapon {};
struct CBasePlayer:CBaseEntity {CBaseEntity* m_pActiveItem{};};
struct EHANDLE {
    CBaseEntity* value{};
    template<class T>T* Get()const{return dynamic_cast<T*>(value);}
};
struct Monster:CBaseEntity {
    struct Kind {int priority{};} type;
    struct Host {bool knockbackBlocked{};int lastHitgroup{};} host;
    struct Speed {float recovery=.73f;} speed;
    Kind* Type(){return &type;} Host& HostState(){return host;} Speed& SpeedState(){return speed;}
};
struct CSOLMonsterDamageCall {
    Monster* monster{};
    struct Context {unsigned char bytes[8]{};} context;
    Monster* Current(){return monster;}
};
namespace csol::scenario {struct ConfigError {std::string message;};}
constexpr int FL_KILLME=1<<30,FL_ONGROUND=1<<9,FL_DUCKING=1<<14;
constexpr int HITGROUP_LEFTLEG=6,HITGROUP_RIGHTLEG=7;
bool FClassnameIs(const Vars* p,const char* name){return std::strcmp(p->classname,name)==0;}
#include "knockback_constants.inc"
#include "knockback_identity.inc"
namespace candidate {
#include "knockback_new.inc"
}
namespace previous {
#include "knockback_old.inc"
}
int checks{},failures{};
void Check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
int main()
{
    using namespace csol::melee;
    Monster monster;CBasePlayer player;CSOLMonsterDamageCall call{&monster};
    csol::scenario::ConfigError error;EHANDLE actor{&player};
    CDragonSword dragon;dragon.m_iId=DragonSwordId;dragon.pev->classname=DragonSwordResources.classname;
    CSkullAxe skull;skull.m_iId=SkullAxeId;skull.pev->classname=SkullAxeResources.classname;
    int oldRejections{};
    for(auto* weapon:{static_cast<CBasePlayerWeapon*>(&dragon),static_cast<CBasePlayerWeapon*>(&skull)}) {
        player.m_pActiveItem=weapon;error.message.clear();
        Check(candidate::ApplyOrdinaryKnockback(call,actor,error),"compiled melee type accepted for ordinary nonlethal hit");
        Check(error.message.empty(),"ordinary hit is not converted to unsupported damage failure");
        Check(monster.pev->velocity.x==2&&monster.pev->velocity.y==3&&monster.pev->velocity.z==4&&monster.speed.recovery==.73f,
            "original false predicate preserves velocity and speed recovery");
        error.message.clear();
        if(!previous::ApplyOrdinaryKnockback(call,actor,error)&&!error.message.empty())++oldRejections;
        CBasePlayerWeapon impostor;impostor.m_iId=weapon->m_iId;impostor.pev->classname=weapon->pev->classname;
        player.m_pActiveItem=&impostor;error.message.clear();
        Check(!candidate::ApplyOrdinaryKnockback(call,actor,error),"matching name and id cannot spoof compiled melee type");
        player.m_pActiveItem=weapon;const auto name=weapon->pev->classname;weapon->pev->classname="weapon_wrong";
        Check(!candidate::ApplyOrdinaryKnockback(call,actor,error),"wrong classname is rejected");weapon->pev->classname=name;
        const auto id=weapon->m_iId;weapon->m_iId=8191;
        Check(!candidate::ApplyOrdinaryKnockback(call,actor,error),"wrong bridge identity is rejected");weapon->m_iId=id;
    }
    Check(oldRejections==2,"previous production rejects both real melee identities");
    CBasePlayerWeapon cannon;cannon.m_iId=CSOL_WEAPON_CANNON;player.m_pActiveItem=&cannon;
    monster.pev->flags=FL_ONGROUND;monster.pev->origin={20,0,0};
    Check(candidate::ApplyOrdinaryKnockback(call,actor,error)&&monster.pev->velocity.x==1100&&monster.pev->velocity.y==0&&
        monster.pev->velocity.z==4&&monster.speed.recovery==.45f,"existing cannon applies real ordinary impulse branch");
    player.m_pActiveItem=&dragon;call.monster=nullptr;
    Check(!candidate::ApplyOrdinaryKnockback(call,actor,error),"lost monster lease still rejects");
    std::printf("ordinary melee checks=%d failures=%d previousRejections=%d\n",checks,failures,oldRejections);
    return failures?1:0;
}
