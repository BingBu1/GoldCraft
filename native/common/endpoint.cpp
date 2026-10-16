#include <winsock2.h>
#include <ws2tcpip.h>
#include "goldcraft/endpoint.hpp"
#include "goldcraft/shared_bulk.hpp"
#include "goldcraft/local_qpc_proof.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <string_view>

namespace goldcraft {
using Clock = std::chrono::steady_clock;
namespace {
struct PeerClosed : std::exception {
    const char* what() const noexcept override { return "peer disconnected"; }
};
std::uint64_t nanos(Clock::duration elapsed) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}
void count_frame(std::array<TransportCount, wire_type_count>& counts, Type type, std::size_t bytes) {
    auto& count = counts[static_cast<std::size_t>(type)]; ++count.frames; count.bytes += bytes;
}
}
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
    std::unique_ptr<SharedBulk> bulk;
    std::unique_ptr<LocalQpcProof> clock_proof;
    std::uint64_t proven_clock_frequency = 0;
    TransportStats stats{};
    std::uint64_t generation = 0, out_sequence = 0;
    Bytes input;
    struct Outbound { Bytes bytes; Type type; std::size_t payload_bytes; Clock::time_point queued; };
    std::deque<Outbound> output;
    std::size_t output_offset = 0, queued_bytes = 0, incoming_bytes = 0;
    std::vector<Message> messages;
    std::string last_error;
    Clock::time_point last_receive{}, last_heartbeat{};

    void update_ages() {
        stats.queue_age_ns = output.empty() ? 0 : nanos(Clock::now() - output.front().queued);
        stats.pending_age_ns = output_offset ? stats.queue_age_ns : 0;
        stats.peak_queue_age_ns = std::max(stats.peak_queue_age_ns, stats.queue_age_ns);
        stats.peak_pending_age_ns = std::max(stats.peak_pending_age_ns, stats.pending_age_ns);
    }

    void disconnect(const std::string& reason) {
        if (peer != INVALID_SOCKET) { closesocket(peer); ++stats.disconnects; }
        update_ages();
        clock_proof.reset(); proven_clock_frequency = 0;
        peer = INVALID_SOCKET; bulk.reset(); handshake.reset(); input.clear(); output.clear(); messages.clear();
        stats.shared_active = false; stats.queue_age_ns = stats.pending_age_ns = 0;
        output_offset = queued_bytes = incoming_bytes = 0; out_sequence = 0;
        if (!reason.empty()) last_error = reason;
    }
    bool queue(Type type, std::span<const std::uint8_t> payload) {
        if (payload.size() + header_bytes > max_queued_bytes - queued_bytes) { ++stats.rejections; return false; }
        const auto begin = Clock::now();
        auto frame = encode_frame(type, handshake->epoch(), ++out_sequence, payload);
        stats.encode_ns += nanos(Clock::now() - begin);
        queued_bytes += frame.size();
        stats.queued_high_water = std::max(stats.queued_high_water, static_cast<std::uint64_t>(queued_bytes));
        output.push_back({std::move(frame), type, payload.size(), begin}); return true;
    }
    void flush() {
        std::size_t budget = 4 * 1024 * 1024;
        while (!output.empty() && budget) {
            update_ages();
            auto& head = output.front();
            auto size = std::min(head.bytes.size() - output_offset, budget);
            const auto begin = Clock::now();
            int n = ::send(peer, reinterpret_cast<const char*>(head.bytes.data() + output_offset), static_cast<int>(size), 0);
            stats.socket_ns += nanos(Clock::now() - begin);
            if (n == SOCKET_ERROR) { if (WSAGetLastError() == WSAEWOULDBLOCK) return; throw ProtocolError("socket write failed"); }
            if (n <= 0) throw PeerClosed();
            output_offset += n; queued_bytes -= n; budget -= n;
            stats.socket_bytes_sent += n;
            if (output_offset == head.bytes.size()) {
                count_frame(stats.tcp_sent, head.type, head.payload_bytes);
                output.pop_front(); output_offset = 0;
            }
        }
        update_ages();
    }
    void clock_ready(bool accepted) {
        Writer reply;
        reply.u32(LocalQpcProof::layout); reply.u32(accepted ? 1u : 0u);
        reply.u64(handshake->epoch()); reply.u64(accepted ? proven_clock_frequency : 0);
        if (!queue(Type::clock_ready, reply.data)) throw ProtocolError("clock ready queue full");
    }
    void negotiate_clock() {
        if (config.host != Role::host_server || config.peer != Role::fabric_server ||
            !(handshake->peer_capabilities() & capability_local_qpc_v1)) return;
        const auto disabled = std::getenv("GOLDCRAFT_LOCAL_QPC");
        if (config.local_qpc && !(disabled && std::string_view(disabled) == "0")) {
            try { clock_proof = LocalQpcProof::create(handshake->epoch()); }
            catch (const std::exception&) { clock_proof.reset(); }
        }
        if (!clock_proof) { clock_ready(false); return; }
        if (!queue(Type::clock_offer, clock_proof->offer())) throw ProtocolError("clock offer queue full");
    }

    void negotiate_bulk() {
        if (config.host != Role::host_client || config.peer != Role::fabric_client ||
            !(handshake->peer_capabilities() & capability_shared_bulk_v1)) return;
        const auto disabled = std::getenv("GOLDCRAFT_SHARED_MEMORY");
        if (!config.shared_bulk || (disabled && std::string_view(disabled) == "0")) { ++stats.mapping_fallback; return; }
        try { bulk = SharedBulk::create(handshake->epoch()); }
        catch (const std::exception&) { bulk.reset(); }
        if (!bulk) { ++stats.mapping_fallback; return; }
        if (!queue(Type::bulk_offer, bulk->offer())) throw ProtocolError("shared bulk offer queue full");
        ++stats.mapping_offered;
    }
    void receive() {
        std::uint8_t buffer[65536];
        for (int attempt = 0; attempt < 64; ++attempt) {
            const auto available = std::min(sizeof(buffer), max_queued_bytes - input.size());
            if (!available) break;
            const auto begin = Clock::now();
            int n = recv(peer, reinterpret_cast<char*>(buffer), static_cast<int>(available), 0);
            stats.socket_ns += nanos(Clock::now() - begin);
            if (n == SOCKET_ERROR) { if (WSAGetLastError() == WSAEWOULDBLOCK) break; throw ProtocolError("socket read failed"); }
            if (!n) throw PeerClosed();
            if (input.size() + n > max_queued_bytes) throw ProtocolError("receive buffer limit");
            stats.socket_bytes_received += n;
            const auto copy_begin = Clock::now();
            input.insert(input.end(), buffer, buffer + n);
            stats.copy_ns += nanos(Clock::now() - copy_begin);
        }
        std::size_t offset = 0, delivered_bytes = 0;
        for (int parsed = 0; parsed < 256 && input.size() - offset >= header_bytes; ++parsed) {
            auto remaining = std::span<const std::uint8_t>(input).subspan(offset);
            auto header = decode_header(remaining.first(header_bytes));
            if (remaining.size() < header_bytes + header.payload_bytes) break;
            auto payload = remaining.subspan(header_bytes, header.payload_bytes);
            bool was_ready = handshake->ready();
            // A reference may expand to 32 MiB: bound the owned application queue,
            // then leave the complete TCP frame pending until the caller drains it.
            std::size_t application_bytes = payload.size();
            if (header.type == Type::bulk_ref) {
                Reader ref(payload); ref.u32(); application_bytes = ref.u32(); ref.u32(); ref.u32(); ref.finish();
                if (application_bytes < bulk_min_payload || application_bytes > max_payload)
                    throw ProtocolError("invalid shared bulk reference length");
            }
            const bool application = was_ready && header.type != Type::heartbeat && !bulk_internal(header.type) && !clock_internal(header.type);
            if ((application || header.type == Type::bulk_ref) &&
                (application_bytes > max_queued_bytes - incoming_bytes || messages.size() >= 4096)) break;
            // As with socket work, limit copy/drain work per game-thread poll. One
            // large frame is indivisible; a following frame waits for the next poll.
            if ((application || header.type == Type::bulk_ref) && delivered_bytes &&
                application_bytes > 4 * 1024 * 1024 - std::min(delivered_bytes, std::size_t(4 * 1024 * 1024))) break;
            handshake->accept(header, payload); last_receive = Clock::now();
            count_frame(stats.tcp_received, header.type, payload.size());
            if (!was_ready) {
                ++generation;
                if (!queue(Type::welcome, handshake->welcome())) throw ProtocolError("welcome queue full");
                negotiate_bulk(); negotiate_clock();
            }
            else if (header.type == Type::clock_ack) {
                if (!clock_proof) throw ProtocolError("unsolicited local clock response");
                const bool accepted = clock_proof->acknowledge(payload);
                proven_clock_frequency = accepted ? clock_proof->frequency() : 0;
                clock_proof.reset();
                clock_ready(accepted);
            }
            else if (clock_internal(header.type)) throw ProtocolError("unexpected local clock control");
            else if (header.type == Type::bulk_ack) {
                if (!bulk) throw ProtocolError("unsolicited shared bulk acknowledgement");
                bulk->acknowledge(payload);
                if (bulk->enabled()) { ++stats.mapping_enabled; stats.shared_active = true; }
                else { ++stats.mapping_fallback; bulk.reset(); }
            }
            else if (header.type == Type::bulk_ref) {
                if (!bulk || !bulk->enabled()) throw ProtocolError("shared bulk reference before acknowledgement");
                const auto begin = Clock::now();
                auto message = bulk->consume(payload);
                stats.copy_ns += nanos(Clock::now() - begin);
                stats.ring_high_water = std::max(stats.ring_high_water, static_cast<std::uint64_t>(bulk->high_water()));
                count_frame(stats.shared_received, message.type, message.payload.size());
                incoming_bytes += message.payload.size(); messages.push_back(std::move(message));
            }
            else if (header.type == Type::bulk_offer) throw ProtocolError("unexpected shared bulk offer");
            else if (header.type != Type::heartbeat) {
                const auto begin = Clock::now();
                messages.push_back({header.type, Bytes(payload.begin(), payload.end())}); incoming_bytes += payload.size();
                stats.copy_ns += nanos(Clock::now() - begin);
            }
            stats.incoming_high_water = std::max(stats.incoming_high_water, static_cast<std::uint64_t>(incoming_bytes));
            if (application || header.type == Type::bulk_ref) delivered_bytes += application_bytes;
            offset += header_bytes + header.payload_bytes;
        }
        if (offset) {
            const auto begin = Clock::now(); input.erase(input.begin(), input.begin() + offset);
            stats.copy_ns += nanos(Clock::now() - begin);
        }
    }
};

