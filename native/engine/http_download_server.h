#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace goldcraft::http {
using Bytes = std::vector<std::uint8_t>;
using Body = std::shared_ptr<const Bytes>;
using Provider = std::function<Body(const std::string&)>;
// The resource-list size is only a 24-bit progress estimate. Ordinary HTTP
// downloads use the response body length; bound them by the store's budget.
constexpr std::size_t max_file_size = 64u * 1024u * 1024u;
bool resource_name(const std::string& name);
// Accept only an explicit http URL. TLS may terminate at the tunnel proxy.
bool public_url(const std::string& url);
struct Stats { std::uint64_t requests = 0, bytes = 0, rejected = 0; };

class Server {
public:
    Server();
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    // Provider is invoked on a worker, and must return promptly on shutdown.
    // bind must be a numeric IPv4 address. Port 0 is for isolated tests only.
    bool start(const std::string& bind, unsigned short port, Provider provider);
    void stop();
    // Only the in-process game module's loopback service is reachable here.
    // Changing/clearing it cancels outstanding spray transfers on both sides.
    void spray_backend(unsigned short port);
    unsigned short port() const;
    Stats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace goldcraft::http
