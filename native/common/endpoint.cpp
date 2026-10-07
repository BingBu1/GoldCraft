#include <winsock2.h>
#include <ws2tcpip.h>
#include "goldcraft/endpoint.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>

namespace goldcraft {
using Clock = std::chrono::steady_clock;
std::optional<EndpointConfig> environment_config(const char* prefix, Role host, Role peer) {
    const std::string base(prefix);
    const char* p = std::getenv((base + "_PORT").c_str());
    const char* s = std::getenv((base + "_SESSION").c_str());
    const char* t = std::getenv((base + "_TOKEN").c_str());
    if (!p && !s && !t) return std::nullopt;
    if (!p || !s || !t) throw ProtocolError("incomplete endpoint environment");
    char* end = nullptr; auto n = std::strtoul(p, &end, 10);
    if (!*p || *end || n < 1024 || n > 65535) throw ProtocolError("invalid endpoint port");
    return EndpointConfig{ static_cast<std::uint16_t>(n), parse_key(s), parse_key(t), host, peer };
}

struct Endpoint::Impl {
    SOCKET listener = INVALID_SOCKET, peer = INVALID_SOCKET;
    EndpointConfig config{};
    bool winsock = false;
    std::unique_ptr<HostHandshake> handshake;
    std::uint64_t generation = 0, out_sequence = 0;
    Bytes input;
    std::deque<Bytes> output;
    std::size_t output_offset = 0, queued_bytes = 0, incoming_bytes = 0;
    std::vector<Message> messages;
    std::string last_error;
    Clock::time_point last_receive{}, last_heartbeat{};

