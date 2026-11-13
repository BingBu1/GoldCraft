// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_viewmodel.hpp"
#include "guest_cof_resources.hpp"
#include "../resource_refresh.hpp"
#include "../../../guest/cof/catalog.hpp"
#include <cmath>
#include <cstring>

namespace csol::client {
namespace {
DWORD ownerThread{};
unsigned char *knifeFlag{},*eliteLeftFlag{};
cvar_t* handedness{};
const cl_entity_s* scopedEntity{};
bool Owner() noexcept {return ownerThread&&ownerThread==GetCurrentThreadId();}
}
bool InitializeGuestCofViewmodel() noexcept
{
    if(ownerThread&&!Owner())return false;
    ownerThread=GetCurrentThreadId();knifeFlag=eliteLeftFlag=nullptr;handedness=nullptr;
    if(!api||!api->ResolveGameSymbol||!api->GetClientBase||!engine.pfnGetCvarPointer)return false;
    void *knife{},*elite{};auto* module=api->GetClientBase();
    if(api->ResolveGameSymbol(module,"GoldCraftViewmodel_KnifeMirrorFlag",MH_GAMESYMBOL_KIND_GLOBAL,&knife)!=MH_GAMESYMBOL_OK||!knife||
       api->ResolveGameSymbol(module,"GoldCraftViewmodel_EliteLeftFlag",MH_GAMESYMBOL_KIND_GLOBAL,&elite)!=MH_GAMESYMBOL_OK||!elite)return false;
    auto* setting=engine.pfnGetCvarPointer("cl_righthand");if(!setting)return false;
    knifeFlag=static_cast<unsigned char*>(knife);eliteLeftFlag=static_cast<unsigned char*>(elite);handedness=setting;return true;
}
bool GuestCofViewmodelReady() noexcept {return Owner()&&knifeFlag&&eliteLeftFlag&&handedness;}
void ShutdownGuestCofViewmodel() noexcept {if(Owner()){knifeFlag=eliteLeftFlag=nullptr;handedness=nullptr;ownerThread=0;}}
GuestCofViewmodelScope::GuestCofViewmodelScope(const cl_entity_s* entity,int flags) noexcept
{
    if(!GuestCofViewmodelReady()||scopedEntity||!(flags&(STUDIO_RENDER|STUDIO_EVENTS))||!ResourceReadsAllowed()||
       !entity||!entity->model||!engine.GetViewModel||entity!=engine.GetViewModel())return;
    bool supported=false;for(const auto& pistol:guest::cof::Pistols)
        if(!std::strncmp(entity->model->name,pistol.viewModel.data(),sizeof(entity->model->name))){supported=true;break;}
    if(!supported||GuestCofResourcesState().status!=GuestCofResourceStatus::Prepared||!std::isfinite(handedness->value))return;
    // Native 10210 inverts only render calls while its previous knife/Elite
    // identity is active. CoF uses one right-hand base mesh; both native studio
    // passes must sample the same authored handedness. All changes are scoped.
    handedness_=handedness;knife_=knifeFlag;elite_=eliteLeftFlag;
    savedHand_=handedness_->value;savedKnife_=*knife_;savedElite_=*elite_;
    scopedEntity=entity;
    *knife_=*elite_=0;handedness_->value=std::trunc(savedHand_)==0?1.f:0.f;
}
GuestCofViewmodelScope::~GuestCofViewmodelScope()
{
    if(handedness_){handedness_->value=savedHand_;*knife_=savedKnife_;*elite_=savedElite_;scopedEntity=nullptr;}
}
}
