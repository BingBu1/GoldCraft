#pragma once
#include "goldcraft/wire.hpp"
#include <array>
#include <optional>

namespace goldcraft {
enum class PairStatus : std::uint32_t { ok, wrong_world, missing_player, stale_player, wrong_token, uuid_in_use, already_bound, invalid_uuid };
struct PlayerBinding {
    std::uint32_t slot = 0, serial = 0, userid = 0;
    Key token{}, uuid{};
    bool paired = false;
};
class PairRegistry {
public:
    void new_world(std::uint64_t world) { world_ = world; players_ = {}; }
    std::uint64_t world() const { return world_; }
    const PlayerBinding& connect(std::uint32_t slot, std::uint32_t serial, std::uint32_t userid) {
        if (!world_ || !slot || slot >= players_.size()) throw ProtocolError("invalid player slot/world");
        players_[slot] = PlayerBinding{slot, serial, userid, random_key(), {}, false};
        return *players_[slot];
    }
    void disconnect(std::uint32_t slot) { if (slot < players_.size()) players_[slot].reset(); }
    const PlayerBinding* get(std::uint32_t slot) const {
        return slot < players_.size() && players_[slot] ? &*players_[slot] : nullptr;
    }
    PairStatus bind(std::uint64_t world, std::uint32_t slot, std::uint32_t serial, const Key& token, const Key& uuid) {
        if (world != world_) return PairStatus::wrong_world;
        const auto* current = get(slot);
        if (!current) return PairStatus::missing_player;
        if (serial != current->serial) return PairStatus::stale_player;
        if (token != current->token) return PairStatus::wrong_token;
        if (uuid == Key{}) return PairStatus::invalid_uuid;
        for (const auto& p : players_) if (p && p->slot != slot && p->paired && p->uuid == uuid) return PairStatus::uuid_in_use;
        if (current->paired && current->uuid != uuid) return PairStatus::already_bound;
        players_[slot]->uuid = uuid; players_[slot]->paired = true; return PairStatus::ok;
    }
private:
    std::uint64_t world_ = 0;
    std::array<std::optional<PlayerBinding>, 65> players_{};
};
}
