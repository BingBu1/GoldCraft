// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.h"
#include <cstdint>
#include <limits>

namespace guest::cof::server
{
// Native entity allocation only. The retained map owns every callback context;
// installing a factory does not establish resource or operation readiness.
template<class Weapon,class ColdHost>
class NativeWeaponFactory final
{
public:
    bool Install(ColdHost candidate) noexcept
    {
        const auto before=binding_;
        if(!before||!candidate.current||!candidate.prepare||!candidate.ownerBackends||!candidate.inventoryPickup||
           !candidate.current(candidate.context)||binding_!=before)return false;
        Advance();
        if(!binding_)return false;
        host_=candidate;return true;
    }
    void Reset() noexcept {Advance();host_={};}
    Weapon* Create(entvars_t& vars) noexcept
    {
        auto* entity=ENT(&vars);
        if(!entity||entity->free||(entity->v.flags&FL_KILLME))return nullptr;
        if(auto* existing=static_cast<CBaseEntity*>(entity->pvPrivateData))
        {
            auto* weapon=dynamic_cast<Weapon*>(existing);
            return weapon&&weapon->pev==&vars&&dynamic_cast<CCSPlayerWeapon*>(weapon->m_pEntity)&&
                weapon->m_pEntity->m_pContainingEntity==weapon?weapon:nullptr;
        }
        const auto candidate=host_;
        const auto generation=binding_;
        const auto serial=entity->serialnumber;
        const auto current=[&] {
            return binding_&&binding_==generation&&!entity->free&&entity->serialnumber==serial&&
                !(entity->v.flags&FL_KILLME);
        };
        if(!candidate.current||!candidate.current(candidate.context)||!current()||entity->pvPrivateData)return nullptr;
        try
        {
            auto* weapon=GetClassPtr<CCSPlayerWeapon>(reinterpret_cast<Weapon*>(&vars));
            if(!weapon||!current()||entity->pvPrivateData!=weapon||!candidate.current(candidate.context)||
               !current()||entity->pvPrivateData!=weapon)return nullptr;
            weapon->ConfigureColdHost(candidate);return weapon;
        }
        catch(...){return nullptr;}
    }
private:
    void Advance() noexcept
    {if(binding_)binding_=binding_==std::numeric_limits<std::uint64_t>::max()?0:binding_+1;}
    ColdHost host_{};
    std::uint64_t binding_{1};
};
}
