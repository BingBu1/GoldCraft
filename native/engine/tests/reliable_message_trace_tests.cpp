// SPDX-License-Identifier: GPL-3.0-or-later
#include "../reliable_message_trace.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace trace = goldcraft::reliable_trace;
static unsigned checks{}, allocations{};
static bool countAllocations{};
void* operator new(std::size_t size)
{
    if (countAllocations) ++allocations;
    if (auto* result = std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)

// Only the adapter's consumed fields. The full SDK compile separately proves
// these member expressions instantiate against the real client_t/netchan_t.
struct Buffer { unsigned char* data{}; int cursize{}, maxsize{65536}; };
struct Channel
{
    Buffer message;
    double rate{100000}, cleartime{};
    float last_received{};
    int outgoing_sequence{}, incoming_acknowledged{}, reliable_length{}, last_reliable_sequence{};
};
struct Client
{
    Channel netchan;
    int active{1}, connected{1}, spawned{1}, fully_connected{1}, hasusrmsgs{1};
    int send_message{1}, skip_message{}, fakeclient{};
};
static std::array<Client, 32> clients;
static unsigned printCalls{};
static std::array<char, 32768> output;
static std::size_t outputSize{};
static bool reenter{};
static void Print(const char* format, ...)
{
    ++printCalls;
    va_list args;
    va_start(args, format);
    const auto n = std::vsnprintf(output.data() + outputSize, output.size() - outputSize, format, args);
    va_end(args);
    CHECK(n >= 0 && std::size_t(n) < output.size() - outputSize);
    outputSize += n;
    if (reenter)
    {
        reenter = false;
        trace::Overflow(&clients[0].netchan.message, 777, clients.data(), int(clients.size()), 999, Print);
    }
}
static void ClearOutput() { printCalls = 0; outputSize = 0; output = {}; }
static const unsigned char payload[]{27, 27, 7, 0, 12, 13};
static void Record(int slot, int before, int type = 66, int bytes = sizeof(payload), bool variable = true)
{
    auto& client = clients[slot];
    client.netchan.message.cursize = before;
    const auto copy = client;
    trace::Message(client, slot, &client.netchan.message, type, 1, payload, bytes, variable, double(before));
    CHECK(std::memcmp(&copy, &client, sizeof(client)) == 0);
}

int main()
{
    countAllocations = true;
    for (int slot = 0; slot < 32; ++slot) trace::Reset(slot, &clients[slot].netchan.message);
    CHECK(trace::states[0].connection == 1);
    CHECK(sizeof(trace::states) < 512 * 1024);
    // More than a complete ring while a queue grows: no print or allocation.
    for (int i = 0; i < 200; ++i) Record(0, i * 8);
    auto& state = trace::states[0];
    CHECK(printCalls == 0 && allocations == 0);
    CHECK(state.count == 64 && state.total.packets == 200 && state.total.bytes == 1600);
    CHECK(state.types[66].window.bytes == 1600 && state.unclassifiedBytes == 0);
    CHECK(state.recent[state.next].before == 136 * 8);
    CHECK(state.recent[(state.next + 63) % 64].before == 199 * 8);
    CHECK(state.recent[state.next].prefix == 0x00071b1b && state.recent[state.next].prefixBytes == 4);

    // A queue drain starts a new observed window, retaining connection totals.
    Record(0, 0, 67, 1, false);
    CHECK(state.window == 2 && state.current.packets == 1 && state.current.bytes == 2);
    CHECK(state.total.packets == 201 && state.total.bytes == 1602);
    CHECK(state.types[67].windowId == 2 && state.types[67].window.bytes == 2);
    CHECK(state.types[66].windowId == 1);
    // Bytes added by engine writers outside PF_MessageEnd are identified.
    Record(0, 100, 67, 0, true);
    CHECK(state.unclassifiedBytes == 98 && state.current.bytes == 4);

    // Keep the first 48KiB crossing, without logging a legitimate large queue.
    Record(0, 49148, 66);
    CHECK(state.crossed && state.crossing.before == 49148 && state.crossing.wireBytes == 8);
    CHECK(!state.reported && printCalls == 0);
    Record(0, 65000, 87);
    CHECK(state.crossing.before == 49148);
    clients[0].netchan.message.cursize = 65533;
    const auto beforeOverflow = clients[0];
    reenter = true;
    trace::Overflow(&clients[0].netchan.message, 6, clients.data(), 32, 250, Print);
    CHECK(std::memcmp(&beforeOverflow, &clients[0], sizeof(Client)) == 0);
    CHECK(state.reported && state.overflow.present && state.overflow.before == 65533);
    CHECK(state.overflow.requested == 6 && state.overflow.time == 250);
    CHECK(printCalls > 64 && printCalls <= 76 && allocations == 0);
    CHECK(std::strstr(output.data(), "queue=65533 capacity=65536 request=6"));
    CHECK(std::strstr(output.data(), "first48k") && std::strstr(output.data(), "prefix_le=00071b1b"));
    CHECK(std::strstr(output.data(), "top type=66") && std::strstr(output.data(), "top type=87"));
    const auto printed = printCalls;
    trace::Overflow(&clients[0].netchan.message, 100, clients.data(), 32, 251, Print);
    CHECK(printCalls == printed && state.overflow.requested == 6);

    // Reusing the same channel address is a new connection, with no old report,
    // old bytes, old crossing, or stale message IDs.
    trace::Reset(0, &clients[0].netchan.message);
    CHECK(state.connection == 2 && state.total.packets == 0 && !state.reported && !state.crossed);
    CHECK(state.count == 0 && state.types[66].total.packets == 0 && !state.overflow.present);
    ClearOutput();
    Record(0, 65530, 69, 1, false);
    clients[0].netchan.message.cursize = 65535;
    trace::Overflow(&clients[0].netchan.message, 2, clients.data(), 32, 300, Print);
    CHECK(state.reported && std::strstr(output.data(), "generation=2"));
    CHECK(!std::strstr(output.data(), "top type=66"));
    CHECK(trace::states[1].total.packets == 0 && !trace::states[1].reported);

    // Other buffers, invalid slots, fake clients and identity mismatches cannot
    // contaminate state or emit an overflow report for a real client.
    ClearOutput();
    Buffer other;
    trace::Overflow(&other, 2, clients.data(), 32, 301, Print);
    trace::Message(clients[1], 1, &other, 66, 1, payload, 6, true, 1);
    trace::Record(-1, &other, 0, 10, 66, 1, payload, 6, true, 1);
    trace::Record(32, &other, 0, 10, 66, 1, payload, 6, true, 1);
    trace::Record(1, &other, 0, 10, 66, 1, payload, 6, true, 1);
    clients[1].fakeclient = 1;
    Record(1, 12);
    trace::Overflow(&clients[1].netchan.message, 65536, clients.data(), 32, 301, Print);
    clients[1].fakeclient = 0;
    CHECK(printCalls == 0 && trace::states[1].total.packets == 0);
    // A direct engine writer can overflow before any DLL message was seen.
    clients[1].netchan.message.cursize = 65534;
    trace::Overflow(&clients[1].netchan.message, 8, clients.data(), 32, 302, Print);
    CHECK(trace::states[1].reported && trace::states[1].count == 0);
    CHECK(trace::states[1].unclassifiedBytes == 65534);
    CHECK(std::strstr(output.data(), "slot=2") && std::strstr(output.data(), "attempted_packets=0"));

    trace::Reset(31, &clients[31].netchan.message);
    Record(31, 0, 255);
    CHECK(trace::states[31].types[255].total.packets == 1);
    const auto total = trace::states[31].total.packets;
    trace::Record(31, &clients[31].netchan.message, 0, 65536, 256, 1, payload, 1, true, 0);
    trace::Record(31, &clients[31].netchan.message, 0, 65536, 66, 1, nullptr, 1, true, 0);
    trace::Record(31, &clients[31].netchan.message, -1, 65536, 66, 1, payload, 1, true, 0);
    CHECK(trace::states[31].total.packets == total);
    CHECK(trace::Add(std::numeric_limits<std::uint64_t>::max(), 2) == std::numeric_limits<std::uint64_t>::max());
    CHECK(allocations == 0);
    countAllocations = false;
    std::printf("reliable trace: %u checks passed, no observed allocations\n", checks);
}
