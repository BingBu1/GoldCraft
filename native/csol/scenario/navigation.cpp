// SPDX-License-Identifier: GPL-3.0-or-later
#include "navigation.hpp"

#include <bit>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace csol::scenario
{
namespace
{
class Reader
{
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}
    std::size_t Offset() const { return offset_; }
    std::size_t Remaining() const { return bytes_.size() - offset_; }
    bool U8(std::uint8_t& value)
    {
        if (!Remaining()) return false;
        value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
        return true;
    }
    bool U16(std::uint16_t& value)
    {
        std::uint8_t low{}, high{};
        if (!U8(low) || !U8(high)) return false;
        value = static_cast<std::uint16_t>(low | (high << 8));
        return true;
    }
    bool U32(std::uint32_t& value)
    {
        std::uint16_t low{}, high{};
        if (!U16(low) || !U16(high)) return false;
        value = low | (static_cast<std::uint32_t>(high) << 16);
        return true;
    }
    bool Float(float& value)
    {
        std::uint32_t bits{};
        if (!U32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }
    bool Point(NavPoint& value) { return Float(value.x) && Float(value.y) && Float(value.z); }
    bool Place(std::string& value)
    {
        std::uint16_t size{};
        if (!U16(size) || !size || size > Remaining()) return false;
        const auto* start = reinterpret_cast<const char*>(bytes_.data() + offset_);
        if (start[size - 1] != '\0') return false;
        value.assign(start, size - 1);
        if (value.find('\0') != std::string::npos) return false;
        offset_ += size;
        return true;
    }
private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};
}

bool ReadNavigationV5(std::span<const std::byte> bytes, NavigationFile& output,
    ConfigError& error)
{
    error = {};
    Reader reader(bytes);
    const auto fail = [&](const char* message)
    {
        error = {reader.Offset(), message};
        return false;
    };
    std::uint32_t magic{}, version{};
    if (!reader.U32(magic) || magic != 0xFEEDFACEu) return fail("invalid navigation magic");
    if (!reader.U32(version) || version != 5) return fail("navigation version requires a separate legacy loader (expected v5)");
    NavigationFile candidate;
    if (!reader.U32(candidate.bspSize)) return fail("truncated navigation BSP size");
    std::uint16_t placeCount{};
    if (!reader.U16(placeCount) || placeCount > reader.Remaining() / 3)
        return fail("truncated navigation place directory");
    candidate.places.resize(placeCount);
    for (auto& place : candidate.places)
        if (!reader.Place(place)) return fail("invalid navigation place name");
    std::uint32_t areaCount{};
    // Minimum v5 area: id/flags, eight floats, four counts, hide/approach
    // counts, encounter count and place. Count zero is valid in original MP.
    if (!reader.U32(areaCount) || areaCount > reader.Remaining() / 61)
        return fail("truncated navigation area table");
    candidate.areas.reserve(areaCount);
    std::unordered_set<std::uint32_t> areaIds, spotIds;
    for (std::uint32_t index = 0; index < areaCount; ++index)
    {
        auto& area = candidate.areas.emplace_back();
        if (!reader.U32(area.id) || !reader.U8(area.attributes) ||
            !reader.Point(area.low) || !reader.Point(area.high) ||
            !reader.Float(area.northEastZ) || !reader.Float(area.southWestZ))
            return fail("invalid navigation area geometry");
        if (!areaIds.insert(area.id).second) return fail("duplicate navigation area id");
        // Preserve reversed corners. The original grid may insert no cells
        // for these records; sorting the coordinates changes the map.
        for (auto& connections : area.connections)
        {
            std::uint32_t count{};
            if (!reader.U32(count) || count > reader.Remaining() / 4)
                return fail("truncated navigation connections");
            connections.resize(count);
            for (auto& id : connections)
                if (!reader.U32(id)) return fail("truncated navigation connection id");
        }
        std::uint8_t count{};
        if (!reader.U8(count) || count > reader.Remaining() / 17)
            return fail("truncated navigation hiding spots");
        area.hidingSpots.resize(count);
        for (auto& spot : area.hidingSpots)
        {
            if (!reader.U32(spot.id) || !reader.Point(spot.position) || !reader.U8(spot.flags))
                return fail("invalid navigation hiding spot");
            if (!spotIds.insert(spot.id).second) return fail("duplicate navigation hiding spot id");
        }
        if (!reader.U8(count) || count > 16 || count > reader.Remaining() / 14)
            return fail("invalid navigation approach count");
        area.approaches.resize(count);
        for (auto& approach : area.approaches)
        {
            if (!reader.U32(approach.here) || !reader.U32(approach.previous) ||
                !reader.U8(approach.previousHow) || !reader.U32(approach.next) ||
                !reader.U8(approach.nextHow) || approach.previousHow > 7 || approach.nextHow > 7)
                return fail("invalid navigation approach");
            // NUM_TRAVERSE_TYPES=7 is the stored "unspecified" sentinel.
            // zs_deadend contains it; preserve it as metadata, not an edge.
        }
        std::uint32_t encounters{};
        if (!reader.U32(encounters) || encounters > reader.Remaining() / 11)
            return fail("truncated navigation encounters");
        area.encounters.resize(encounters);
        for (auto& encounter : area.encounters)
        {
            if (!reader.U32(encounter.from) || !reader.U8(encounter.fromDirection) ||
                !reader.U32(encounter.to) || !reader.U8(encounter.toDirection) ||
                !reader.U8(count) || count > reader.Remaining() / 5 ||
                encounter.fromDirection >= 4 || encounter.toDirection >= 4)
                return fail("invalid navigation encounter");
            encounter.spots.resize(count);
            for (auto& spot : encounter.spots)
                if (!reader.U32(spot.id) || !reader.U8(spot.position))
                    return fail("truncated navigation encounter spot");
        }
        if (!reader.U16(area.place)) return fail("truncated navigation place index");
    }
    // Unresolved IDs (including zero) are resolved to null by the owner.
    // Original PostLoad reports them but its caller still accepts the map.
    candidate.trailingBytes = reader.Remaining();
    output = std::move(candidate);
    return true;
}
}
