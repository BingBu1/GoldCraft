// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the real SDK animation/world harness and declared component boundaries.
#define main GlockCompositionMain
#include "../cryoffear-glock-weapon/main.cpp"
#undef main
#include "native/csol/server/weapons/guest_cof_factory.hpp"
#include <cstring>

// Factory only needs construction and identity of the actual SDK wrapper.
// These virtual gameplay methods are explicit abort boundaries.
BOOL CCSPlayerWeapon::DefaultDeploy(char*,char*,int,char*,int){std::abort();}
int CCSPlayerWeapon::DefaultReload(int,int,float){std::abort();}
bool CCSPlayerWeapon::DefaultShotgunReload(int,int,float,float,const char*,const char*){std::abort();}
void CCSPlayerWeapon::KickBack(float,float,float,float,float,float,int){std::abort();}
void CCSPlayerWeapon::SendWeaponAnim(int,int){std::abort();}
#include "sdk-on-create.inc"
namespace
{
using namespace csol::server::cof;
std::vector<void*> privateAllocations;
int allocated{},prepareCalls{},currentCalls{},mutation{},mutateAt{};
edict_t* target{};
bool allowCurrent=true;
bool FactoryCurrent(void*) noexcept
{
    ++currentCalls;
    if(currentCalls==mutateAt)
    {
        switch(mutation)
        {
        case 1:ResetGlockFactory();break;
        case 2:++target->serialnumber;break;
        case 3:target->free=TRUE;break;
        case 4:target->v.flags|=FL_KILLME;break;
        case 5:target->pvPrivateData=&player;break;
        case 6:allowCurrent=false;break;
        case 7:InstallGlockFactory({});break;
        }
    }
    return allowCurrent;
}
bool FactoryPrepare(void*,GlockWeapon&,GlockPrepared& out) noexcept{++prepareCalls;out=prepared;return true;}
GlockColdHost Host(){return {nullptr,FactoryCurrent,FactoryPrepare,ColdOwner,ColdInventory};}
void InitEdict(edict_t& e){e={};e.serialnumber=19;e.v.pContainingEntity=&e;}
void Configure()
{
    Initialize();ResetGlockFactory();privateAllocations.clear();allocated=prepareCalls=currentCalls=mutation=mutateAt=0;allowCurrent=true;
    g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* entity,int32 amount)->void*
    {
        Check(amount>=int(sizeof(GlockWeapon)));auto* memory=std::calloc(1,std::size_t(amount));Check(memory!=nullptr);
        privateAllocations.push_back(memory);entity->pvPrivateData=memory;++allocated;return memory;
    };
}
void Release()
{
    ResetGlockFactory();
    for(auto* p:privateAllocations)
    {
        auto* item=static_cast<GlockWeapon*>(p);delete item->m_pEntity;item->~GlockWeapon();std::free(p);
    }
    privateAllocations.clear();
}
}
int main(int argc,char** argv)
{
    if(argc!=2)return 1;
    std::ifstream file(argv[1],std::ios::binary);if(!file)return 1;
    std::vector<char> bytes{std::istreambuf_iterator<char>(file),{}};model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    unsigned scenarios{};
    // The engine's temporary precache path calls factory then Precache without
    // Spawn. Keep sentinel fields to prove factory acquisition never spawns.
    Configure();edict_t temporary{};InitEdict(temporary);target=&temporary;
    Check(InstallGlockFactory(Host()));temporary.v.health=37;temporary.v.movetype=71;temporary.v.solid=72;
    auto* item=CreateGlock(temporary.v);Check(item&&allocated==1&&item->pev==&temporary.v&&temporary.pvPrivateData==item);
    Check(dynamic_cast<CCSPlayerWeapon*>(item->m_pEntity)!=nullptr&&item->m_pEntity->m_pContainingEntity==item);
    Check(prepareCalls==0&&temporary.v.health==37&&temporary.v.movetype==71&&temporary.v.solid==72&&item->m_pfnThink==nullptr);
    item->Precache();ItemInfo info{};Check(prepareCalls==1&&item->ResourcesReady()&&item->GetItemInfo(&info)&&info.iId==896&&temporary.v.movetype==71&&item->m_pfnThink==nullptr);
    auto& state=const_cast<MagazineActionState&>(item->ActionState());state.primary=123.5f;state.secondary=91.25f;state.loading=true;state.action=2;
    item->m_iClip=7;item->m_flNextPrimaryAttack=123.5f;
    Check(CreateGlock(temporary.v)==item&&allocated==1&&prepareCalls==1&&item->ActionState().primary==123.5f&&item->ActionState().secondary==91.25f&&item->ActionState().loading&&item->ActionState().action==2&&item->m_iClip==7&&item->m_flNextPrimaryAttack==123.5f);
    ResetGlockFactory();Check(CreateGlock(temporary.v)==item&&item->ActionState().primary==123.5f);++scenarios;Release();
    // No installed current backend means no engine private allocation.
    Configure();edict_t empty{};InitEdict(empty);target=&empty;Check(!CreateGlock(empty.v)&&allocated==0);Release();++scenarios;
    for(int missing=0;missing<5;++missing)
    {
        Configure();edict_t e{};InitEdict(e);target=&e;auto h=Host();
        switch(missing){case 0:h.current=nullptr;break;case 1:h.prepare=nullptr;break;case 2:h.ownerBackends=nullptr;break;case 3:h.inventoryPickup=nullptr;break;case 4:allowCurrent=false;break;}
        Check(!InstallGlockFactory(h)&&!CreateGlock(e.v)&&allocated==0);Release();++scenarios;
    }
    // Engine-provided incompatible private CBaseEntity type is refused.
    Configure();edict_t wrong{};InitEdict(wrong);target=&wrong;Check(InstallGlockFactory(Host()));wrong.pvPrivateData=&player;
    Check(!CreateGlock(wrong.v)&&wrong.pvPrivateData==&player&&allocated==0);Release();++scenarios;
    // Entity/host retirement before allocation and after GetClassPtr/OnCreate.
    for(int stage:{1,2})for(int kind=1;kind<=6;++kind)
    {
        Configure();edict_t e{};InitEdict(e);target=&e;Check(InstallGlockFactory(Host()));currentCalls=0;mutation=kind;mutateAt=stage;
        Check(!CreateGlock(e.v));Check(allocated==(stage==1?0:1));
        for(auto* p:privateAllocations)Check(!static_cast<GlockWeapon*>(p)->ResourcesReady());
        Release();++scenarios;
    }
    // Installation itself may reenter Reset; outer installation must fail.
    Configure();edict_t reentry{};InitEdict(reentry);target=&reentry;mutation=1;mutateAt=1;
    Check(!InstallGlockFactory(Host())&&!CreateGlock(reentry.v)&&allocated==0);Release();++scenarios;
    // Recycled engine entity gets a fresh class/token; old world readiness dies.
    Configure();edict_t recycled{};InitEdict(recycled);target=&recycled;Check(InstallGlockFactory(Host()));
    auto* old=CreateGlock(recycled.v);Check(old);old->Precache();Check(old->ResourcesReady());
    recycled.pvPrivateData=nullptr;++recycled.serialnumber;
    Check(!old->ResourcesReady());auto* fresh=CreateGlock(recycled.v);Check(fresh&&fresh!=old&&allocated==2&&!fresh->ResourcesReady());fresh->Precache();Check(fresh->ResourcesReady()&&!old->ResourcesReady());Release();++scenarios;
    // A genuine Glock's private object must belong to the supplied entvars.
    Configure();edict_t first{},second{};InitEdict(first);InitEdict(second);target=&first;Check(InstallGlockFactory(Host()));
    auto* original=CreateGlock(first.v);Check(original);second.pvPrivateData=original;
    Check(!CreateGlock(second.v)&&original->pev==&first.v&&allocated==1);Release();++scenarios;
    // Ordinary world Spawn follows the same caller-owned factory contract.
    Configure();edict_t spawned{};InitEdict(spawned);target=&spawned;Check(InstallGlockFactory(Host()));
    g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
    auto* world=CreateGlock(spawned.v);Check(world&&prepareCalls==0);world->Spawn();
    Check(prepareCalls==1&&world->ResourcesReady()&&world->Status()==GlockStatus::Applied&&world->m_iDefaultAmmo==15&&world->m_iId==896&&world->m_pEntity->m_pContainingEntity==world&&static_cast<CCSPlayerWeapon*>(world->m_pEntity)->m_ItemInfo.iId==896&&spawned.v.movetype==MOVETYPE_TOSS&&spawned.v.solid==SOLID_BBOX&&world->m_pfnThink!=nullptr);
    Release();++scenarios;
    for(int invalid=0;invalid<4;++invalid)
    {
        Configure();edict_t e{};InitEdict(e);target=&e;Check(InstallGlockFactory(Host()));
        if(invalid==0)e.free=TRUE;else if(invalid==1)e.v.flags|=FL_KILLME;
        else if(invalid==2)e.v.pContainingEntity=nullptr;
        else g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t*,int32)->void*{throw std::bad_alloc();};
        Check(!CreateGlock(e.v)&&allocated==0);Release();++scenarios;
    }
    // Existing private objects with broken wrapper ownership are refused.
    for(int invalid=0;invalid<3;++invalid)
    {
        Configure();edict_t e{};InitEdict(e);target=&e;Check(InstallGlockFactory(Host()));
        auto* w=CreateGlock(e.v);Check(w);auto* saved=w->m_pEntity;
        CCSPlayerItem wrongWrapper;wrongWrapper.m_pContainingEntity=w;
        if(!invalid)w->m_pEntity=nullptr;else if(invalid==1)saved->m_pContainingEntity=&player;else w->m_pEntity=&wrongWrapper;
        Check(!CreateGlock(e.v)&&allocated==1);w->m_pEntity=saved;saved->m_pContainingEntity=w;Release();++scenarios;
    }
    std::cout<<scenarios<<" actual SDK factory scenarios, "<<checks<<" checks, zero failures\n";
}
