#pragma once
#include "goldcraft/wire.hpp"
#include <memory>
#include <optional>

namespace goldcraft {
struct EndpointConfig {
    std::uint16_t port = 0;
    Key session{}, token{};
    Role host = Role::host_client, peer = Role::fabric_client;
    bool shared_bulk = true;
    bool local_qpc = true;
};
std::optional<EndpointConfig> environment_config(const char* prefix, Role host, Role peer);

struct TransportCount { std::uint64_t frames = 0, bytes = 0; };
struct TransportStats {
    // Indexed by stable wire Type. Counts include internal TCP control frames.
    std::array<TransportCount, wire_type_count> tcp_sent{}, tcp_received{}, shared_sent{}, shared_received{};
    std::uint64_t mapping_offered = 0, mapping_enabled = 0, mapping_fallback = 0;
    std::uint64_t rejections = 0, disconnects = 0, ring_high_water = 0;
    std::uint64_t queued_bytes = 0, queued_high_water = 0, incoming_bytes = 0, incoming_high_water = 0;
    std::uint64_t queue_age_ns = 0, peak_queue_age_ns = 0, pending_age_ns = 0, peak_pending_age_ns = 0;
    std::uint64_t encode_ns = 0, copy_ns = 0, socket_ns = 0, drain_ns = 0;
    std::uint64_t socket_bytes_sent = 0, socket_bytes_received = 0;
    bool shared_active = false;
};

// Nonblocking loopback transport. Only call from its owning game thread.
class Endpoint {
public:
    Endpoint();
    ~Endpoint();
    Endpoint(const Endpoint&) = delete;
    Endpoint& operator=(const Endpoint&) = delete;
    void start(const EndpointConfig& config);
    void stop();
    void poll();
    bool connected() const;
    std::uint64_t generation() const;
    // Nonzero only after an authenticated same-host proof on this connection.
    std::uint64_t local_qpc_frequency() const;
    // Connection latch; never downgraded to legacy poses if the clock fails.
    bool status_effects() const;
    bool status_attributes() const;
    bool status_environment() const;
    bool status_fall() const;
    bool status_health() const;
    bool status_motion() const;
    bool status_visibility() const;
    bool status_vision() const;
    bool view_distortion() const;
    bool entity_outlines() const;
    bool player_gameplay() const;
    bool skill_guard() const;
    bool fluid_volumes() const;
    bool native_armor() const;
    bool gameplay_rules() const;
    bool host_explosions() const;
    bool tnt_damage() const;
    bool effect_position_hold() const;
    std::uint16_t port() const;
    const std::string& error() const;
    std::vector<Message> take_messages();
    bool send(Type type, std::span<const std::uint8_t> payload);
    TransportStats transport_stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
