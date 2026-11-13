// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace goldcraft::scenario
{
// Authored foreign-game weapons have no implicit CSO item/part/perk identity.
// This capability does not bypass authoritative damage, upgrades or rewards.
enum class GuestMetadata : std::uint8_t { Invalid, NoCsoItem };
enum class GuestImpulse : std::uint8_t { Invalid, WeaponOwnsHitBehavior };
enum class GuestKill : std::uint8_t { Invalid, StandardPlayerProcessing };
struct GuestProfile
{
    std::int32_t weaponId{};
    GuestMetadata metadata{};
    GuestImpulse impulse{};
    GuestKill kills{};
};
constexpr bool ValidGuestProfile(GuestProfile profile,int actualWeaponId) noexcept
{
    return profile.weaponId==actualWeaponId && actualWeaponId>31 && actualWeaponId<1024 &&
        profile.metadata==GuestMetadata::NoCsoItem &&
        profile.impulse==GuestImpulse::WeaponOwnsHitBehavior &&
        profile.kills==GuestKill::StandardPlayerProcessing;
}
class GuestWeapon
{
public:
    // Return a value; a callback must never leave a borrowed profile dangling.
    virtual GuestProfile ScenarioGuestProfile() const noexcept=0;
protected:
    // Borrowed capability only. GoldSrc owns entity destruction through its
    // private-data lifecycle, not delete through a secondary interface.
    ~GuestWeapon()=default;
};
}
