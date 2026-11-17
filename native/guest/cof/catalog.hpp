// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_damage.hpp"

#include <array>
#include <string_view>

namespace guest::cof
{
// Authored CS inventory placement. CoF's original HUD slot is not a CS slot.
inline constexpr int GlockHudSlot = 1;
inline constexpr int GlockHudPosition = 0;
inline constexpr int P345HudSlot = 1;
inline constexpr int P345HudPosition = 1;

// Host-only IDs, classnames, prices and shop placement. Original CoF IDs
// collide with stock CS; never publish an original ID as its host identity.
template<class Identity>
struct BasicWeaponDefinition
{
    Identity identity;
    int hostId;
    int originalId;
    int clip;
    int reserve;
    int price;
    int ammunitionPrice;
    int ammunitionCount;
    std::string_view classname;
    std::string_view ammunition;
    std::string_view displayName;
    std::string_view viewModel;
    std::string_view playerModel;
    std::string_view worldModel;
};

using PistolDefinition=BasicWeaponDefinition<Pistol>;
using FirearmDefinition=BasicWeaponDefinition<Firearm>;

inline constexpr std::array Pistols{
    PistolDefinition{Pistol::Glock, 896, 3, 15, 150, 650, 30, 15,
        "weapon_cof_glock", "cof_glock", "客串武器·Glock 19",
        "models/guest/cof/glock/v_glock.mdl", "models/guest/cof/glock/p_glock.mdl",
        "models/guest/cof/glock/w_glock.mdl"},
    PistolDefinition{Pistol::P345, 897, 7, 8, 80, 800, 30, 8,
        "weapon_cof_p345", "cof_p345", "客串武器·P345",
        "models/guest/cof/p345/v_p345.mdl", "models/guest/cof/p345/p_p345.mdl",
        "models/guest/cof/p345/w_p345.mdl"},
    PistolDefinition{Pistol::Revolver, 898, 21, 5, 50, 1300, 45, 5,
        "weapon_cof_revolver", "cof_revolver", "客串武器·Taurus .357",
        "models/guest/cof/revolver/v_revolver.mdl", "models/guest/cof/revolver/p_revolver.mdl",
        "models/guest/cof/revolver/w_revolver.mdl"},
    PistolDefinition{Pistol::VP70, 899, 19, 18, 180, 1100, 45, 18,
        "weapon_cof_vp70", "cof_vp70", "客串武器·VP70",
        "models/guest/cof/vp70/v_vp70.mdl", "models/guest/cof/vp70/p_vp70.mdl",
        "models/guest/cof/vp70/w_vp70.mdl"}
};

// Compile-time common view; original pistol family APIs remain source-compatible.
constexpr Firearm FirearmIdentity(Pistol identity) noexcept
{
    const auto index=static_cast<unsigned>(identity);
    return index<Pistols.size()?static_cast<Firearm>(index):Firearm::Count;
}
inline constexpr std::array<FirearmDefinition,FirearmKeys.size()> Firearms=[] {
    std::array<FirearmDefinition,FirearmKeys.size()> result{};
    for(std::size_t i=0;i<Pistols.size();++i)
    {
        const auto& source=Pistols[i];
        result[i]={FirearmIdentity(source.identity),source.hostId,source.originalId,source.clip,source.reserve,
            source.price,source.ammunitionPrice,source.ammunitionCount,source.classname,source.ammunition,
            source.displayName,source.viewModel,source.playerModel,source.worldModel};
    }
    result[static_cast<unsigned>(Firearm::TMP)]={Firearm::TMP,900,14,30,300,2000,30,30,
        "weapon_cof_tmp","cof_tmp","客串武器·MP9",
        "models/guest/cof/tmp/v_tmp.mdl","models/guest/cof/tmp/p_tmp.mdl","models/guest/cof/tmp/w_tmp.mdl"};
    result[static_cast<unsigned>(Firearm::MP5)]={Firearm::MP5,901,27,30,300,2400,30,30,
        "weapon_cof_mp5","cof_mp5","客串武器·MP5A3",
        "models/guest/cof/mp5/v_mp5.mdl","models/guest/cof/mp5/p_mp5.mdl","models/guest/cof/mp5/w_mp5.mdl"};
    result[static_cast<unsigned>(Firearm::M16)]={Firearm::M16,902,9,20,200,3100,40,20,
        "weapon_cof_m16","cof_m16","客串武器·M16",
        "models/guest/cof/m16/v_m16.mdl","models/guest/cof/m16/p_m16.mdl","models/guest/cof/m16/w_m16.mdl"};
    result[static_cast<unsigned>(Firearm::G43)]={Firearm::G43,903,26,10,100,3500,50,10,
        "weapon_cof_g43","cof_g43","客串武器·G43",
        "models/guest/cof/g43/v_g43.mdl","models/guest/cof/g43/p_g43.mdl","models/guest/cof/g43/w_g43.mdl"};
    result[static_cast<unsigned>(Firearm::Shotgun)]={Firearm::Shotgun,904,4,5,50,3000,65,5,
        "weapon_cof_shotgun","cof_shotgun","客串武器·Remington 870",
        "models/guest/cof/shotgun/v_shotgun.mdl","models/guest/cof/shotgun/p_shotgun.mdl",
        "models/guest/cof/shotgun/w_shotgun.mdl"};
    return result;
}();
constexpr const FirearmDefinition* FindFirearm(Firearm identity) noexcept
{
    const auto index=static_cast<unsigned>(identity);
    return index<Firearms.size()?&Firearms[index]:nullptr;
}
constexpr const FirearmDefinition* FindFirearm(int hostId) noexcept
{
    const unsigned index=static_cast<unsigned>(hostId)-Firearms.front().hostId;
    return index<Firearms.size()?&Firearms[index]:nullptr;
}
constexpr const FirearmDefinition* FindOriginalFirearm(int originalId) noexcept
{
    switch(originalId)
    {
    case 3:return FindFirearm(Firearm::Glock);
    case 7:return FindFirearm(Firearm::P345);
    case 21:return FindFirearm(Firearm::Revolver);
    case 19:return FindFirearm(Firearm::VP70);
    case 14:return FindFirearm(Firearm::TMP);
    case 27:return FindFirearm(Firearm::MP5);
    case 9:return FindFirearm(Firearm::M16);
    case 26:return FindFirearm(Firearm::G43);
    case 4:return FindFirearm(Firearm::Shotgun);
    default:return nullptr;
    }
}
constexpr bool OriginalFirearmId(int id) noexcept {return FindOriginalFirearm(id)!=nullptr;}
static_assert(Firearms.size()==FirearmKeys.size());

constexpr const PistolDefinition* FindPistol(int hostId) noexcept
{
    const unsigned index = static_cast<unsigned>(hostId) - Pistols.front().hostId;
    return index < Pistols.size() ? &Pistols[index] : nullptr;
}

constexpr const PistolDefinition* FindPistol(Pistol identity) noexcept
{
    const auto index = static_cast<unsigned>(identity);
    return index < Pistols.size() ? &Pistols[index] : nullptr;
}

constexpr bool OriginalPistolId(int id) noexcept
{
    return id == 3 || id == 7 || id == 19 || id == 21;
}

constexpr const PistolDefinition* FindOriginalPistol(int id) noexcept
{
    switch(id)
    {
    case 3: return &Pistols[static_cast<unsigned>(Pistol::Glock)];
    case 7: return &Pistols[static_cast<unsigned>(Pistol::P345)];
    case 21: return &Pistols[static_cast<unsigned>(Pistol::Revolver)];
    case 19: return &Pistols[static_cast<unsigned>(Pistol::VP70)];
    default: return nullptr;
    }
}
}
