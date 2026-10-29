// SPDX-License-Identifier: GPL-3.0-or-later
// Engine services are recording substitutes. Weapon, dispatch, accumulator,
// breakable damage and killer-name methods are extracted unchanged by extract.py.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

using BOOL=int;
constexpr int TRUE=1,FALSE=0;
#define EXT_FUNC
#define __API_HOOK(name) name
#define STRING(value) (value)
#define MAKE_STRING(value) (value)
#define ALLOC_STRING(value) (value)
#define ENT(value) (value)
#define Q_min std::min
#define Q_strncmp std::strncmp
template<std::size_t N> void Q_strlcpy(char (&dst)[N],const char* src){std::snprintf(dst,N,"%s",src);}
constexpr int IN_ATTACK=1,IN_ATTACK2=1<<11,IN_RELOAD=1<<13,EF_MUZZLEFLASH=2,
    FL_ONGROUND=1<<9,FL_DUCKING=1<<14,FL_CLIENT=1<<3,DAMAGE_NO=0,CONTENTS_WATER=-3,
    BRIGHT_GUN_FLASH=512,HITGROUP_GENERIC=0,HITGROUP_HEAD=1,HITGROUP_CHEST=2,
    HITGROUP_STOMACH=3,HITGROUP_LEFTARM=4,HITGROUP_RIGHTARM=5,
    DMG_BULLET=2,DMG_BURN=8,DMG_NEVERGIB=1<<12,DMG_CLUB=1<<7,DMG_POISON=1<<17,
    MSG_ALL=2,ignore_monsters=1,DONT_BLEED=-1,SF_BREAK_CROWBAR=1<<8,
    WEAPON_NOCLIP=-1,ITEM_FLAG_NOFIREUNDERWATER=1<<5,ITEM_FLAG_NOAUTORELOAD=2,
    WPNSTATE_SHIELD_DRAWN=1<<5,DEFAULT_FOV=90,ARMOR_NONE=0,ARMOR_VESTHELM=2;
enum WeaponIdType {WEAPON_NONE,WEAPON_P228,WEAPON_SCOUT=3,WEAPON_XM1014=5,WEAPON_MAC10=7,
    WEAPON_ELITE=10,WEAPON_FIVESEVEN,WEAPON_UMP45=12,WEAPON_GALIL=14,WEAPON_MP5N=19,
    WEAPON_M249=20,WEAPON_M3,WEAPON_TMP=23,WEAPON_DEAGLE=26,WEAPON_AK47=28,WEAPON_P90=30,WEAPON_C4=6};
