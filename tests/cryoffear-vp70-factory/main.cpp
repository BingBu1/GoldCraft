// SPDX-License-Identifier: GPL-3.0-or-later
#define main VP70CompositionMain
#include "../cryoffear-vp70-weapon/main.cpp"
#undef main
#include "native/csol/server/weapons/guest_cof_vp70_factory.hpp"

BOOL CCSPlayerWeapon::DefaultDeploy(char*,char*,int,char*,int){std::abort();}
int CCSPlayerWeapon::DefaultReload(int,int,float){std::abort();}
bool CCSPlayerWeapon::DefaultShotgunReload(int,int,float,float,const char*,const char*){std::abort();}
void CCSPlayerWeapon::KickBack(float,float,float,float,float,float,int){std::abort();}
void CCSPlayerWeapon::SendWeaponAnim(int,int){std::abort();}
#include "sdk-on-create.inc"

namespace
{
using namespace csol::server::cof;
std::vector<void*> allocations;
unsigned allocated{},prepareCalls{},currentCalls{},mutateAt{},mutation{},scenarios{};
bool allowed=true;
edict_t* target{};
bool FactoryCurrent(void*) noexcept
{
    if(++currentCalls==mutateAt)switch(mutation)
    {
    case 1:ResetVP70Factory();break;
    case 2:++target->serialnumber;break;
    case 3:target->free=TRUE;break;
    case 4:target->v.flags|=FL_KILLME;break;
    case 5:target->pvPrivateData=&player;break;
    case 6:allowed=false;break;
    }
    return allowed;
}
bool FactoryPrepare(void*,VP70Weapon&,VP70Prepared& output) noexcept
{++prepareCalls;output=prepared;return true;}
VP70ColdHost Host(){return {nullptr,FactoryCurrent,FactoryPrepare,ColdOwner,ColdInventory};}
void Configure(edict_t& entity)
{
    Initialize();ResetVP70Factory();allocations.clear();
    allocated=prepareCalls=currentCalls=mutateAt=mutation=0;allowed=true;
    entity={};entity.serialnumber=29;entity.v.pContainingEntity=&entity;target=&entity;
    g_engfuncs.pfnPvAllocEntPrivateData=[](edict_t* entity,int32 amount)->void* {
        Check(amount>=int(sizeof(VP70Weapon)));
        auto* memory=std::calloc(1,std::size_t(amount));Check(memory!=nullptr);
        allocations.push_back(memory);entity->pvPrivateData=memory;++allocated;return memory;
    };
}
void Release()
{
    ResetVP70Factory();
    for(auto* memory:allocations)
    {
        auto* item=static_cast<VP70Weapon*>(memory);
        delete item->m_pEntity;item->~VP70Weapon();std::free(memory);
    }
    allocations.clear();++scenarios;
}
}
int main(int argc,char** argv)
{
    if(argc!=2)return 1;
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};
    Check(!bytes.empty());model.resize(bytes.size());std::memcpy(model.data(),bytes.data(),bytes.size());
    edict_t temporary{};Configure(temporary);Check(InstallVP70Factory(Host()));
    temporary.v.health=37;temporary.v.movetype=71;temporary.v.solid=72;
    auto* item=CreateVP70(temporary.v);
    Check(item&&allocated==1&&prepareCalls==0&&item->pev==&temporary.v&&item->m_pfnThink==nullptr);
    Check(dynamic_cast<CCSPlayerWeapon*>(item->m_pEntity)&&item->m_pEntity->m_pContainingEntity==item);
    Check(temporary.v.health==37&&temporary.v.movetype==71&&temporary.v.solid==72);
    item->Precache();ItemInfo info{};
    Check(prepareCalls==1&&item->ResourcesReady()&&item->GetItemInfo(&info)&&info.iId==899);
    Check(goldcraft::scenario::ValidGuestProfile(item->ScenarioGuestProfile(),899));
    auto& action=const_cast<MagazineActionState&>(item->ActionState());
    action.primary=123.5f;action.secondary=91.25f;action.loading=true;action.action=2;item->m_iClip=75;
    Check(CreateVP70(temporary.v)==item&&allocated==1&&prepareCalls==1&&item->m_iClip==75&&action.primary==123.5f&&action.loading);
    ResetVP70Factory();Check(CreateVP70(temporary.v)==item);Release();

    edict_t empty{};Configure(empty);Check(!CreateVP70(empty.v)&&allocated==0);Release();
    for(int missing=0;missing<5;++missing)
    {
        edict_t entity{};Configure(entity);auto callbacks=Host();
        switch(missing)
        {
        case 0:callbacks.current=nullptr;break;case 1:callbacks.prepare=nullptr;break;
        case 2:callbacks.ownerBackends=nullptr;break;case 3:callbacks.inventoryPickup=nullptr;break;
        case 4:allowed=false;break;
        }
        Check(!InstallVP70Factory(callbacks)&&!CreateVP70(entity.v)&&allocated==0);Release();
    }
    for(unsigned stage:{1u,2u})for(unsigned kind=1;kind<=6;++kind)
    {
        edict_t entity{};Configure(entity);Check(InstallVP70Factory(Host()));
        currentCalls=0;mutateAt=stage;mutation=kind;
        Check(!CreateVP70(entity.v)&&allocated==(stage==1?0u:1u));
        for(auto* memory:allocations)Check(!static_cast<VP70Weapon*>(memory)->ResourcesReady());
        Release();
    }
    edict_t reentrant{};Configure(reentrant);mutateAt=1;mutation=1;
    Check(!InstallVP70Factory(Host())&&!CreateVP70(reentrant.v)&&allocated==0);Release();
    edict_t wrong{};Configure(wrong);Check(InstallVP70Factory(Host()));wrong.pvPrivateData=&player;
    Check(!CreateVP70(wrong.v)&&wrong.pvPrivateData==&player&&allocated==0);Release();
    edict_t recycled{};Configure(recycled);Check(InstallVP70Factory(Host()));
    auto* old=CreateVP70(recycled.v);Check(old);old->Precache();Check(old->ResourcesReady());
    recycled.pvPrivateData=nullptr;++recycled.serialnumber;
    auto* fresh=CreateVP70(recycled.v);Check(fresh&&fresh!=old&&allocated==2&&!old->ResourcesReady());
    fresh->Precache();Check(fresh->ResourcesReady()&&!old->ResourcesReady());Release();
    edict_t spawned{};Configure(spawned);Check(InstallVP70Factory(Host()));
    g_engfuncs.pfnSetOrigin=[](edict_t*,const float*){};g_engfuncs.pfnSetSize=[](edict_t*,const float*,const float*){};
    auto* world=CreateVP70(spawned.v);Check(world&&prepareCalls==0);world->Spawn();
    Check(world->ResourcesReady()&&world->m_iDefaultAmmo==18&&world->m_iId==899&&world->m_pfnThink!=nullptr);
    Check(static_cast<CCSPlayerWeapon*>(world->m_pEntity)->m_ItemInfo.iId==899);Release();
    for(int invalid=0;invalid<3;++invalid)
    {
        edict_t entity{};Configure(entity);Check(InstallVP70Factory(Host()));
        if(invalid==0)entity.free=TRUE;else if(invalid==1)entity.v.flags|=FL_KILLME;else entity.v.pContainingEntity=nullptr;
        Check(!CreateVP70(entity.v)&&allocated==0);Release();
    }
    std::cout<<scenarios<<" actual VP70 native factory scenarios, "<<checks<<" SDK checks, zero failures\n";
}
