// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace csol
{
// CSO transmits equipment IDs in ten bits, separately from the active
// weapon's sixteen-bit ID and its single prediction record (slot zero).
constexpr int WeaponIdLimit = 1024;
constexpr int LegacyWeaponLimit = 32;
constexpr int ActiveWeaponSlot = 0;
constexpr int ThrowingKnifeId = 220;
constexpr int BeamgunId = 725;
constexpr int BeamgunLEId = 726;
constexpr std::uint8_t WeaponProtocolVersion = 1;
constexpr char WeaponDefinitionMessage[] = "CSOWpnList";
constexpr char WeaponInventoryMessage[] = "CSOWpnInv";
constexpr char WeaponCurrentMessage[] = "CSOWpnCur";
constexpr char WeaponPickupMessage[] = "CSOWpnPick";
constexpr char AmmoPickupMessage[] = "CSOAmmoPick";

// Stock CS reserves 31 for the suit and 99 for the shield purchase identity.
constexpr bool ValidWeaponId(int id) { return id > 0 && id < WeaponIdLimit && id != 31 && id != 99; }
constexpr bool ExtendedWeaponId(int id) { return ValidWeaponId(id) && id >= LegacyWeaponLimit; }
constexpr std::uint32_t LegacyWeaponMask(int id)
{
    return id > 0 && id < LegacyWeaponLimit ? (std::uint32_t{1} << id) : 0;
}
constexpr int PredictionSlot(int id, bool active)
{
    return !ValidWeaponId(id) ? -1 : id < LegacyWeaponLimit ? id : active ? ActiveWeaponSlot : -1;
}

struct WeaponDefinition
{
    int id{};
    int slot{}; // HUD slot, zero based; independent of the weapon ID.
    int position{};
    int ammo1 = -1, ammo2 = -1;
    int maxAmmo1 = -1, maxAmmo2 = -1;
    int maxClip = -1;
    int flags{};
    std::string name;
    bool operator==(const WeaponDefinition&) const = default;
};
struct WeaponCurrent
{
    int id{}, state{};
    int clip = -1;
    int reserve{}, secondary{};
    bool operator==(const WeaponCurrent&) const = default;
};
struct AmmoPickup { int id{}, secondary{}, amount{}; };
std::vector<std::byte> EncodeAmmoPickup(const AmmoPickup& value);
bool DecodeAmmoPickup(std::span<const std::byte> bytes,AmmoPickup& value);

// Reliable inventory messages contain a complete, atomically committed list.
// A generation may span several <= 192-byte GoldSrc user messages. The
// client never exposes a partial list or trusts a truncated message.
using WeaponOwnership = std::array<bool, WeaponIdLimit>;
std::vector<std::byte> EncodeWeaponDefinition(const WeaponDefinition& value);
bool DecodeWeaponDefinition(std::span<const std::byte> bytes, WeaponDefinition& value);
std::vector<std::byte> EncodeWeaponCurrent(const WeaponCurrent& value);
bool DecodeWeaponCurrent(std::span<const std::byte> bytes, WeaponCurrent& value);
std::vector<std::vector<std::byte>> EncodeWeaponInventory(const WeaponOwnership& owned, std::uint16_t generation);

class WeaponInventoryReceiver
{
public:
    bool Receive(std::span<const std::byte> bytes);
    void Reset();
    const WeaponOwnership& Owned() const { return owned_; }
private:
    WeaponOwnership owned_{}, pending_{};
    std::uint16_t generation_{};
    unsigned nextPart_{};
    int lastId_{};
    bool receiving_{};
};
}