enum {WEAPON_SECONDARY_ATTACK_NONE,WEAPON_SECONDARY_ATTACK_SET,WEAPON_SECONDARY_ATTACK_BLOCK,PLAYER_ATTACK1};
struct Vector {
    float x{},y{},z{};
    Vector()=default;Vector(float a,float b,float c):x(a),y(b),z(c){}
    float& operator[](int i){return (&x)[i];}float operator[](int i) const{return (&x)[i];}
    Vector operator+(Vector b)const{return {x+b.x,y+b.y,z+b.z};}
    Vector operator-(Vector b)const{return {x-b.x,y-b.y,z-b.z};}
    Vector operator*(float b)const{return {x*b,y*b,z*b};}
    float Length2D()const{return std::sqrt(x*x+y*y);}
    Vector Normalize()const {float l=std::sqrt(x*x+y*y+z*z);return l?*this*(1/l):Vector(0,0,1);}
};
float DotProduct(Vector a,Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct CBaseEntity;struct CBasePlayerWeapon;
struct entvars_t {Vector origin{},v_angle{},punchangle{},velocity{},absmin{},absmax{},size{};
    int button{},effects{},flags{FL_ONGROUND},takedamage{1},waterlevel{},spawnflags{};
    float health{100},ltime{},nextthink{},fov{90};const char *classname{"target"},*viewmodel{},*weaponmodel{};CBaseEntity* owner{};};
using edict_t=CBaseEntity;
struct TraceResult {float flFraction{1};edict_t* pHit{};Vector vecEndPos{};int iHitgroup{7};};
struct DamageRecord {CBaseEntity* target;entvars_t* inflictor;entvars_t* attacker;float damage;int type;};
struct TraceRecord {CBaseEntity* target;entvars_t* attacker;float damage;Vector direction;TraceResult trace;int type;};
struct LosRecord {Vector from,to;int ignore;edict_t* excluded;};
std::vector<DamageRecord> damages;
std::vector<TraceRecord> attacks;
std::vector<LosRecord> traces;
std::vector<CBaseEntity*> entities;
std::vector<int> wireBytes;
std::vector<float> wireCoords;
std::vector<std::uint32_t> wireLongs;
int pointContents{},messageCount{},sphereCalls{},bloodCalls{},penetrationResets{};
bool zombieMode{};
TraceResult losResult;
struct Globals {Vector v_forward{};float time{20};} globals;
Globals* gpGlobals=&globals;
struct CVar {float value{};};
CVar damageMode,refill_bpammo_weapons;
float normalDamage=410,zombieDamage=1150,scenarioDamage=2950,escapeDamage=650;
int effectMessage=77;
struct CBaseEntity {
    entvars_t vars{};entvars_t* pev=&vars;Vector body{};int bodyCalls{};bool player{};
    CBaseEntity(){vars.owner=this;}virtual ~CBaseEntity()=default;
    edict_t* edict(){return this;}int entindex(){return 7;}
    virtual Vector BodyTarget(Vector){++bodyCalls;return body;}
    virtual void TraceAttack(entvars_t*,float,Vector,TraceResult*,int);
    virtual BOOL TakeDamage(entvars_t* inflictor,entvars_t* attacker,float damage,int type){damages.push_back({this,inflictor,attacker,damage,type});pev->health-=damage;return TRUE;}
    int BloodColor(){return DONT_BLEED;}void TraceBleed(float,Vector,TraceResult*,int){}
    void ResetDmgPenetrationLevel(){++penetrationResets;}BOOL IsPlayer(){return player;}
};
template<class T> using EntityHandle=T*;
struct MultiDamage {CBaseEntity* hEntity{};float amount{};int type{};} gMultiDamage;
void ClearMultiDamage();void ApplyMultiDamage(entvars_t*,entvars_t*);void AddMultiDamage(entvars_t*,CBaseEntity*,float,int);
void SpawnBlood(Vector,int,float){++bloodCalls;}
struct RecordingTarget:CBaseEntity {
    void TraceAttack(entvars_t* attacker,float damage,Vector direction,TraceResult* trace,int type)override{
        attacks.push_back({this,attacker,damage,direction,*trace,type});
        CBaseEntity::TraceAttack(attacker,damage,direction,trace,type);
    }
};
struct CBreakable:RecordingTarget {
    bool breakable{true},broken{};int sounds{};float m_flDelay{};
    BOOL IsBreakable(){return breakable;}void Die(){broken=true;pev->takedamage=DAMAGE_NO;}
    void DamageSound(){++sounds;}
    BOOL TakeDamage(entvars_t*,entvars_t*,float,int)override;
};
Vector g_vecAttackDir;
struct CBasePlayer:CBaseEntity {
    std::uint32_t random_seed{0xf1234567u};
    int m_iWeaponVolume{},m_iWeaponFlash{},m_iFOV{90},m_iLastZoom{90},m_rgAmmo[32]{},m_iKevlar{};
    bool m_bResumeZoom{},m_bIsDefusing{},m_bCanShoot{true},m_bCanShootOverride{},shield{};
    float m_flEjectBrass{},m_flNextAttack{};int animations{},tabulates{};
    bool aimCarbine{true},shootCarbine{true};char m_szAnimExtention[32]{};
    CBasePlayerWeapon* m_pActiveItem{};
    CBasePlayer(){player=true;pev->classname="player";pev->flags|=FL_CLIENT;}
    void SetAnimation(int){++animations;}Vector GetGunPosition(){return pev->origin;}
    int LookupSequence(const char* s){return (std::strstr(s,"aim")?aimCarbine:shootCarbine)?5:-1;}
    void TabulateAmmo(){++tabulates;}bool HasShield(){return shield;}CBasePlayer* CSPlayer(){return this;}
    static CBasePlayer* Instance(entvars_t* vars){return vars&&vars->owner&&vars->owner->player?static_cast<CBasePlayer*>(vars->owner):nullptr;}
    const char* GetKillerWeaponName(entvars_t*,entvars_t*)const;BOOL IsArmored(int);
};
const char* GoldCraft_EquipmentWeapon(entvars_t*){return nullptr;}
const char* CSOL_DeathWeapon(entvars_t*){return nullptr;}
struct Rules {bool multiplayer{true},freeze{};bool IsMultiplayer(){return multiplayer;}bool IsFreezePeriod(){return freeze;}} rules;
Rules* g_pGameRules=&rules;
struct CBasePlayerWeapon:CBaseEntity {
    CBasePlayer* m_pPlayer{};int m_iClip{20},m_iId{144},m_iShotsFired{},m_iPrimaryAmmoType{},m_iGlock18ShotsFired{},m_iFamasShotsFired{},m_iWeaponState{},m_iStateSecondaryAttack{};
    float m_flNextPrimaryAttack{},m_flNextSecondaryAttack{},m_flTimeWeaponIdle{},m_flAccuracy{},m_fMaxSpeed{},m_flLastFireTime{},m_flDecreaseShotsFired{},m_flGlock18Shoot{},m_flFamasShoot{};
    bool m_fInReload{},m_fFireOnEmpty{},m_bDelayFire{},ready{true};const char* model_name{};
    int emptySounds{},emptyResets{},reloadCalls{},anim{-1},animSkip{-1},secondaryCalls{},infiniteCalls{};
    std::array<float,7> kick{};
    virtual BOOL CanDeploy(){return ready;}virtual BOOL UseDecrement(){return TRUE;}
    virtual void PrimaryAttack(){}virtual void SecondaryAttack(){++secondaryCalls;}virtual void Reload(){++reloadCalls;}virtual void WeaponIdle(){}
    void PlayEmptySound(){++emptySounds;}void ResetEmptySound(){++emptyResets;}
    void SendWeaponAnim(int a,int skip){anim=a;animSkip=skip;}
    void KickBack(float a,float b,float c,float d,float e,float f,int g){kick={a,b,c,d,e,f,float(g)};}
    BOOL DefaultDeploy(char*,char*,int,char*,int);void ItemPostFrame();bool HasSecondaryAttack();
    void FireRemaining(int,float,int){}void EjectBrassLate(){}void HandleInfiniteAmmo(){++infiniteCalls;}
    int iMaxClip(){return 20;}int PrimaryAmmoIndex(){return 0;}int SecondaryAmmoIndex(){return 1;}
    const char* pszAmmo1(){return "cannon";}const char* pszAmmo2(){return nullptr;}const char* pszName(){return "weapon_cannon";}
    int iFlags(){return 0;}bool IsPistol(){return false;}bool IsUseable(){return m_iClip>0;}bool ShouldWeaponIdle(){return m_flTimeWeaponIdle<=0;}
    float GetNextAttackDelay(float value){return value;}CBasePlayerWeapon* CSPlayerWeapon(){return this;}
};
struct CCannon:CBasePlayerWeapon {void PrimaryAttack()override;BOOL Deploy();void WeaponIdle()override;void SecondaryAttack()override{}void Reload()override{}};
float UTIL_WeaponTimeBase(){return 0;}float GetBaseAccuracy(WeaponIdType){return 0;}
int UTIL_PointContents(Vector){return pointContents;}
bool CSOL_ZombieMode(){return zombieMode;}
void UTIL_MakeVectors(Vector a){constexpr float p=3.14159265358979323846f/180;globals.v_forward={std::cos(a.x*p)*std::cos(a.y*p),std::cos(a.x*p)*std::sin(a.y*p),-std::sin(a.x*p)};}
CBaseEntity* UTIL_FindEntityInSphere(CBaseEntity* previous,Vector,float){++sphereCalls;auto it=previous?std::find(entities.begin(),entities.end(),previous)+1:entities.begin();return it==entities.end()?nullptr:*it;}
void UTIL_TraceLine(Vector from,Vector to,int ignore,edict_t* excluded,TraceResult* out){traces.push_back({from,to,ignore,excluded});*out=losResult;}
void MESSAGE_BEGIN(int kind,int id){if(kind!=MSG_ALL||id!=effectMessage)std::abort();++messageCount;}
void WRITE_BYTE(int value){wireBytes.push_back(value);}void WRITE_COORD(float value){wireCoords.push_back(value);}void MESSAGE_END(){}
void WRITE_LONG(std::uint32_t value){wireLongs.push_back(value);}
std::array<float,3> Point(const Vector& v){return {v.x,v.y,v.z};}
#include "actual.inc"

int checks{},failures{};
void Check(bool condition,const char* label){++checks;if(!condition){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
bool Near(float a,float b,float epsilon=.0002f){return std::abs(a-b)<=epsilon;}
void CheckPoint(Vector a,Vector b,const char* label){Check(Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z),label);}
void Place(CBaseEntity& entity,Vector p,Vector half={4,4,4}){entity.body=p;entity.pev->origin=p;entity.pev->absmin=p-half;entity.pev->absmax=p+half;entity.pev->size=half*2;}
void Reset(){damages.clear();attacks.clear();traces.clear();entities.clear();wireBytes.clear();wireCoords.clear();wireLongs.clear();pointContents=messageCount=sphereCalls=bloodCalls=penetrationResets=0;zombieMode=false;losResult={};damageMode.value=0;normalDamage=410;zombieDamage=1150;scenarioDamage=2950;escapeDamage=650;globals={};globals.time=20;rules={};gMultiDamage={};}
struct Fixture {CBasePlayer shooter;CCannon cannon;RecordingTarget target;
    Fixture(){Reset();cannon.m_pPlayer=&shooter;shooter.m_pActiveItem=&cannon;Place(target,{81,0,0});entities={&target};shooter.pev->button=IN_ATTACK;}
};
void TestFiltering(){
    {Fixture f;f.cannon.PrimaryAttack();Check(damages.size()==1,"front target hit");Check(Near(damages[0].damage,328),"normal linear damage");Check(damages[0].attacker==f.shooter.pev&&damages[0].inflictor==f.shooter.pev,"player attacker and inflictor");Check(damages[0].type==(DMG_BULLET|DMG_NEVERGIB|DMG_BURN),"real multidamage OR burn flags");Check(attacks[0].type==DMG_BURN,"TraceAttack burn argument");Check(attacks[0].trace.pHit==&f.target&&attacks[0].trace.iHitgroup==HITGROUP_GENERIC,"generic hit and explicit target");CheckPoint(attacks[0].trace.vecEndPos,f.target.body,"body endpoint");CheckPoint(attacks[0].direction,{1,0,0},"normalized trace direction");Check(traces[0].ignore==ignore_monsters&&traces[0].excluded==&f.shooter,"LOS excludes shooter and monsters");Check(penetrationResets==1,"real ApplyMultiDamage reset");}
    {Fixture f;entities={&f.shooter,&f.target};f.cannon.PrimaryAttack();Check(damages.size()==1&&f.shooter.bodyCalls==0,"self excluded before body query");}
    {Fixture f;f.target.pev->takedamage=DAMAGE_NO;f.cannon.PrimaryAttack();Check(damages.empty()&&f.target.bodyCalls==0&&traces.empty(),"non-damageable excluded");}
    for(int water=0;water<2;++water)for(int level=0;level<4;++level){Fixture f;pointContents=water?CONTENTS_WATER:0;f.target.pev->waterlevel=level;f.cannon.PrimaryAttack();bool expected=water?level!=0:level!=3;Check((damages.size()==1)==expected,"water boundary matrix");}
    {Fixture f;Place(f.target,{-81,0,0});f.cannon.PrimaryAttack();Check(damages.empty()&&traces.empty(),"rear target excluded before LOS");}
    {Fixture f;Place(f.target,{81,180,0});f.cannon.PrimaryAttack();Check(damages.empty()&&traces.empty(),"outside horizontal envelope");}
    {Fixture f;Place(f.target,{100,80,0},{4,35,4});f.cannon.PrimaryAttack();Check(damages.size()==1,"edge bounds intersect although center outside cone");}
    {Fixture f;Place(f.target,{0,81,0});f.shooter.pev->v_angle.y=90;f.cannon.PrimaryAttack();Check(damages.size()==1,"yaw rotated shot");}
    {Fixture f;f.shooter.pev->punchangle={0,90,0};f.cannon.PrimaryAttack();Check(damages.size()==1,"punch affects visual but not area direction");Check(wireCoords.size()==9&&Near(wireCoords[4],90),"visual punch included in message");}
    {Fixture f;CBaseEntity wall;losResult.flFraction=.25f;losResult.pHit=&wall;f.cannon.PrimaryAttack();Check(damages.empty(),"blocking world or other brush rejected");}
    {Fixture f;losResult.flFraction=.25f;losResult.pHit=&f.target;f.cannon.PrimaryAttack();Check(damages.size()==1,"LOS target brush accepted");CheckPoint(attacks[0].trace.vecEndPos,f.target.body,"accepted brush uses body endpoint");}
    {Fixture f;losResult.flFraction=0;losResult.pHit=nullptr;f.cannon.PrimaryAttack();Check(damages.empty(),"zero trace without target rejected");}
    {Fixture f;RecordingTarget second;Place(second,{162,0,0});entities.push_back(&second);gMultiDamage={&f.shooter,9000,DMG_CLUB};f.cannon.PrimaryAttack();Check(damages.size()==2&&Near(damages[0].damage,328)&&Near(damages[1].damage,246),"per-target accumulator reset prevents old damage leakage");Check(penetrationResets==2,"two independent Apply calls");}
}
void TestDamageModes(){
    const float expected[]{328,920,2360,520,328};
    for(int mode=0;mode<5;++mode){Fixture f;damageMode.value=float(mode);f.cannon.PrimaryAttack();Check(damages.size()==1&&Near(damages[0].damage,expected[mode]),"damage mode selection");}
    for(int mode=0;mode<4;++mode){Fixture f;damageMode.value=float(mode);zombieMode=true;f.cannon.PrimaryAttack();float e=mode==2?2360:mode==3?520:920;Check(Near(damages[0].damage,e),"zombie auto mode with explicit scenario/escape priority");}
    {Fixture f;normalDamage=0;f.cannon.PrimaryAttack();Check(damages.empty()&&traces.empty()&&messageCount==1&&f.cannon.m_iClip==19,"zero configured damage still consumes shot and emits effect");}
    {Fixture f;normalDamage=410.99f;f.cannon.PrimaryAttack();Check(Near(damages[0].damage,328),"property integer conversion");}
    for(float d:{0.f,202.5f,405.f}){Fixture f;Place(f.target,{d,0,0});f.cannon.PrimaryAttack();Check(damages.size()==1&&Near(damages[0].damage,410.f*(1-d/405.f)),"near middle and maximum distance");}
}
void TestDispatch(){
    {Fixture f;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==19&&messageCount==1,"real dispatcher fires primary");Check(Near(f.cannon.m_flNextPrimaryAttack,3.2f)&&Near(f.cannon.m_flNextSecondaryAttack,3.2f)&&Near(f.cannon.m_flTimeWeaponIdle,3.2f),"all fire timers set");f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==19&&messageCount==1,"positive cooldown blocks repeated dispatch");f.cannon.m_flNextPrimaryAttack=.0001f;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==19,"positive epsilon cooldown blocked");f.cannon.m_flNextPrimaryAttack=0;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==18,"zero cooldown fires");}
    {Fixture f;f.shooter.pev->button=IN_ATTACK|IN_ATTACK2;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==20&&messageCount==0,"dual buttons blocked by native secondary dispatch");f.shooter.pev->button=IN_ATTACK|IN_ATTACK2;f.cannon.m_flNextSecondaryAttack=1;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==20,"dual buttons blocked inside PrimaryAttack when secondary cooling");}
    for(int clip:{0,-1}){Fixture f;f.cannon.m_iClip=clip;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==clip&&f.cannon.emptySounds==1&&messageCount==0&&damages.empty(),"empty attack no effect or damage");Check(Near(f.cannon.m_flNextPrimaryAttack,.4f)&&Near(f.cannon.m_flNextSecondaryAttack,.4f),"empty cooldown .4");}
    {Fixture f;rules.freeze=true;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==20,"freeze period blocks attack");}
    {Fixture f;f.shooter.m_bIsDefusing=true;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==20,"defusing blocks attack");}
    {Fixture f;f.shooter.pev->waterlevel=3;pointContents=CONTENTS_WATER;f.target.pev->waterlevel=3;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==19&&damages.size()==1,"flags0 permits underwater shot");}
    {Fixture f;f.cannon.m_iClip=3;f.shooter.m_rgAmmo[0]=17;f.shooter.pev->button=IN_RELOAD;f.cannon.m_flNextPrimaryAttack=-.1f;f.cannon.ItemPostFrame();Check(f.cannon.m_iClip==3&&f.shooter.m_rgAmmo[0]==17&&!f.cannon.m_fInReload,"native reload dispatch cannot create magazine ammo");}
    for(int missing=0;missing<4;++missing){Fixture f;f.shooter.aimCarbine=(missing&1)==0;f.shooter.shootCarbine=(missing&2)==0;f.cannon.m_flNextPrimaryAttack=2.9f;f.cannon.m_flNextSecondaryAttack=2.9f;f.cannon.m_bDelayFire=true;Check(f.cannon.Deploy(),"deploy succeeds");Check(std::string(f.shooter.m_szAnimExtention)==(missing?"m249":"carbine"),"carbine needs both sequences otherwise m249");Check(Near(f.cannon.m_flNextPrimaryAttack,2.9f)&&Near(f.cannon.m_flNextSecondaryAttack,2.9f),"actual DefaultDeploy preserves shot cooldown");Check(f.cannon.anim==2&&Near(f.shooter.m_flNextAttack,.75f)&&Near(f.cannon.m_flTimeWeaponIdle,1.5f),"draw sequence and native deploy timers");Check(Near(f.cannon.m_fMaxSpeed,235)&&f.cannon.m_iShotsFired==0&&!f.cannon.m_fInReload&&!f.cannon.m_bDelayFire,"deploy weapon state");}
    {Fixture f;f.cannon.ready=false;Check(!f.cannon.Deploy()&&f.cannon.anim==-1,"native deploy respects readiness");}
    {Fixture f;f.cannon.m_flTimeWeaponIdle=.01f;f.cannon.WeaponIdle();Check(f.cannon.anim==-1&&f.cannon.emptyResets==1,"idle does not interrupt positive timer");f.cannon.m_flTimeWeaponIdle=0;f.cannon.WeaponIdle();Check(f.cannon.anim==0&&Near(f.cannon.m_flTimeWeaponIdle,20),"idle sequence and 20 second delay");}
}
void TestEffectsAndRecoil(){
    const std::array<std::array<float,7>,4> expected{{{13,5,1.85f,.55f,15,5.7f,2},{13,2.25f,1.45f,.7f,12,10,1},{9,2.1f,1.25f,.5f,15,5.5f,1},{13,3.2f,1.5f,.5f,15,10,2}}};
    for(int posture=0;posture<4;++posture){Fixture f;f.shooter.pev->origin={1,2,3};f.shooter.pev->flags=posture==0?0:FL_ONGROUND;if(posture==2)f.shooter.pev->flags|=FL_DUCKING;if(posture==1)f.shooter.pev->velocity={50,0,3};f.cannon.PrimaryAttack();Check(f.cannon.kick==expected[posture],"posture kickback parameters");Check(f.shooter.m_iWeaponVolume==2048&&f.shooter.m_iWeaponFlash==BRIGHT_GUN_FLASH&&(f.shooter.pev->effects&EF_MUZZLEFLASH),"volume flash contract");Check(f.shooter.animations==1&&f.cannon.anim==1&&f.cannon.animSkip==TRUE,"player and view attack animation");Check(wireBytes==std::vector<int>({2,7,posture==2?1:0})&&wireCoords.size()==9,"version owner duck and nine coords");Check(wireLongs==std::vector<std::uint32_t>{0xf1234567u}&&wireBytes.size()+2*wireCoords.size()+4*wireLongs.size()==csol::cannon::EffectBytes,"version2 exact 25-byte contract and full command seed");Check(Near(wireCoords[0],1)&&Near(wireCoords[1],2)&&Near(wireCoords[2],3),"effect origin");Check(Near(wireCoords[6],posture==1?50.f:0.f)&&Near(wireCoords[8],posture==1?3.f:0.f),"effect velocity");}
}
void TestGlassAndKiller(){
    for(bool breakable:{false,true}){Fixture f;CBreakable glass;Place(glass,{81,0,0});glass.pev->health=100;glass.breakable=breakable;entities={&glass};losResult.flFraction=.5f;losResult.pHit=&glass;f.cannon.PrimaryAttack();Check(glass.broken==breakable,"real glass TakeDamage breakability respected");Check(Near(glass.pev->health,breakable?-228.f:100.f),"real glass receives unscaled burn/bullet damage");if(breakable)Check(Near(glass.pev->nextthink,.1f)&&glass.sounds==0,"glass die schedules think and avoids damage sound");}
    {Fixture f;CBreakable glass;Place(glass,{81,0,0});glass.pev->health=1000;entities={&glass};f.cannon.PrimaryAttack();Check(!glass.broken&&Near(glass.pev->health,672)&&glass.sounds==1,"unbroken glass takes damage and shard sound");}
    {Fixture f;CBasePlayer victim;Check(std::string(victim.GetKillerWeaponName(f.shooter.pev,f.shooter.pev))=="cannon","real kill name resolves active cannon");Check(std::string(f.shooter.GetKillerWeaponName(f.shooter.pev,f.shooter.pev))=="world","self kill does not borrow current weapon");victim.m_iKevlar=1;Check(victim.IsArmored(HITGROUP_GENERIC),"generic cannon hit is armored");victim.m_iKevlar=0;Check(!victim.IsArmored(HITGROUP_GENERIC),"no vest no armor");}
}
int main(){TestFiltering();TestDamageModes();TestDispatch();TestEffectsAndRecoil();TestGlassAndKiller();std::printf("%s cannon server %d checks; %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;}