Endpoint::Endpoint() : impl_(std::make_unique<Impl>()) {}
Endpoint::~Endpoint() { stop(); }
void Endpoint::start(const EndpointConfig& config) {
    stop(); auto& s = *impl_; s.config = config; s.last_error.clear(); s.stats = {};
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
    const auto begin = Clock::now();
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
        if (s.peer == INVALID_SOCKET) { s.stats.drain_ns += nanos(Clock::now() - begin); return; }
        s.flush(); s.receive();
        auto now = Clock::now();
        auto timeout = std::chrono::seconds(s.handshake->ready() ? 10 : 3);
        if (now - s.last_receive > timeout) throw ProtocolError("peer timeout");
        if (s.handshake->ready() && now - s.last_heartbeat > std::chrono::seconds(1)) {
            if (!s.queue(Type::heartbeat, {})) throw ProtocolError("heartbeat queue full");
            s.last_heartbeat = now;
        }
        s.flush();
    } catch (const PeerClosed& e) { s.disconnect(e.what()); }
    catch (const std::exception& e) { ++s.stats.rejections; s.disconnect(e.what()); }
    s.stats.drain_ns += nanos(Clock::now() - begin);
}
bool Endpoint::connected() const { return impl_->handshake && impl_->handshake->ready(); }
std::uint64_t Endpoint::generation() const { return impl_->generation; }
std::uint64_t Endpoint::local_qpc_frequency() const { return connected() ? impl_->proven_clock_frequency : 0; }
bool Endpoint::status_attributes() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_status_attributes) != 0;
}
bool Endpoint::status_vision() const {
    return connected() && impl_->config.host==Role::host_server && impl_->config.peer==Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_status_vision) != 0;
}
bool Endpoint::status_visibility() const {
    return connected() && impl_->config.host==Role::host_server && impl_->config.peer==Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_status_visibility) != 0;
}
bool Endpoint::status_motion() const {
    return connected() && impl_->config.host==Role::host_server && impl_->config.peer==Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_status_motion) != 0;
}
bool Endpoint::status_environment() const {
    return connected() && impl_->config.host==Role::host_server && impl_->config.peer==Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_status_environment) != 0;
}
bool Endpoint::player_gameplay() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_player_gameplay) != 0;
}
bool Endpoint::skill_guard() const {
    return player_gameplay() && (impl_->handshake->peer_capabilities() & capability_skill_guard) != 0;
}
bool Endpoint::native_armor() const {
    return status_attributes() && (impl_->handshake->peer_capabilities() & capability_native_armor) != 0;
}
bool Endpoint::fluid_volumes() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_fluid_volumes) != 0;
}
bool Endpoint::status_effects() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_status_effects) != 0;
}
bool Endpoint::gameplay_rules() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_gameplay_rules) != 0;
}
bool Endpoint::host_explosions() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_host_explosions) != 0;
}
bool Endpoint::tnt_damage() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_tnt_damage) != 0;
}
bool Endpoint::effect_position_hold() const {
    return connected() && impl_->config.host == Role::host_server && impl_->config.peer == Role::fabric_server &&
        (impl_->handshake->peer_capabilities() & capability_effect_position_hold) != 0;
}
std::uint16_t Endpoint::port() const { return impl_->config.port; }
const std::string& Endpoint::error() const { return impl_->last_error; }
std::vector<Message> Endpoint::take_messages() { std::vector<Message> result; result.swap(impl_->messages); impl_->incoming_bytes = 0; return result; }
bool Endpoint::send(Type t, std::span<const std::uint8_t> p) {
    if (!connected() || t == Type::hello || t == Type::welcome || bulk_internal(t) || clock_internal(t)) return false;
    try { return impl_->queue(t, p); } catch (const std::exception& e) { impl_->disconnect(e.what()); return false; }
}
TransportStats Endpoint::transport_stats() const {
    auto result = impl_->stats;
    result.queued_bytes = impl_->queued_bytes; result.incoming_bytes = impl_->incoming_bytes;
    result.queue_age_ns = impl_->output.empty() ? 0 : nanos(Clock::now() - impl_->output.front().queued);
    result.pending_age_ns = impl_->output_offset ? result.queue_age_ns : 0;
    result.peak_queue_age_ns = std::max(result.peak_queue_age_ns, result.queue_age_ns);
    result.peak_pending_age_ns = std::max(result.peak_pending_age_ns, result.pending_age_ns);
    return result;
}
}
