// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_resources.hpp"
#include "native/guest/cof/server/sound_dispatch.hpp"
#include "ehandle.h"

class CSoundEnt;

namespace csol::server::cof
{
// NativeCS adaptation of the proven CoF extra-sound flags/deadline consumer.
// This owns an actual SDK CSoundEnt, not a shadow sound pool or NPC listener.
// Authoritative thread only. Callback storage and engine private data must
// survive each synchronous SDK call, including a reentrant EndMap.
class SoundField final
{
public:
    SoundField() = default;
    ~SoundField() { EndMap(); }
    SoundField(const SoundField&) = delete;
    SoundField& operator=(const SoundField&) = delete;
    SoundField(SoundField&&) = delete;
    SoundField& operator=(SoundField&&) = delete;
    bool BeginMap(const ResourceAuthority&) noexcept;
    void EndMap() noexcept;
    // Actual OnFreeEntPrivateData, while the edict table is still readable.
    // Drop the handle before ReHLDS releases/reuses the old map's hunk.
    void EntityRetiring(edict_t*) noexcept;
    std::uint64_t Generation() const noexcept { return authority_.generation; }
    // EndMap revokes admission; only the engine releases the entity itself.
    bool EngineEntitiesReleased(std::uint64_t generation) noexcept;
    bool Current() const noexcept;
    bool ShotLatch(CBasePlayer&, int types, float deadline, const guest::cof::PlaySfxCall&) noexcept;
    // Dispatcher calls this once at real PostThink tail for a current CoF
    // NativeCS movement owner. Native UpdatePlayerSound semantics are retained.
    bool PostThink(CBasePlayer&, const guest::cof::PlaySfxCall&) noexcept;
    // Cold spawn/death/disconnect: clear actual reserved slot and extra fields.
    bool ResetPlayer(CBasePlayer&, const guest::cof::PlaySfxCall&) noexcept;
private:
    bool Identity() const noexcept;
    bool PlayerCurrent(CBasePlayer&, const EHANDLE&, const guest::cof::PlaySfxCall&, bool alive) const noexcept;
    ResourceAuthority authority_{};
    EHANDLE sound_{};
    edict_t* entity_{};
    CSoundEnt* identity_{};
    std::uint64_t lastGeneration_{};
    bool active_{}, busy_{};
};
}
