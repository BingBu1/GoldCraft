// SPDX-License-Identifier: GPL-3.0-or-later
// Production transaction class is extracted verbatim. Engine/entity services
// below are controlled substitutes, including serial identity and hook failures.
#include <array>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
struct CBaseEntity;struct CBasePlayer;struct CBasePlayerItem;struct CBasePlayerWeapon;struct CWeaponBox;
using WeaponIdType=int;
constexpr int MAX_ITEM_TYPES=6,PRIMARY_WEAPON_SLOT=1,CSOL_WEAPON_CANNON=144,SF_NORESPAWN=1,FL_KILLME=1<<30,HIDEHUD_WEAPONS=1;
#define STRING(value) (value)
struct Vec {float x{},y{},z{};};
struct edict_t;
struct Vars {Vec origin{};int flags{},spawnflags{};const char* classname="";edict_t* owner{};};
struct edict_t {Vars v{};bool free{};unsigned serial=1;CBaseEntity* object{};};
struct CBaseEntity {
    edict_t record{};Vars* pev=&record.v;
    CBaseEntity(){record.object=this;}virtual ~CBaseEntity()=default;
    edict_t* edict(){return &record;}
};
struct EHANDLE {
    edict_t* value{};unsigned serial{};
    void Set(edict_t* e){value=e;serial=e?e->serial:0;}
    edict_t* Get(){return value && !value->free && value->serial==serial?value:nullptr;}
    EHANDLE& operator=(CBaseEntity* e){Set(e?e->edict():nullptr);return *this;}
    operator CBaseEntity*(){auto* e=Get();return e?e->object:nullptr;}
};
struct CBasePlayerItem:CBaseEntity {CBasePlayerItem* m_pNext{};CBasePlayer* m_pPlayer{};int id=144,slot=1;int iItemSlot(){return slot;}virtual bool IsWeapon(){return false;}};
struct CBasePlayerWeapon:CBasePlayerItem {
    int m_iClip=19,m_iShotsFired=3,m_fInReload=1,m_fInSpecialReload=1;bool m_bDelayFire=true;
    float m_flNextPrimaryAttack=2,m_flNextSecondaryAttack=2,m_flTimeWeaponIdle=2,m_flLastFireTime=17,
        m_flPrevPrimaryAttack=3,m_flDecreaseShotsFired=9,m_flAccuracy=.4f,m_flNextReload=4.3f;
    CBasePlayerWeapon(){pev->classname="weapon_cannon";}bool IsWeapon()override{return true;}
    void AttachToPlayer(CBasePlayer*);
};
struct CBasePlayer:CBaseEntity {
    CBasePlayerItem* m_rgpPlayerItems[MAX_ITEM_TYPES]{};CBasePlayerItem* m_pActiveItem{},*m_pLastItem{};
    float m_flNextAttack=.12f;int index{},userid{},m_iHideHUD=33;bool alive=true,role=true,dormant=false;int updates{},switches{};
    CBasePlayerItem* GetItemById(int id){for(auto* first:m_rgpPlayerItems)for(auto* p=first;p;p=p->m_pNext)if(p->id==id)return p;return nullptr;}
    bool IsAlive(){return alive;}bool IsDormant(){return dormant;}int entindex(){return index;}
    CBaseEntity* DropPlayerItem(const char*);int AddPlayerItem(CBasePlayerItem*);int RemovePlayerItem(CBasePlayerItem*);
    void SelectItem(const char*);bool SwitchWeapon(CBasePlayerItem*);
};
struct CWeaponBox:CBaseEntity {CBasePlayerItem* m_rgpPlayerItems[MAX_ITEM_TYPES]{};CWeaponBox(){pev->classname="weaponbox";}bool IsEmpty(){for(auto* p:m_rgpPlayerItems)if(p)return false;return true;}};
struct Globals{int maxClients=2;}globals,*gpGlobals=&globals;
std::array<CBasePlayer*,3> players{};
std::vector<std::unique_ptr<CBaseEntity>> entities;
std::function<void(const char*,CBaseEntity*)> callback;
CBasePlayerWeapon *initial{},*fresh{};CWeaponBox* box{};bool spawnFails{},dropRefuses{},touchRefuses{},addRefuses{},removeRefuses{},addPostFalse{};
int checks{},failures{},warnings{},addCalls{},touchCalls{};
template<class T>T* make(){auto p=std::make_unique<T>();auto* q=p.get();entities.push_back(std::move(p));return q;}
void event(const char* name,CBaseEntity* entity){if(callback)callback(name,entity);}
CBasePlayer* Player(int index){return index>0 && index<=2 && players[index] && !players[index]->dormant?players[index]:nullptr;}
CBaseEntity* UTIL_PlayerByIndex(int index){return Player(index);}
int GETPLAYERUSERID(edict_t* e){return static_cast<CBasePlayer*>(e->object)->userid;}
bool CSOL_ZombieHuman(CBasePlayer* p){return p->alive && p->role;}
bool CSOL_ClipAmmo(CBasePlayerWeapon* p){return p && p->id==144 && !std::strcmp(p->pev->classname,"weapon_cannon");}
bool CSOL_HasWeapon(CBasePlayer* p,int id){return p->GetItemById(id)!=nullptr;}
void CSOL_UpdateWeaponInventory(CBasePlayer* p){++p->updates;}
bool FClassnameIs(Vars* vars,const char* text){return !std::strcmp(vars->classname,text);}
const char* ALLOC_STRING(const char* text){return text;}
bool FNullEnt(edict_t* e){return !e;}
edict_t* CREATE_NAMED_ENTITY(const char*){fresh=make<CBasePlayerWeapon>();fresh->m_iClip=20;return fresh->edict();}
void DispatchSpawn(edict_t* e){if(spawnFails)e->v.flags|=FL_KILLME;event("spawn",e->object);}
void UTIL_Remove(CBaseEntity* e){e->pev->flags|=FL_KILLME;event("remove",e);}
void SERVER_PRINT(const char*){++warnings;}
void unlink(CBasePlayer* p,CBasePlayerItem* weapon){
    for(auto& head:p->m_rgpPlayerItems){auto** link=&head;while(*link && *link!=weapon)link=&(*link)->m_pNext;if(*link){*link=weapon->m_pNext;weapon->m_pNext=nullptr;}}
}
int CBasePlayer::AddPlayerItem(CBasePlayerItem* weapon){
    ++addCalls;event("beforeAdd",weapon);
    if(addRefuses)return 0;
    weapon->m_pPlayer=this;weapon->m_pNext=m_rgpPlayerItems[weapon->slot];m_rgpPlayerItems[weapon->slot]=weapon;
    event("afterAdd",weapon);return !addPostFalse;
}
int CBasePlayer::RemovePlayerItem(CBasePlayerItem* weapon){
    event("beforeRemove",weapon);if(removeRefuses)return 0;
    unlink(this,weapon);if(m_pActiveItem==weapon)m_pActiveItem=nullptr;weapon->m_pPlayer=nullptr;event("afterRemove",weapon);return 1;
}
void CBasePlayerWeapon::AttachToPlayer(CBasePlayer* player){pev->owner=player->edict();m_pPlayer=player;event("attach",this);}
bool CBasePlayer::SwitchWeapon(CBasePlayerItem* weapon){
    ++switches;m_pLastItem=m_pActiveItem;m_pActiveItem=weapon;m_flNextAttack=.75f;
    auto* w=static_cast<CBasePlayerWeapon*>(weapon);w->m_flTimeWeaponIdle=1.5f;w->m_iShotsFired=0;w->m_bDelayFire=false;
    w->m_fInReload=w->m_fInSpecialReload=0;w->m_flNextReload=0;m_iHideHUD&=~HIDEHUD_WEAPONS;event("switch",weapon);return true;
}
void CBasePlayer::SelectItem(const char*){if(auto* weapon=GetItemById(144);weapon && weapon!=m_pActiveItem)SwitchWeapon(weapon);}
CBaseEntity* CBasePlayer::DropPlayerItem(const char*){
    auto* weapon=GetItemById(144);if(!weapon)return nullptr;
    if(m_pActiveItem!=GetItemById(29))SwitchWeapon(GetItemById(29));
    event("beforeDrop",weapon);if(dropRefuses)return nullptr;
    unlink(this,weapon);weapon->m_pPlayer=nullptr;
    box=make<CWeaponBox>();box->pev->owner=edict();box->m_rgpPlayerItems[weapon->slot]=weapon;weapon->pev->owner=box->edict();
    event("afterDrop",weapon);return box;
}
void DispatchTouch(edict_t* e,edict_t* other){
    ++touchCalls;auto* weapon=static_cast<CBasePlayerWeapon*>(e->object);auto* player=static_cast<CBasePlayer*>(other->object);
    event("touch",weapon);if(touchRefuses)return;
    if(player->AddPlayerItem(weapon))weapon->AttachToPlayer(player);
}
#include "purchase_backup.inc"
void check(bool condition,const char* label){++checks;if(!condition){++failures;std::printf("FAIL %s\n",label);}}
void reset(){
    callback={};entities.clear();players={};fresh=nullptr;box=nullptr;spawnFails=dropRefuses=touchRefuses=addRefuses=removeRefuses=addPostFalse=false;
    warnings=addCalls=touchCalls=0;
    for(int i=1;i<=2;++i){auto* p=make<CBasePlayer>();p->index=i;p->userid=100+i;players[i]=p;}
    initial=make<CBasePlayerWeapon>();initial->m_pPlayer=players[1];initial->pev->owner=players[1]->edict();players[1]->m_rgpPlayerItems[1]=initial;
    auto* knife=make<CBasePlayerWeapon>();knife->id=29;knife->slot=3;knife->pev->classname="weapon_knife";knife->m_pPlayer=players[1];knife->pev->owner=players[1]->edict();
    players[1]->m_rgpPlayerItems[3]=knife;players[1]->m_pActiveItem=initial;players[1]->m_pLastItem=knife;
}
void preserved(){
    auto* p=players[1];check(p->GetItemById(144)==initial,"same cannon in inventory");check(initial->pev->owner==p->edict(),"original owner player");
    check(initial->m_iClip==19,"clip preserved");check(initial->m_flNextPrimaryAttack==2 && initial->m_flNextSecondaryAttack==2,"cooldowns preserved");
    check(initial->m_flTimeWeaponIdle==2 && p->m_flNextAttack==.12f,"idle and player cooldown preserved");
    check(p->m_pActiveItem==initial && p->m_pLastItem==p->GetItemById(29),"active and last preserved");
    check(initial->m_iShotsFired==3 && initial->m_bDelayFire && initial->m_flAccuracy==.4f,"firing state preserved");
    check(initial->m_flLastFireTime==17 && initial->m_flPrevPrimaryAttack==3 && initial->m_flDecreaseShotsFired==9,"timing internals preserved");
    check(initial->m_fInReload==1 && initial->m_fInSpecialReload==1 && initial->m_flNextReload==4.3f,"reload timers preserved");
    check(p->m_iHideHUD==33,"weapon HUD bit restored without changing other bits");
}
int main(){
    reset();check(CannonPurchaseBackup(players[1]).Replace()==1,"normal replacement accepted");check(players[1]->GetItemById(144)==fresh && fresh->m_iClip==20,"fresh full cannon held");check(box && box->m_rgpPlayerItems[1]==initial,"old cannon remains dropped");
    reset();touchRefuses=true;check(CannonPurchaseBackup(players[1]).Replace()==0,"touch refusal fails purchase");preserved();check(box && box->IsEmpty() && (box->pev->flags&FL_KILLME),"empty old box removed");check(fresh->pev->flags&FL_KILLME,"refused fresh removed");
    reset();dropRefuses=true;check(CannonPurchaseBackup(players[1]).Replace()==0,"refused drop fails purchase");preserved();check(touchCalls==0,"refused drop never touches replacement");
    reset();touchRefuses=true;players[1]->m_pActiveItem=players[1]->GetItemById(29);players[1]->m_pLastItem=nullptr;check(CannonPurchaseBackup(players[1]).Replace()==0,"inactive cannon failure");check(players[1]->switches==0,"already active fallback not switched again");
    reset();touchRefuses=true;{auto* pistol=make<CBasePlayerWeapon>();pistol->id=26;pistol->slot=2;pistol->pev->classname="weapon_deagle";pistol->m_pPlayer=players[1];pistol->pev->owner=players[1]->edict();players[1]->m_rgpPlayerItems[2]=pistol;players[1]->m_pActiveItem=pistol;
        check(CannonPurchaseBackup(players[1]).Replace()==0,"active pistol purchase refusal");check(players[1]->m_pActiveItem==pistol,"restore active pistol");
        check(pistol->m_flTimeWeaponIdle==2 && pistol->m_flNextPrimaryAttack==2 && pistol->m_flNextSecondaryAttack==2 && pistol->m_flNextReload==4.3f,"restore pistol public timers");
        check(pistol->m_fInReload==1 && pistol->m_fInSpecialReload==1 && pistol->m_iShotsFired==3 && pistol->m_bDelayFire,"restore pistol reload and recoil state");}
    reset();spawnFails=true;check(CannonPurchaseBackup(players[1]).Replace()==0,"spawn failure");preserved();check(!box,"spawn failure never drops original");
    reset();addPostFalse=true;check(CannonPurchaseBackup(players[1]).Replace()==0,"post-add FALSE cannot count as purchase");preserved();check(fresh->pev->flags&FL_KILLME,"partially added fresh removed");
    reset();touchRefuses=true;addRefuses=true;check(CannonPurchaseBackup(players[1]).Replace()==0,"restoration Add refusal");check(!players[1]->GetItemById(144) && box->m_rgpPlayerItems[1]==initial,"restore refusal leaves original solely in box");check(!initial->m_pPlayer && !initial->m_pNext && !(box->pev->flags&FL_KILLME),"failed restore box remains usable");
    reset();touchRefuses=true;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"afterDrop"))players[1]->role=false;};check(CannonPurchaseBackup(players[1]).Replace()==0,"infection during drop fails");check(box->m_rgpPlayerItems[1]==initial && !players[1]->GetItemById(144),"no restore onto zombie");
    reset();touchRefuses=true;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"afterDrop"))++players[1]->userid;};check(CannonPurchaseBackup(players[1]).Replace()==0,"userid replacement fails");check(!players[1]->GetItemById(144),"no restore onto replacement session");
    reset();touchRefuses=true;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"afterDrop"))++initial->record.serial;};check(CannonPurchaseBackup(players[1]).Replace()==0,"deleted original serial ignored");check(!players[1]->GetItemById(144),"no stale original reattached");
    reset();touchRefuses=true;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"afterDrop"))box->pev->flags|=FL_KILLME;};check(CannonPurchaseBackup(players[1]).Replace()==0,"pending deleted box fails restore");check(!players[1]->GetItemById(144),"no resurrect from pending deleted box");
    reset();touchRefuses=true;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"touch")){box->m_rgpPlayerItems[1]=nullptr;players[2]->AddPlayerItem(initial);initial->AttachToPlayer(players[2]);}};check(CannonPurchaseBackup(players[1]).Replace()==0,"transferred original fails restore");check(players[2]->GetItemById(144)==initial && initial->pev->owner==players[2]->edict(),"other player's gun retained");
    reset();addPostFalse=true;removeRefuses=true;check(CannonPurchaseBackup(players[1]).Replace()==0,"remove rejection fails purchase");check(players[1]->GetItemById(144)==fresh && !(fresh->pev->flags&FL_KILLME),"never remove still-linked fresh entity");check(box->m_rgpPlayerItems[1]==initial,"original retained in box on remove refusal");
    reset();{CannonPurchaseGuard first(1);check(first.Allowed(),"first cannon entry allowed");CannonPurchaseGuard nested(1);check(!nested.Allowed(),"same buyer nested cannon denied");CannonPurchaseGuard other(2);check(other.Allowed(),"different buyer entry independent");}
    {CannonPurchaseGuard next(1);check(next.Allowed(),"guard releases after scope");}
    reset();touchRefuses=true;callback=[](const char* stage,CBaseEntity*){if(!std::strcmp(stage,"touch")){CannonPurchaseGuard nested(1);check(!nested.Allowed(),"reentry denied inside Touch callback");}};
    {CannonPurchaseGuard outer(1);check(CannonPurchaseBackup(players[1]).Replace()==0,"guarded refused purchase");}preserved();
    std::printf("%s cannon purchase backup %d checks; %d failures\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;
}
