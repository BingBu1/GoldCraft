// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../player_actions.hpp"
#include "../scenario_damage.hpp"

class CBasePlayer;

namespace guest::cof::server
{
enum class PlayerAction : unsigned char { Hold, Move, Fire, Reload, IronSight, WeaponMelee, Count };
inline constexpr std::size_t PlayerActionCount = static_cast<std::size_t>(PlayerAction::Count);

// Every pair is explicit, including IronSight and WeaponMelee. An absent
// onehanded shoot2 must never become PLAYER_ATTACK2 or sequence zero.
using PlayerAnimationPlan = std::array<PlayerActionLabels, PlayerActionCount>;

// Explicit first-batch host authoring choice. Stock uses knife shoot as a
// pistol-whip gesture; CSO uses its one-handed nataknife shoot. Hold/Move/Iron
// use onehanded aim, Fire/Reload use matching onehanded tracks. Bind still
// rejects absent labels. This is not an original CoF animation/grip claim.
enum class PistolAnimationRig : unsigned char { Stock, CSO };
enum class LongGun : unsigned char { TMP, MP5, M16, Shotgun, Rifle, Count };
std::optional<PlayerAnimationPlan> RecommendedPistolAnimationPlan(Pistol weapon,
                                                                PistolAnimationRig rig) noexcept;
// Explicit host authoring: compact guns use mp5, M16 uses stock rifle / CSO
// m16a1ep, pump uses shotgun and bolt rifle uses rifle. CSO longgun melee uses
// m16a1ep shoot2; compact melee uses nataknife shoot. Timing stays with weapon.
std::optional<PlayerAnimationPlan> RecommendedLongGunAnimationPlan(LongGun weapon,
                                                                 PistolAnimationRig rig) noexcept;

enum class PlayerAnimationResult { Rejected, PreservedNative, Unchanged, Applied };

class PlayerAnimationAdapter
{
public:
    // Cold authoritative game-thread operation after full model validation.
    // Span must refer to GET_MODEL_PTR(owner), not an unrelated offline copy.
    // Bind to this owner/life's actual model. Host bumps modelGeneration on
    // every owner life, role, model or content change; mapEpoch on session/map.
    // Failure drops the previous binding. No default weapon/pose mappings.
    bool Prepare(CBasePlayer& player, std::span<const std::byte> validatedModel,
                 PlayerModelGeneration generation, Pistol weapon,
                 const PlayerAnimationPlan& plan, PlayerActionLabelCompare hostCompare,
                 std::string& error, PlayerModelExtent extent = PlayerModelExtent::CompleteFile);
    bool Prepare(CBasePlayer& player, std::span<const std::byte> validatedModel,
                 PlayerModelGeneration generation, LongGun weapon,
                 const PlayerAnimationPlan& plan, PlayerActionLabelCompare hostCompare,
                 std::string& error, PlayerModelExtent extent = PlayerModelExtent::CompleteFile);

    // Apply after the native player movement/animation decision. Preserve its
    // gait, swim/jump/death/flinch authority. Hold/Move/IronSight never interrupt
    // an unfinished native attack/reload. Fire/Melee explicitly restart the
    // selected upper-body action; this does not schedule a melee damage think.
    // Parent must still validate active-item ownership/session/life before use.
    PlayerAnimationResult Apply(CBasePlayer& player, PlayerModelGeneration current,
                                Pistol weapon, PlayerAction action, float now) const;
    PlayerAnimationResult Apply(CBasePlayer& player, PlayerModelGeneration current,
                                LongGun weapon, PlayerAction action, float now) const;

    std::optional<PlayerActionSequence> Select(PlayerModelGeneration current,
                                              Pistol weapon, PlayerAction action,
                                              bool crouching) const noexcept;
    std::optional<PlayerActionSequence> Select(PlayerModelGeneration current,
                                              LongGun weapon, PlayerAction action,
                                              bool crouching) const noexcept;
    void Reset() noexcept;

private:
    static constexpr unsigned WeaponCount = static_cast<unsigned>(Pistol::Count) + static_cast<unsigned>(LongGun::Count);
    bool PrepareWeapon(CBasePlayer&, std::span<const std::byte>, PlayerModelGeneration,
                       unsigned weapon, const PlayerAnimationPlan&, PlayerActionLabelCompare,
                       std::string& error, PlayerModelExtent);
    PlayerAnimationResult ApplyWeapon(CBasePlayer&, PlayerModelGeneration, unsigned weapon,
                                      PlayerAction, float now) const;
    std::optional<PlayerActionSequence> SelectWeapon(PlayerModelGeneration, unsigned weapon,
                                                    PlayerAction, bool crouching) const noexcept;
    PlayerActionBindings bindings_;
    const CBasePlayer* owner_{};
    int ownerSerial_{}, modelIndex_{};
    unsigned weapon_{WeaponCount};
};
}
