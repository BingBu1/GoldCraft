#include "goldcraft/endpoint.hpp"
#ifndef GOLDCRAFT_ORIGINAL_BASELINE
#include "goldcraft/shared_bulk.hpp"
#endif
#include <windows.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

using namespace goldcraft;
using Clock = std::chrono::steady_clock;
namespace {
#ifdef GOLDCRAFT_ORIGINAL_BASELINE
struct HarnessStats {};
#else
using HarnessStats = TransportStats;
#endif
std::uint32_t fast_crc(std::span<const std::uint8_t> payload) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            auto c = n; for (int bit = 0; bit < 8; ++bit) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
            values[n] = c;
        }
        return values;
    }();
    std::uint32_t crc = UINT32_MAX;
    for (auto b : payload) crc = table[(crc ^ b) & 255] ^ (crc >> 8);
    return ~crc;
}
#ifndef GOLDCRAFT_ORIGINAL_BASELINE
void counts(std::ostream& out, std::string_view name, const std::array<TransportCount, 70>& values) {
    out << ",\"" << name << "\":{";
    bool first = true;
    for (std::size_t i = 0; i < values.size(); ++i) if (values[i].frames) {
        if (!first) out << ','; first = false;
        out << '"' << i << "\":{\"frames\":" << values[i].frames << ",\"bytes\":" << values[i].bytes << '}';
    }
    out << '}';
}
#endif
void stats_json(std::ostream& out, const HarnessStats& s, std::uint64_t total, DWORD handles_before, DWORD handles_after) {
#ifdef GOLDCRAFT_ORIGINAL_BASELINE
    (void)s;
    out << "{\"baselineWire\":26,\"messages\":" << total << ",\"handlesBefore\":" << handles_before << ",\"handlesAfter\":" << handles_after << '}';
#else
    out << "{\"messages\":" << total << ",\"offered\":" << s.mapping_offered << ",\"enabled\":" << s.mapping_enabled
        << ",\"fallback\":" << s.mapping_fallback << ",\"rejections\":" << s.rejections << ",\"disconnects\":" << s.disconnects
        << ",\"ringHighWater\":" << s.ring_high_water << ",\"queueHighWater\":" << s.queued_high_water
        << ",\"incomingHighWater\":" << s.incoming_high_water << ",\"peakQueueAgeNs\":" << s.peak_queue_age_ns
        << ",\"peakPendingAgeNs\":" << s.peak_pending_age_ns << ",\"encodeNs\":" << s.encode_ns
        << ",\"copyNs\":" << s.copy_ns << ",\"socketNs\":" << s.socket_ns << ",\"drainNs\":" << s.drain_ns
        << ",\"socketBytesSent\":" << s.socket_bytes_sent << ",\"socketBytesReceived\":" << s.socket_bytes_received
        << ",\"handlesBefore\":" << handles_before << ",\"handlesAfter\":" << handles_after;
    counts(out, "tcpSent", s.tcp_sent); counts(out, "tcpReceived", s.tcp_received); counts(out, "sharedReceived", s.shared_received);
    out << '}';
#endif
}
}
int main(int argc, char** argv) {
    try {
        std::uint64_t target = 0, timeout_ms = 30000, pause_ms = 0, sessions = 1;
        bool tcp_only = false, replies = true, quiet = false;
        std::string stats_path;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--tcp-only") tcp_only = true;
            else if (arg == "--no-replies") replies = false;
            else if (arg == "--quiet") quiet = true;
            else {
                if (++i >= argc) throw ProtocolError("missing argument");
                if (arg == "--stats") stats_path = argv[i];
                else if (arg == "--messages") target = std::stoull(argv[i]);
                else if (arg == "--timeout-ms") timeout_ms = std::stoull(argv[i]);
                else if (arg == "--pause-ms") pause_ms = std::stoull(argv[i]);
                else if (arg == "--sessions") sessions = std::stoull(argv[i]);
                else throw ProtocolError("unknown argument");
            }
        }
        Endpoint endpoint;
        EndpointConfig config{0, parse_key("00112233445566778899aabbccddeeff"), parse_key("ffeeddccbbaa99887766554433221100")};
#ifndef GOLDCRAFT_ORIGINAL_BASELINE
        config.shared_bulk = !tcp_only;
#else
        (void)tcp_only;
#endif
        endpoint.start(config);
        // Warm platform crypto/security initialization before measuring mapping lifetime.
#ifndef GOLDCRAFT_ORIGINAL_BASELINE
        { auto warm = SharedBulk::create(1); if (!warm) throw ProtocolError("mapping warmup failed"); }
#else
        (void)random_epoch();
#endif
        DWORD handles_before = 0; GetProcessHandleCount(GetCurrentProcess(), &handles_before);
        std::cout << "READY " << endpoint.port() << std::endl;
        const auto begin = Clock::now();
        auto finish = Clock::time_point::max(), connected_at = Clock::time_point::max();
        std::uint64_t total = 0, seen_generation = 0;
        bool paused = false;
        for (;;) {
            endpoint.poll();
            if (endpoint.generation() != seen_generation) {
                seen_generation = endpoint.generation(); connected_at = Clock::now();
                if (!quiet) std::cout << "CONNECTED " << seen_generation << std::endl;
            }
            for (auto& message : endpoint.take_messages()) {
                const auto crc = fast_crc(message.payload); ++total;
                if (!quiet) std::cout << "MESSAGE " << seen_generation << ' ' << static_cast<unsigned>(message.type) << ' '
                    << message.payload.size() << ' ' << crc << std::endl;
                if (replies) {
                    Writer response; response.u32(static_cast<unsigned>(message.type)); response.u32(static_cast<std::uint32_t>(message.payload.size())); response.u32(crc);
                    if (!endpoint.send(Type::damage_result, response.data)) throw ProtocolError("reply queue rejected");
                }
            }
            const auto now = Clock::now();
            bool shared_active = false;
#ifndef GOLDCRAFT_ORIGINAL_BASELINE
            shared_active = endpoint.transport_stats().shared_active;
#endif
            if (!paused && pause_ms && endpoint.connected() &&
                (shared_active || now - connected_at > std::chrono::milliseconds(150))) {
                paused = true;
                if (!quiet) std::cout << "PAUSED " << pause_ms << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
            }
            if (target && total >= target && seen_generation >= sessions && finish == Clock::time_point::max())
                finish = now + std::chrono::milliseconds(300);
            if (now >= finish || now - begin >= std::chrono::milliseconds(timeout_ms)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#ifndef GOLDCRAFT_ORIGINAL_BASELINE
        const auto stats = endpoint.transport_stats();
#else
        const HarnessStats stats{};
#endif
        const auto failure = endpoint.error();
        endpoint.stop();
        DWORD handles_after = 0; GetProcessHandleCount(GetCurrentProcess(), &handles_after);
        std::cout << "STATS "; stats_json(std::cout, stats, total, handles_before, handles_after); std::cout << std::endl;
        if (!stats_path.empty()) { std::ofstream file(stats_path); stats_json(file, stats, total, handles_before, handles_after); file << '\n'; }
        if (!failure.empty()) std::cout << "DISCONNECTED " << failure << std::endl;
        return target && total < target ? 2 : 0;
    } catch (const std::exception& e) { std::cerr << "FAILED " << e.what() << std::endl; return 1; }
}
