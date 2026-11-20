// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_resources.hpp"
#include "native/guest/cof/famas_resources.hpp"

namespace csol::server::cof
{
enum class FamasResourcePart : unsigned
{
    SequenceTimes, Aim, PlayerModel, NullModel, ShellModel, ClipModel,
    LegacyFire, FirstDraw, ItemGet, Fire, Count
};
struct FamasResources
{
    std::uint64_t generation{};
    guest::cof::MagazineSequenceTimes sequences;
    Vector aimOffset{};
    int aimZoom{};
    std::array<int, guest::cof::resources::FamasModels.size()> models{};
    std::array<int, guest::cof::resources::FamasSounds.size()> sounds{};
    int viewModelString{}, playerModelString{}, worldModelString{}, inventoryItemString{};
    int inventoryDownload{};
    unsigned preparedParts{};
    std::array<ResourceResult, static_cast<unsigned>(FamasResourcePart::Count)> parts{};
    bool Ready(FamasResourcePart part) const noexcept
    {
        const auto i=static_cast<unsigned>(part);
        return i<parts.size() && (preparedParts & (1u<<i));
    }
};

// Valid V/W and inventory identity are the mechanical closure. Optional
// presentation is reported per part; only a complete current generation is
// committed. Client effect/HUD preparation is a separate cold stage.
ResourceResult PrepareFamasClientAudio(const ResourceAuthority&, bool m16AudioPrepared) noexcept;
ResourceResult PrepareFamasResources(const ResourceAuthority&, FamasResources&) noexcept;
}
