// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production TU and real ReGameDLL objects; controlled engine exits.
#include "../csol-scenario-rules-session/sdk_engine.hpp"
#include "../../native/csol/server/weapons/wpn_balrog11.cpp"

namespace regression {
using namespace sdk_test;
unsigned assertions{},failed{},fireEvents{};
int pitch{},yaw{},flameFlag{},pelletFlag{};
float volume{},spread{};
void Expect(bool value,const char* why)
{++assertions;if(!value){++failed;std::cerr<<"ASSERT "<<why<<'\n';}}
bool Near(float a,float b){return std::abs(a-b)<.0001f;}
void Animation(IReGameHook_CBasePlayer_SetAnimation*,CBasePlayer*,PLAYER_ANIM){}
void Prepare(Engine& e)
{
    e.Prepare("0");g_pGameRules=InstallGameRules();
    Require(g_pGameRules&&!CSOL_RulesRejected(),"native rules installed");LinkUserMessages();
    g_engfuncs.pfnInfoKeyValue=[](char*,const char* name)->char*{
        return const_cast<char*>(!std::strcmp(name,"csol_protocol")?"1":"");};
    g_engfuncs.pfnMakeVectors=[](const float*){
        gpGlobals->v_forward=Vector(1,0,0);gpGlobals->v_right=Vector(0,1,0);gpGlobals->v_up=Vector(0,0,1);};
    g_engfuncs.pfnTraceTexture=[](edict_t*,const float*,const float*)->const char*{return nullptr;};
    g_engfuncs.pfnPointContents=[](const float*){return CONTENTS_EMPTY;};
    g_engfuncs.pfnCanSkipPlayer=[](const edict_t*){return 0;};
    // Endpoint-only random stubs never exit FireBuckshots rejection sampling.
    g_engfuncs.pfnRandomFloat=[](float low,float high){return (low+high)*.5f;};
    // Orientation is an observed engine output; flight direction is checked
    // independently from the unchanged launch vector in the real entity.
    g_engfuncs.pfnVecToAngles=[](const float*,float* angles){std::fill_n(angles,3,0.f);};
    g_engfuncs.pfnPlaybackEvent=[](int flags,const edict_t*,unsigned short,float,float*,float*,float a,float b,int c,int d,int f,int p){
        ++fireEvents;volume=a;spread=b;pitch=c;yaw=d;flameFlag=f;pelletFlag=p;
        Expect(flags==FEV_NOTHOST,"actual event suppresses predicting owner");};
    e.onTrace=[](edict_t*,TraceResult&){};
    g_ReGameHookchains.m_CBasePlayer_SetAnimation.registerHook(Animation,192);
}
CBasePlayer* Player(Engine& e)
{
    auto* ed=e.Reserve(1,"player");auto* p=GetClassPtr<CCSPlayer>(reinterpret_cast<CBasePlayer*>(&ed->v));
    ed->v.flags=FL_CLIENT|FL_ONGROUND;ed->v.health=100;ed->v.deadflag=DEAD_NO;ed->v.takedamage=DAMAGE_AIM;
    p->m_iTeam=CT;p->m_iJoiningState=JOINED;p->m_iNumSpawns=1;p->m_bJustConnected=false;p->has_disconnected=false;
    p->m_bCanShoot=true;p->CSPlayer()->m_bCanShootOverride=true;return p;
}
CBalrog11* Gun(CBasePlayer& p)
{
    auto* ed=Engine::Create(Engine::String(csol::balrog11::Classname));
    auto* gun=dynamic_cast<CBalrog11*>(CBaseEntity::Instance(ed));Require(gun,"actual weapon entity factory");
    gun->m_iId=csol::balrog11::Id;gun->CBasePlayerWeapon::Spawn();gun->Precache();
    gun->m_pPlayer=&p;ed->v.owner=p.edict();gun->m_iPrimaryAmmoType=15;gun->m_iSecondaryAmmoType=16;
    p.m_rgpPlayerItems[PRIMARY_WEAPON_SLOT]=gun;p.m_pActiveItem=gun;p.m_rgAmmo[15]=32;
    return gun;
}
void Events(Engine& e)
{
    auto* p=Player(e);auto* gun=Gun(*p);
    for(float recoil:{-12.f,-3.25f,0.f,5.f,11.f})
    {
        gun->m_iClip=7;
        for(int shot=0;shot<4;++shot)
        {
            p->pev->punchangle.x=recoil;const auto before=fireEvents;gun->PrimaryAttack();
            Expect(fireEvents==before+1&&pitch==7&&yaw==0&&flameFlag==0&&pelletFlag==0,
                "real primary wrapper preserves original encoding independently of recoil");
            Expect(Near(spread,csol::balrog11::Spread)&&Near(volume,shot==3?.2f:1.f),"primary charge-volume and spread retained");
        }
        Expect(CSOL_Balrog11Charges(gun)==1&&gun->m_iClip==3,"four shots earn one charge");
        p->pev->punchangle.x=recoil;const auto before=fireEvents;gun->SecondaryAttack();
        Expect(fireEvents==before+1&&pitch==7&&yaw==0&&flameFlag==1&&pelletFlag==0,
            "real secondary wrapper preserves original encoding independently of recoil");
        Expect(CSOL_Balrog11Charges(gun)==0&&gun->m_iClip==3&&Near(volume,1),"secondary consumes only charge");
    }
}
struct Victim final:CBaseEntity
{
    unsigned hits{};bool bsp{};std::function<void()> duringDamage;
    BOOL TakeDamage(entvars_t*,entvars_t*,float,int)override{++hits;if(duringDamage)duringDamage();return TRUE;}
    BOOL IsBSPModel()override{return bsp;}
};
Victim* Target(Engine& e)
{
    auto* ed=e.Reserve(e.nextEntity++,"victim");auto* victim=GetClassPtr<CCSEntity>(reinterpret_cast<Victim*>(&ed->v));
    victim->pev->takedamage=DAMAGE_AIM;return victim;
}
CBalrog11Flame* Shoot(CBasePlayer& owner,const std::shared_ptr<FlameVolley>& volley)
{
    const int index=Engine::self->nextEntity;
    CBalrog11Flame::Shoot(owner,Vector(10,20,30),Vector(.6f,0,.8f),2400,volley);
    auto* flame=dynamic_cast<CBalrog11Flame*>(CBaseEntity::Instance(Engine::At(index)));
    Require(flame,"actual Shoot allocated flame");return flame;
}
void Lifecycle(Engine& e)
{
    auto* p=Player(e);auto volley=std::make_shared<FlameVolley>();
    auto* a=Shoot(*p,volley);auto* b=Shoot(*p,volley);auto* victim=Target(e);
    const Vector launch=a->pev->velocity;
    Expect(launch==Vector(600,0,800)&&a->pev->movetype==MOVETYPE_FLYMISSILE&&a->pev->solid==SOLID_TRIGGER,
        "actual Shoot preserves original speed and trigger movement");
    victim->duringDamage=[&]{a->Touch(victim);b->Touch(victim);};
    a->Touch(victim);b->Touch(victim);
    Expect(victim->hits==1&&volley->hits.size()==1,"all five projectiles and reentrant Touch share one hit record");
    for(const Vector clipped:{Vector(0,0,0),Vector(-20,30,-40),Vector(0,0,800)})
    {
        a->pev->velocity=clipped;e.globals.time+=.02f;a->Think();
        Expect(a->pev->velocity==launch&&!(a->pev->flags&FL_KILLME),"Think restores clipped launch velocity after living-target contact");
        Expect(Near(a->pev->nextthink,e.globals.time+.02f),"original 20ms Think schedule");
    }
    auto* wall=Target(e);wall->bsp=true;a->Touch(wall);
    Expect((a->pev->flags&FL_KILLME)&&!a->m_pfnThink&&!a->m_pfnTouch,"BSP contact retires flame and clears callbacks");
    auto* c=Shoot(*p,std::make_shared<FlameVolley>());e.globals.time+=csol::balrog11::FlameLife;c->Think();
    Expect((c->pev->flags&FL_KILLME)&&!c->m_pfnThink&&!c->m_pfnTouch,"lifetime deadline retires flame");
}
}
int main(int argc,char** argv)
{
    using namespace regression;
    try{
        auto e=std::make_unique<sdk_test::Engine>();Prepare(*e);
        const std::string group=argc>1?argv[1]:"events";
        if(group=="events")Events(*e);else if(group=="lifecycle")Lifecycle(*e);else throw std::runtime_error("unknown group");
        g_ReGameHookchains.m_CBasePlayer_SetAnimation.unregisterHook(Animation);e.reset();
        std::cout<<"balrog11_server behavior="<<assertions<<" failed="<<failed<<" sdk="<<sdk_test::checks<<" sdkFailed="<<sdk_test::failures<<'\n';
        return failed||sdk_test::failures?1:0;
    }catch(const std::exception& error){std::cerr<<"EXCEPTION "<<error.what()<<" checks="<<assertions<<" failures="<<failed<<'\n';return 2;}
}
