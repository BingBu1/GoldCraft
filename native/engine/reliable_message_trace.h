// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

// This observer never owns or changes an engine buffer. Fixed storage keeps
// ordinary message writes allocation-free; only a real overflow prints.
namespace goldcraft::reliable_trace
{
inline constexpr std::size_t ClientCount = 32, RecentCount = 64, TypeCount = 256;
inline constexpr std::uint32_t CrossingBytes = 48 * 1024;
struct Count { std::uint64_t packets{}, bytes{}; };
struct TypeCountEntry { Count total{}, window{}; std::uint64_t windowId{}; };
struct Entry
{
    double time{};
    std::uint64_t window{};
    std::uint32_t before{}, wireBytes{}, prefix{};
    std::uint16_t type{}, destination{};
    std::uint8_t prefixBytes{};
};
struct OverflowEvent { double time{}; int before{}, maximum{}, requested{}; bool present{}; };
struct State
{
    const void* buffer{};
    std::uint64_t connection{}, window{1};
    Count total{}, current{};
    std::uint64_t unclassifiedBytes{};
    double windowStart{};
    std::uint32_t expectedAfter{}, peak{};
    std::array<TypeCountEntry, TypeCount> types{};
    std::array<Entry, RecentCount> recent{};
    std::size_t next{}, count{};
    Entry crossing{};
    bool crossed{}, observed{}, reported{};
    OverflowEvent overflow{};
};
inline std::array<State, ClientCount> states{};

inline std::uint64_t Add(std::uint64_t a, std::uint64_t b)
{
    return b > std::numeric_limits<std::uint64_t>::max() - a ?
        std::numeric_limits<std::uint64_t>::max() : a + b;
}
inline void Add(Count& count, std::uint32_t bytes)
{
    count.packets = Add(count.packets, 1);
    count.bytes = Add(count.bytes, bytes);
}
inline void Reset(int slot, const void* buffer)
{
    if (slot < 0 || slot >= static_cast<int>(ClientCount)) return;
    auto& state = states[slot];
    const auto generation = Add(state.connection, 1);
    state = {};
    state.buffer = buffer;
    state.connection = generation;
}
inline State* Find(int slot, const void* buffer)
{
    if (slot < 0 || slot >= static_cast<int>(ClientCount) || !buffer) return nullptr;
    auto& state = states[slot];
    return state.buffer == buffer ? &state : nullptr;
}
inline void Record(int slot, const void* buffer, int before, int maximum,
    int type, int destination, const unsigned char* payload, int payloadBytes,
    bool variable, double now)
{
    auto* state = Find(slot, buffer);
    if (!state || before < 0 || maximum <= 0 || type < 0 || type >= static_cast<int>(TypeCount) ||
        destination < 0 || destination > 65535 || payloadBytes < 0 ||
        payloadBytes > std::numeric_limits<int>::max() - 2 || (payloadBytes && !payload)) return;
    // A decrease proves the old queue drained/cleared. Equal/larger values
    // cannot prove it, so this is explicitly an observed window, not a frame.
    if (!state->observed || static_cast<std::uint32_t>(before) < state->expectedAfter)
    {
        if (state->observed) ++state->window;
        state->windowStart = now;
        state->current = {};
        state->unclassifiedBytes = static_cast<std::uint32_t>(before);
        state->observed = true;
    }
    else if (static_cast<std::uint32_t>(before) > state->expectedAfter)
        state->unclassifiedBytes = Add(state->unclassifiedBytes, before - state->expectedAfter);
    Entry entry{now, state->window, static_cast<std::uint32_t>(before),
        static_cast<std::uint32_t>(payloadBytes + 1 + (variable ? 1 : 0)), 0,
        static_cast<std::uint16_t>(type), static_cast<std::uint16_t>(destination)};
    entry.prefixBytes = static_cast<std::uint8_t>(payloadBytes < 4 ? payloadBytes : 4);
    for (unsigned i = 0; i < entry.prefixBytes; ++i)
        entry.prefix |= static_cast<std::uint32_t>(payload[i]) << (i * 8);
    state->expectedAfter = entry.before + entry.wireBytes;
    if (state->expectedAfter > state->peak) state->peak = state->expectedAfter;
    if (!state->crossed && state->expectedAfter >= CrossingBytes)
    { state->crossing = entry; state->crossed = true; }
    state->recent[state->next] = entry;
    state->next = (state->next + 1) % RecentCount;
    if (state->count < RecentCount) ++state->count;
    Add(state->total, entry.wireBytes);
    Add(state->current, entry.wireBytes);
    auto& typeCount = state->types[type];
    if (typeCount.windowId != state->window)
    { typeCount.window = {}; typeCount.windowId = state->window; }
    Add(typeCount.total, entry.wireBytes);
    Add(typeCount.window, entry.wireBytes);
}

// Templates read the actual client_t/netchan_t members at their SDK call
// sites; no copied layout, pointer arithmetic, or private field offsets.
template<class Client, class Buffer>
inline void Message(Client& client, int slot, Buffer* buffer, int type, int destination,
    const unsigned char* payload, int bytes, bool variable, double now)
{
    if (buffer != &client.netchan.message || client.fakeclient) return;
    Record(slot, buffer, buffer->cursize, buffer->maxsize, type, destination, payload, bytes, variable, now);
}

template<class Client, class Print>
inline void Report(State& state, int slot, const Client& client, Print print)
{
    if (state.reported) return;
    state.reported = true; // Console hooks must not recursively print this report.
    const auto& event = state.overflow;
    const auto& channel = client.netchan;
    print("[reliable_trace] overflow slot=%d generation=%llu time=%.6f queue=%d capacity=%d request=%d peak_attempt=%u\n",
        slot + 1, static_cast<unsigned long long>(state.connection), event.time,
        event.before, event.maximum, event.requested, state.peak);
    print("[reliable_trace] active=%d connected=%d spawned=%d full=%d usrmsgs=%d send=%d skip=%d rate=%.1f clear=%.6f received=%.6f out=%d ack=%d reliable=%d last_reliable=%d\n",
        int(client.active), int(client.connected), int(client.spawned), int(client.fully_connected),
        int(client.hasusrmsgs), int(client.send_message), int(client.skip_message), channel.rate,
        channel.cleartime, double(channel.last_received), channel.outgoing_sequence,
        channel.incoming_acknowledged, channel.reliable_length, channel.last_reliable_sequence);
    print("[reliable_trace] attempted_packets=%llu attempted_bytes=%llu window=%llu since=%.6f window_packets=%llu window_bytes=%llu unclassified_bytes=%llu recent=%u\n",
        static_cast<unsigned long long>(state.total.packets), static_cast<unsigned long long>(state.total.bytes),
        static_cast<unsigned long long>(state.window), state.windowStart,
        static_cast<unsigned long long>(state.current.packets), static_cast<unsigned long long>(state.current.bytes),
        static_cast<unsigned long long>(state.unclassifiedBytes), static_cast<unsigned>(state.count));
    if (state.crossed)
    {
        const auto& e = state.crossing;
        print("[reliable_trace] first48k time=%.6f window=%llu type=%u before=%u wire=%u prefix_le=%08x prefix_bytes=%u\n",
            e.time, static_cast<unsigned long long>(e.window), unsigned(e.type), e.before, e.wireBytes,
            e.prefix, unsigned(e.prefixBytes));
    }
    std::array<bool, TypeCount> shown{};
    // Eight heaviest message IDs in the observed accumulation window.
    for (unsigned rank = 0; rank < 8; ++rank)
    {
        std::size_t best = TypeCount;
        for (std::size_t i = 0; i < TypeCount; ++i)
            if (!shown[i] && state.types[i].windowId == state.window && state.types[i].window.bytes &&
                (best == TypeCount || state.types[i].window.bytes > state.types[best].window.bytes)) best = i;
        if (best == TypeCount) break;
        shown[best] = true;
        const auto& type = state.types[best];
        print("[reliable_trace] top type=%u packets=%llu bytes=%llu connection_packets=%llu connection_bytes=%llu\n",
            static_cast<unsigned>(best), static_cast<unsigned long long>(type.window.packets),
            static_cast<unsigned long long>(type.window.bytes), static_cast<unsigned long long>(type.total.packets),
            static_cast<unsigned long long>(type.total.bytes));
    }
    for (std::size_t i = 0; i < state.count; ++i)
    {
        const auto& e = state.recent[(state.next + RecentCount - state.count + i) % RecentCount];
        print("[reliable_trace] recent time=%.6f window=%llu type=%u dest=%u before=%u wire=%u prefix_le=%08x prefix_bytes=%u\n",
            e.time, static_cast<unsigned long long>(e.window), unsigned(e.type), unsigned(e.destination),
            e.before, e.wireBytes, e.prefix, unsigned(e.prefixBytes));
    }
}

template<class Buffer, class Client, class Print>
inline void Overflow(Buffer* buffer, int requested, Client* clients, int clientCount, double now, Print print)
{
    if (!buffer || !clients) return;
    const int count = clientCount < static_cast<int>(ClientCount) ? clientCount : static_cast<int>(ClientCount);
    for (int slot = 0; slot < count; ++slot)
    {
        auto& client = clients[slot];
        if (buffer != &client.netchan.message || client.fakeclient) continue;
        auto* state = Find(slot, buffer);
        if (!state || state->overflow.present) return;
        if (!state->observed)
            state->unclassifiedBytes = buffer->cursize > 0 ? static_cast<unsigned>(buffer->cursize) : 0;
        else if (buffer->cursize > 0 && static_cast<unsigned>(buffer->cursize) > state->expectedAfter)
            state->unclassifiedBytes = Add(state->unclassifiedBytes,
                static_cast<unsigned>(buffer->cursize) - state->expectedAfter);
        state->overflow = {now, buffer->cursize, buffer->maxsize, requested, true};
        Report(*state, slot, client, print);
        return;
    }
}
}
