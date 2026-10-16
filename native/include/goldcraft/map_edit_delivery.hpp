#pragma once
#include <array>
#include <algorithm>

namespace goldcraft::edits {
constexpr unsigned reliable_room(int capacity, int queued, bool usable,
                                 bool fragmented) noexcept {
    // Never grow the engine's reliable fragment backlog. Keep at least 2 KiB
    // for other gameplay traffic and cap optional queued edits at 1536 bytes.
    if (!usable || fragmented || capacity < 2048 || queued < 0 || queued > capacity) return 0;
    const int room = std::min(1536, capacity - 2048) - queued;
    return room > 0 ? static_cast<unsigned>(room) : 0;
}
// Each native user message remains <= 192 bytes. A busy player can consume
// unused turns, with <= 8 fragments/player and <= 16 globally each frame.
// The persistent round-robin cursor prevents a busy first client starving the
// rest. At most one idle cycle is scanned after the last useful send.
template<class Send> int pump_fragments(int clients, int& cursor, Send send, int per_client = 8) {
    if (clients < 1 || clients > 64) return 0;
    std::array<unsigned char, 65> used{};
    int sent = 0, idle = 0;
    cursor = std::clamp(cursor, 0, clients);
    while (sent < 16 && idle < clients) {
        cursor = cursor % clients + 1;
        if (used[cursor] >= per_client || !send(cursor)) { ++idle; continue; }
        ++used[cursor]; ++sent; idle = 0;
    }
    return sent;
}
} // namespace goldcraft::edits
