// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario/monster_appearances.hpp"
#include "../scenario/monster_attributes.hpp"

class CCSOLScenarioMonster;

// Per-map model precache, owned by the scenario rules. Precache is called only
// in the engine's map loading phase. All paths use the game filesystem; this
// layer never opens an archive or depends on an external resource directory.
class CCSOLMonsterModels
{
public:
    bool Precache(const csol::scenario::MonsterTypeTable& types, csol::scenario::ConfigError& error);
    bool Reset();
    bool Ready() const { return m_ready; }
    int ModelIndex(const csol::scenario::MonsterAppearance& appearance) const;
    // Sound is precached with the map's effective appearances. Empty sound
    // entries deliberately emit nothing; never precache from a death callback.
    bool EmitDeathSound(CCSOLScenarioMonster& monster,
        const csol::scenario::MonsterAppearance& appearance) const;

    // Apply to a newly acquired, inactive slot before behavior reset. Original
    // order is attributes -> model -> appearance bounds. Position/angles,
    // animation and enabling damage/Think belong to the later behavior stage.
    // On engine callback failure the caller must abandon this spawn; existing
    // entity writes are not a transaction. Output is committed only on success.
    bool Apply(CCSOLScenarioMonster& monster, const csol::scenario::RoundMonsterDeltas& delta,
        csol::scenario::MonsterAttributes& output, csol::scenario::ConfigError& error,
        bool (*sessionCheck)(const void*) = nullptr, const void* sessionContext = nullptr);

private:
    std::array<int, csol::scenario::MonsterAppearanceCount> m_indices{};
    std::array<bool, csol::scenario::MonsterAppearanceCount> m_deathSounds{};
    bool m_ready{};
    bool m_busy{};
};
