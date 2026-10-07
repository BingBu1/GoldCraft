#pragma once
#include "goldcraft/wire.hpp"
#include <memory>
#include <optional>

namespace goldcraft {
struct EndpointConfig {
    std::uint16_t port = 0;
    Key session{}, token{};
    Role host = Role::host_client, peer = Role::fabric_client;
};
std::optional<EndpointConfig> environment_config(const char* prefix, Role host, Role peer);

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
    std::uint16_t port() const;
    const std::string& error() const;
    std::vector<Message> take_messages();
    bool send(Type type, std::span<const std::uint8_t> payload);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
