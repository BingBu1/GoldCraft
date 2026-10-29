// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "server/weapon_damage.h"
#include <iostream>
#include <fstream>
#include <source_location>
#include <cstdlib>
#ifndef CSOL_BEAM_ENTITY_SOURCE
#define CSOL_BEAM_ENTITY_SOURCE "../../native/csol/server/wpn_beamgun.cpp"
#endif
#include CSOL_BEAM_ENTITY_SOURCE

CCSOLBeamDamageConfig config;
bool zombieHuman{};
bool CSOL_ZombieHuman(CBasePlayer* player){return zombieHuman&&player&&player->IsAlive();}
bool CSOL_BeamDamageReady(){return damageReady;}
float CSOL_BeamDamage(CSOLBeamVariant variant,CSOLDamageMode mode,CSOLBeamDamage kind){return config.Get(variant,mode,kind);}
bool CSOL_IsWeapon(CBasePlayerWeapon* weapon){return weapon&&csol::BeamWeaponId(weapon->m_iId);}
ItemInfo infos[2];
const ItemInfo* CSOL_ItemInfo(int id){return &infos[id==726];}
void UTIL_PrecacheOtherWeapon(const char* name){CBeamGun gun;gun.pev->classname=name;gun.Precache();gun.GetItemInfo(&infos[FClassnameIs(gun.pev,"weapon_beamgunle")]);}
int checks;
void Check(bool value,const std::source_location at=std::source_location::current())
{++checks;if(!value){std::cerr<<"FAIL beam entity line "<<at.line()<<'\n';std::exit(1);}}
// Model the engine boundary: active input first, then one decay for each
// inventory weapon (player.cpp pt_end). Advance owns only extension timers.
void Tick(CBasePlayer& owner,CBeamGun& gun,float dt,int buttons=0)
{
    owner.pev->button=buttons;
    if(owner.m_pActiveItem==&gun&&owner.m_flNextAttack<=0)gun.ItemPostFrame();
    gun.m_flNextPrimaryAttack=std::max(gun.m_flNextPrimaryAttack-dt,-1.f);
    gun.m_flNextSecondaryAttack=std::max(gun.m_flNextSecondaryAttack-dt,-.001f);
    gun.m_flTimeWeaponIdle=std::max(gun.m_flTimeWeaponIdle-dt,-.001f);
    gpGlobals->time+=dt;gun.Advance(dt);
    owner.m_flNextAttack=std::max(owner.m_flNextAttack-dt,-.001f);
}
void SwitchCooldowns()
{
    for(const char* name:{"weapon_beamgun","weapon_beamgunle"})
    for(const bool dive:{false,true})
    {
        CBasePlayer owner;entities={&owner};effects.clear();gpGlobals->time=10;
        CBeamGun gun;gun.pev->classname=name;gun.Spawn();Check(gun.AddToPlayer(&owner));
        owner.m_pActiveItem=&gun;Check(gun.Deploy());
        Check(gun.m_flNextPrimaryAttack==.1f&&gun.m_flNextSecondaryAttack==.1f);
        Tick(owner,gun,.11f);owner.m_rgAmmo[2]=100;
        owner.m_afButtonPressed=IN_ATTACK|IN_ATTACK2;Tick(owner,gun,.01f);
        owner.m_afButtonPressed=0;Tick(owner,gun,.31f);
        const int input=dive?IN_ATTACK2:IN_ATTACK;
        Tick(owner,gun,.01f,input);
        const int mode=dive?4:3, phase=dive?1:0;
        const auto attacks=[&]{return std::count_if(effects.begin(),effects.end(),[&](auto& e){return e.mode==mode&&e.phase==phase;});};
        Check(attacks()==1);
        const float deadline=gpGlobals->time+gun.m_flNextPrimaryAttack;
        for(int cycle=0;cycle<3;++cycle)
        {
            gun.Holster();owner.m_pActiveItem=nullptr;Tick(owner,gun,.02f);
            const float remaining=gun.m_flNextPrimaryAttack;
            Check(gun.Deploy());owner.m_pActiveItem=&gun;
            Check(std::abs(gun.m_flNextPrimaryAttack-remaining)<1e-6f);
            Tick(owner,gun,.11f);Tick(owner,gun,.01f,input);
            Check(attacks()==1);
            Check(std::abs(gpGlobals->time+gun.m_flNextPrimaryAttack-deadline)<1e-4f);
        }
        while(gun.m_flNextPrimaryAttack>0)Tick(owner,gun,.01f,input);
        Check(attacks()==1);Tick(owner,gun,.01f,input);Check(attacks()==2);
        gun.Holster();owner.m_pActiveItem=nullptr;Tick(owner,gun,2.f);
        Check(gun.Deploy()&&gun.m_flNextPrimaryAttack==.1f); // No stale cooldown.
        gun.m_flNextPrimaryAttack=.7f;gun.m_flNextSecondaryAttack=.4f;
        deployReady=false;Check(!gun.Deploy());deployReady=true;
        Check(gun.m_flNextPrimaryAttack==.7f&&gun.m_flNextSecondaryAttack==.4f);
        Check(gun.Deploy()&&gun.m_flNextPrimaryAttack==.7f&&gun.m_flNextSecondaryAttack==.4f);
    }
    // Expiry after cancelling a near-expiry dive must not create a second
    // way to shorten the surviving attack timers on either predictor/server.
    csol::BeamWeaponState s;s.charge=csol::BeamCharge::Alpha;s.chargeTime=.2f;
    Check(s.Secondary().attack==csol::BeamAttack::DiveStart);
    s.Holster();s.Decrement(.15f);s.nextPrimary-=.15f;s.nextSecondary-=.15f;
    Check(s.BeginDeploy()==csol::BeamChargeDraw);s.FinishDeploy(true);
    s.Decrement(.1f);s.nextPrimary-=.1f;s.nextSecondary-=.1f;
    const float remaining=s.nextPrimary;
    Check(s.ExpireCharge().animation==csol::BeamChargeExit);
    Check(s.nextPrimary==remaining&&s.nextSecondary==remaining&&!s.Charged());
}
int main(int argc,char** argv)
{
    std::ifstream file(argv[1]);std::string json{std::istreambuf_iterator<char>(file),{}};
    std::string error;Check(config.Parse(json,error));
    CSOL_InitBeamWeapons();CSOL_PrecacheBeamWeapons();
    SwitchCooldowns();
    for(const char* name:{"weapon_beamgun","weapon_beamgunle"})
    {
        CBasePlayer owner,target;owner.index=1;target.index=2;target.m_iTeam=2;target.pev->origin={60,0,28};
        entities={&owner,&target};hits.clear();effects.clear();gpGlobals->time=10;damageMode.value=0;
        CBeamGun gun;gun.pev->classname=name;gun.Spawn();ItemInfo info;gun.GetItemInfo(&info);
        Check(!gun.removed&&info.iMaxClip==80&&info.iMaxAmmo1==200&&info.iSlot==0&&info.iWeight==20&&gun.GetMaxSpeed()==245);
        Check(gun.AddToPlayer(&owner)==1);owner.m_pActiveItem=&gun;gun.m_iClip=80;owner.m_rgAmmo[1]=200;
        Check(gun.Deploy()&&lastAnimation==csol::BeamDraw);
        Check(!std::strcmp(owner.m_szAnimExtention,"m249"));
        owner.skull8=true;++owner.pev->modelindex;gun.Advance(0);
        Check(!std::strcmp(owner.m_szAnimExtention,"skull8"));
        owner.skull8=false;++owner.pev->modelindex;gun.Advance(0);
        Check(!std::strcmp(owner.m_szAnimExtention,"m249"));
        gun.PrimaryAttack();
        Check(gun.m_iClip==79&&lastAnimation==csol::BeamShoot&&bulletDamage==40);
        Check((lastTraceStart-owner.GetGunPosition()).Length()==0);
        weapon_data_t wire;wire.m_iId=gun.m_iId;CSOL_PackBeamWeapon(&gun,&wire);
        Check(wire.m_iId==info.iId&&wire.m_iClip==79&&wire.m_flNextPrimaryAttack==.3f);
        gun.Reload();Check(gun.m_fInReload&&lastAnimation==csol::BeamReload);
        owner.m_flNextAttack=0;gun.ItemPostFrame();Check(gun.m_iClip==80&&owner.m_rgAmmo[1]==199);
        gun.m_iClip=70;gun.Reload();owner.m_flNextAttack=0;rules.freeze=true;gun.ItemPostFrame();rules.freeze=false;
        Check(gun.m_iClip==80&&owner.m_rgAmmo[1]==189&&!gun.m_fInReload);
        gun.SecondaryAttack();Check(lastAnimation==csol::BeamShootB1);
        gun.Advance(.21f);gun.ItemPostFrame();Check(!hits.empty()&&hits.back().target==&target);
        Check(hits.back().attacker==owner.pev&&hits.back().inflictor==gun.pev);
        Check(std::none_of(hits.begin(),hits.end(),[&](const Hit& hit){return hit.target==&owner;}));
        const int previous=int(hits.size());gun.ItemPostFrame();Check(int(hits.size())==previous);
        gun.SecondaryAttack();gun.Advance(.31f);gun.ItemPostFrame();gun.Pack(&wire);
        Check(csol::ReadBeamPrediction(wire).charge==csol::BeamCharge::Alpha);
        const int clip=gun.m_iClip;gun.PrimaryAttack();Check(gun.m_iClip==clip&&lastAnimation==csol::BeamChargeShoot);
        gun.Holster();owner.m_pActiveItem=nullptr;gun.Advance(.25f);gun.Pack(&wire);
        Check(csol::ReadBeamPrediction(wire).charge==csol::BeamCharge::Alpha);
        Check(gun.Deploy()&&lastAnimation==csol::BeamChargeDraw);owner.m_pActiveItem=&gun;
        CSOL_RemoveBeamWeapon(&gun);gun.Pack(&wire);Check(csol::ReadBeamPrediction(wire).charge==csol::BeamCharge::None);
        owner.m_rgAmmo[2]=100;gun.Deploy();owner.m_afButtonPressed=IN_ATTACK|IN_ATTACK2;
        gun.ItemPostFrame();gun.Pack(&wire);Check(wire.iuser1==0&&csol::ReadBeamPrediction(wire).charge==csol::BeamCharge::Beta);
        owner.m_afButtonPressed=0;gun.ItemPostFrame();Check(wire.iuser1==0);
        gun.pev->body=1;gun.Holster();owner.m_pActiveItem=nullptr;gun.Advance(1);gun.Pack(&wire);
        Check(csol::ReadBeamPrediction(wire).charge==csol::BeamCharge::Beta&&wire.fuser1==6);
        Check(gun.Deploy()&&lastAnimation==csol::BeamChargeDraw&&gun.pev->body==1&&gun.pev->skin==0);
        owner.m_pActiveItem=&gun;
        owner.pev->button=IN_FORWARD;gun.SecondaryAttack();Check(owner.pev->velocity.x==250&&owner.pev->velocity.z==350&&!(owner.pev->flags&FL_ONGROUND));
        gun.Advance(.41f);gun.ItemPostFrame();Check(owner.pev->velocity.z==-500);
        owner.pev->flags|=FL_ONGROUND;gun.ItemPostFrame();Check(owner.pev->velocity.Length()==0);
        gun.ItemPostFrame();Check(lastAnimation==csol::BeamDiveEnd);
        gpGlobals->time+=7.1f;gun.Advance(7.1f);gun.ItemPostFrame();gun.Advance(.01f);Check(owner.m_rgAmmo[2]==5);
        gun.Holster();owner.m_pActiveItem=nullptr;gpGlobals->time+=10;gun.Advance(1);Check(owner.m_rgAmmo[2]==5);
        gun.Deploy();owner.m_pActiveItem=&gun;blocked=true;hits.clear();gun.SecondaryAttack();gun.Advance(.21f);gun.ItemPostFrame();Check(hits.empty());blocked=false;
        for(int gate=0;gate<3;++gate){owner.alive=gate!=0;owner.m_bCanShoot=gate!=1;rules.freeze=gate==2;
            const int before=gun.m_iClip;gun.PrimaryAttack();Check(gun.m_iClip==before);}
        rules.freeze=false;owner.alive=owner.m_bCanShoot=true;gun.Holster();
        owner.m_rgAmmo[2]=100;gun.Deploy();owner.m_afButtonPressed=IN_ATTACK|IN_ATTACK2;gun.ItemPostFrame();
        owner.m_afButtonPressed=0;gun.SecondaryAttack();owner.alive=false;gun.Advance(.01f);gun.Pack(&wire);
        Check(csol::ReadBeamPrediction(wire).dive==csol::BeamDive::None&&effects.back().mode==4&&effects.back().phase==0);
        owner.alive=true;gun.Holster();
        damageMode.value=1;gun.Deploy();gpGlobals->time+=1;hits.clear();effects.clear();gun.Advance(.01f);
        Check(hits.size()==4&&hits.front().damage==75&&effects.back().count==2);
        target.pev->origin={250,0,28};gpGlobals->time+=1;hits.clear();gun.Advance(.01f);Check(hits.size()==1&&hits.front().damage==100);
        target.m_iTeam=owner.m_iTeam;gpGlobals->time+=1;hits.clear();gun.Advance(.01f);Check(hits.empty());
        target.m_iTeam=2;target.alive=false;gpGlobals->time+=1;gun.Advance(.01f);Check(hits.empty());
        // A different JSON changes the actual authoritative bullet call.
        const auto original=json;auto changed=json;const auto offset=changed.find("\"ShootADamage\": 40");
        Check(offset!=std::string::npos);changed.replace(offset,18,"\"ShootADamage\": 73");
        Check(config.Parse(changed,error));damageMode.value=0;gun.PrimaryAttack();Check(bulletDamage==73);
        Check(config.Parse(original,error));
        changed=original;const auto overrideAt=changed.find("\"beamgunle\": {}");
        Check(overrideAt!=std::string::npos);changed.replace(overrideAt,15,"\"beamgunle\": {\"ShootADamage\": 119}");
        Check(config.Parse(changed,error));gun.PrimaryAttack();Check(bulletDamage==(gun.m_iId==726?119:40));
        Check(config.Parse(original,error));
        // Human-observed cadence: small blasts at 2/4/6/8; shot 10 replaces
        // the small blast with the large blast. Exercise actual damage calls.
        gun.Holster();gun.Deploy();gpGlobals->time+=6;target.alive=true;
        target.pev->origin=primaryImpactPoint={60,0,28};primaryImpactEnabled=true;
        for(const int expected:{0,1,0,1,0,1,0,1,0,4})
        {
            gpGlobals->time+=.31f;hits.clear();gun.PrimaryAttack();
            Check(effects.back().mode==0&&effects.back().phase==expected);
            Check(hits.size()==(expected?1u:0u));
            if(expected)
            {
                const auto variant=gun.m_iId==726?CSOLBeamVariant::BeamgunLE:CSOLBeamVariant::Beamgun;
                const auto kind=expected==4?CSOLBeamDamage::ShootAFullExplosion:CSOLBeamDamage::ShootAExplosion;
                Check(hits.front().damage==config.Get(variant,CSOLDamageMode::Normal,kind));
            }
        }
        gpGlobals->time+=6;hits.clear();gun.PrimaryAttack();Check(hits.empty()&&effects.back().phase==0);
        gun.PrimaryAttack();Check(hits.size()==1&&effects.back().phase==1);
        primaryImpactEnabled=false;
        // A switch keeps charge while removal, a new owner and death do not.
        // Exercise the production adapters, including inactive countdowns.
        owner.m_afButtonPressed=0;owner.m_pActiveItem=&gun;
        const auto alpha=[&]{gun.Removed();gun.SecondaryAttack();gun.Advance(.21f);gun.ItemPostFrame();
            gun.SecondaryAttack();gun.Advance(.31f);gun.ItemPostFrame();gun.Pack(&wire);
            Check(csol::ReadBeamPrediction(wire).charge==csol::BeamCharge::Alpha);};
        for(int mode:{0,1,2})
        {damageMode.value=float(mode);alpha();Check(effects.back().mode==2&&effects.back().phase==mode);}
        damageMode.value=0;
        alpha();gun.Holster();owner.m_pActiveItem=nullptr;gun.Advance(2.5f);
        Check(gun.Deploy()&&lastAnimation==csol::BeamChargeDraw); // Exactly zero is still valid.
        gun.Holster();gun.Advance(.01f);Check(gun.Deploy()&&lastAnimation==csol::BeamDraw);
        owner.m_pActiveItem=&gun;alpha();CSOL_RemoveBeamWeapon(&gun);gun.Pack(&wire);
        Check(!csol::ReadBeamPrediction(wire).Charged()&&wire.fuser1==0);
        alpha();gun.Drop();gun.Pack(&wire);Check(!csol::ReadBeamPrediction(wire).Charged()&&gun.removed);
        alpha();gun.Kill();gun.Pack(&wire);Check(!csol::ReadBeamPrediction(wire).Charged());
        alpha();CBasePlayer newOwner;Check(gun.AddToPlayer(&newOwner)==1);
        gun.Pack(&wire);Check(!csol::ReadBeamPrediction(wire).Charged());
        CBasePlayerItem ordinary;CSOL_RemoveBeamWeapon(&ordinary);CSOL_RemoveBeamWeapon(nullptr);
    }
    damageReady=false;CBeamGun unavailable;unavailable.Spawn();Check(unavailable.removed);
    damageReady=true;
    for(const char* name:{"weapon_beamgun","weapon_beamgunle"})
    {
        CBasePlayer owner;entities={&owner};CBeamGun gun;gun.pev->classname=name;
        gun.Spawn();gun.AddToPlayer(&owner);owner.m_pActiveItem=&gun;
        auto beta=[&] {
            gun.Removed();gun.Deploy();owner.m_rgAmmo[2]=100;
            owner.m_afButtonPressed=IN_ATTACK|IN_ATTACK2;gun.ItemPostFrame();owner.m_afButtonPressed=0;
        };
        gpGlobals->time=100;zombieHuman=false;beta();Check(!CSOL_BeamGuardBlocks(&owner,50));
        zombieHuman=true;beta();Check(CSOL_BeamGuardBlocks(&owner,50));
        Check(CSOL_BeamInflictor(gun.pev));
        CSOL_UpdateBeamGuardHUD(&owner);Check(message==std::vector<std::byte>{std::byte{1},std::byte{0xe8},std::byte{3}});
        Check(!CSOL_BeamGuardBlocks(&owner,0)&&!CSOL_BeamGuardBlocks(&owner,-1));
        gun.Holster();owner.m_pActiveItem=nullptr;gpGlobals->time=100.999f;
        Check(CSOL_BeamGuardBlocks(&owner,9999));
        message.clear();CSOL_UpdateBeamGuardHUD(&owner);Check(message.empty());
        gun.Deploy();owner.m_pActiveItem=&gun;gpGlobals->time=101;
        Check(!CSOL_BeamGuardBlocks(&owner,50)); // exact deadline, not beta's seven seconds
        CSOL_UpdateBeamGuardHUD(&owner);Check(message==std::vector<std::byte>{std::byte{1},std::byte{0},std::byte{0}});
        gun.SecondaryAttack();Check(CSOL_BeamGuardBlocks(&owner,50));
        gpGlobals->time=101.5f;gun.Advance(.5f);gun.ItemPostFrame();
        Check(CSOL_BeamGuardBlocks(&owner,50));
        gpGlobals->time=102;gun.Advance(.5f);gun.ItemPostFrame();
        Check(!CSOL_BeamGuardBlocks(&owner,50)); // loop/landing never renew protection
        beta();gun.Removed();Check(!CSOL_BeamGuardBlocks(&owner,50));
        CSOL_UpdateBeamGuardHUD(&owner);
        beta();gun.Drop();Check(!CSOL_BeamGuardBlocks(&owner,50));
        beta();gun.Kill();Check(!CSOL_BeamGuardBlocks(&owner,50));
        beta();CSOL_ResetBeamGuard(&owner);Check(!CSOL_BeamGuardBlocks(&owner,50));
        beta();owner.alive=false;Check(!CSOL_BeamGuardBlocks(&owner,50));
        gun.Advance(.01f);owner.alive=true;Check(!CSOL_BeamGuardBlocks(&owner,50));
        beta();zombieHuman=false;Check(!CSOL_BeamGuardBlocks(&owner,50));
        zombieHuman=true;CSOL_ResetBeamGuard(&owner);
        Check(!CSOL_BeamGuardBlocks(&owner,50));
        beta();CBasePlayer recipient;gun.AddToPlayer(&recipient);
        Check(!CSOL_BeamGuardBlocks(&recipient,50));
    }
    entvars_t ordinary;ordinary.classname="grenade";Check(!CSOL_BeamInflictor(&ordinary)&&!CSOL_BeamInflictor(nullptr));
    std::cout<<"PASS production beam entity "<<checks<<" checks (engine services are fixtures)\n";
}