    void disconnect(const std::string& reason) {
        if (peer != INVALID_SOCKET) closesocket(peer);
        peer = INVALID_SOCKET; handshake.reset(); input.clear(); output.clear(); messages.clear();
        output_offset = queued_bytes = incoming_bytes = 0; out_sequence = 0;
        if (!reason.empty()) last_error = reason;
    }
    bool queue(Type type, std::span<const std::uint8_t> payload) {
        if (payload.size() + header_bytes > max_queued_bytes - queued_bytes) return false;
        auto frame = encode_frame(type, handshake->epoch(), ++out_sequence, payload);
        queued_bytes += frame.size(); output.push_back(std::move(frame)); return true;
    }
    void flush() {
        std::size_t budget = 4 * 1024 * 1024;
        while (!output.empty() && budget) {
            auto& head = output.front();
            auto size = std::min(head.size() - output_offset, budget);
            int n = ::send(peer, reinterpret_cast<const char*>(head.data() + output_offset), static_cast<int>(size), 0);
            if (n == SOCKET_ERROR) { if (WSAGetLastError() == WSAEWOULDBLOCK) return; throw ProtocolError("socket write failed"); }
            if (n <= 0) throw ProtocolError("socket closed while writing");
            output_offset += n; queued_bytes -= n; budget -= n;
            if (output_offset == head.size()) { output.pop_front(); output_offset = 0; }
        }
    }
    void receive() {
        std::uint8_t buffer[65536];
        for (int attempt = 0; attempt < 64; ++attempt) {
            int n = recv(peer, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
            if (n == SOCKET_ERROR) { if (WSAGetLastError() == WSAEWOULDBLOCK) break; throw ProtocolError("socket read failed"); }
            if (!n) throw ProtocolError("peer disconnected");
            if (input.size() + n > max_queued_bytes) throw ProtocolError("receive buffer limit");
            input.insert(input.end(), buffer, buffer + n);
        }
        std::size_t offset = 0;
        for (int parsed = 0; parsed < 256 && input.size() - offset >= header_bytes; ++parsed) {
            auto remaining = std::span<const std::uint8_t>(input).subspan(offset);
            auto header = decode_header(remaining.first(header_bytes));
            if (remaining.size() < header_bytes + header.payload_bytes) break;
            auto payload = remaining.subspan(header_bytes, header.payload_bytes);
            bool was_ready = handshake->ready();
            handshake->accept(header, payload); last_receive = Clock::now();
            if (!was_ready) { ++generation; if (!queue(Type::welcome, handshake->welcome())) throw ProtocolError("welcome queue full"); }
            else if (header.type != Type::heartbeat) {
                if (incoming_bytes + payload.size() > max_queued_bytes || messages.size() >= 4096) throw ProtocolError("unconsumed message limit");
                messages.push_back({header.type, Bytes(payload.begin(), payload.end())}); incoming_bytes += payload.size();
            }
            offset += header_bytes + header.payload_bytes;
        }
        if (offset) input.erase(input.begin(), input.begin() + offset);
    }
};

Endpoint::Endpoint() : impl_(std::make_unique<Impl>()) {}
Endpoint::~Endpoint() { stop(); }
void Endpoint::start(const EndpointConfig& config) {
    stop(); auto& s = *impl_; s.config = config; s.last_error.clear();
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2), &data)) throw ProtocolError("WSAStartup failed");
    s.winsock = true;
    try {
        s.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s.listener == INVALID_SOCKET) throw ProtocolError("listener creation failed");
        BOOL exclusive = TRUE;
        if (setsockopt(s.listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive))) throw ProtocolError("exclusive binding failed");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(config.port); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(s.listener, 4)) throw ProtocolError("loopback port is unavailable");
        int size = sizeof(address);
        if (getsockname(s.listener, reinterpret_cast<sockaddr*>(&address), &size)) throw ProtocolError("listener address failed");
        s.config.port = ntohs(address.sin_port);
        u_long nonblocking = 1;
        if (ioctlsocket(s.listener, FIONBIO, &nonblocking)) throw ProtocolError("nonblocking listener failed");
    } catch (...) { stop(); throw; }
}
void Endpoint::stop() {
    auto& s = *impl_; s.disconnect("");
    if (s.listener != INVALID_SOCKET) closesocket(s.listener);
    s.listener = INVALID_SOCKET;
    if (s.winsock) WSACleanup();
    s.winsock = false;
}
void Endpoint::poll() {
    auto& s = *impl_; if (s.listener == INVALID_SOCKET) return;
    try {
        for (int i = 0; i < 4; ++i) {
            SOCKET accepted = accept(s.listener, nullptr, nullptr);
            if (accepted == INVALID_SOCKET) { if (WSAGetLastError() == WSAEWOULDBLOCK) break; throw ProtocolError("accept failed"); }
            if (s.peer != INVALID_SOCKET) { closesocket(accepted); continue; }
            s.peer = accepted;
            u_long nonblocking = 1;
            BOOL no_delay = TRUE;
            if (ioctlsocket(s.peer, FIONBIO, &nonblocking) || setsockopt(s.peer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay))) throw ProtocolError("peer setup failed");
            s.handshake = std::make_unique<HostHandshake>(s.config.session, s.config.token, s.config.host, s.config.peer, random_epoch());
            s.last_receive = s.last_heartbeat = Clock::now();
        }
        if (s.peer == INVALID_SOCKET) return;
        s.flush(); s.receive();
        auto now = Clock::now();
        auto timeout = std::chrono::seconds(s.handshake->ready() ? 10 : 3);
        if (now - s.last_receive > timeout) throw ProtocolError("peer timeout");
        if (s.handshake->ready() && now - s.last_heartbeat > std::chrono::seconds(1)) {
            if (!s.queue(Type::heartbeat, {})) throw ProtocolError("heartbeat queue full");
            s.last_heartbeat = now;
        }
        s.flush();
    } catch (const std::exception& e) { s.disconnect(e.what()); }
}
bool Endpoint::connected() const { return impl_->handshake && impl_->handshake->ready(); }
std::uint64_t Endpoint::generation() const { return impl_->generation; }
std::uint16_t Endpoint::port() const { return impl_->config.port; }
const std::string& Endpoint::error() const { return impl_->last_error; }
std::vector<Message> Endpoint::take_messages() { std::vector<Message> result; result.swap(impl_->messages); impl_->incoming_bytes = 0; return result; }
bool Endpoint::send(Type t, std::span<const std::uint8_t> p) {
    if (!connected() || t == Type::hello || t == Type::welcome) return false;
    try { return impl_->queue(t, p); } catch (const std::exception& e) { impl_->disconnect(e.what()); return false; }
}
}
