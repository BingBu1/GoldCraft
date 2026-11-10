// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace csol::client {

// Host adapter for one mouse button. The original normal Button captures on
// press and invokes its command only on an armed release. Timing and command
// validation belong to the menu, outside this engine/poll edge adapter.
class MenuButtonInput {
public:
    void Reset(bool held = false) {
        held_ = held;
        captured_ = hovered_ = -1;
        eventSincePoll_ = false;
    }

    void Move(int hit) { hovered_ = hit; }

    int Event(bool down, int hit) {
        eventSincePoll_ = true;
        return Observe(down, hit);
    }

    // Call once per frame. An engine event wins over the potentially stale
    // polled button state in that cycle. Polling resumes in the next cycle.
    int Poll(bool down, int hit) {
        Move(hit);
        if (eventSincePoll_) {
            eventSincePoll_ = false;
            return -1;
        }
        return Observe(down, hit);
    }

    bool Held() const { return held_; }
    int Captured() const { return captured_; }
    bool Armed(int id) const { return id >= 0 && hovered_ == id; }
    bool Selected(int id) const { return id >= 0 && held_ && captured_ == id; }
    bool Depressed(int id) const { return Armed(id) && Selected(id); }

private:
    int Observe(bool down, int hit) {
        Move(hit);
        if (down == held_) return -1;
        held_ = down;
        if (down) {
            captured_ = hit;
            return -1;
        }
        const int clicked = captured_ >= 0 && captured_ == hit ? hit : -1;
        captured_ = -1;
        return clicked;
    }

    bool held_{}, eventSincePoll_{};
    int captured_{-1}, hovered_{-1};
};

// Original type-1/2 TabButton background consumer. Six image groups are
// exported, but this consumer never chooses disabled_occ (group 5).
constexpr int TabVisualGroup(bool selected, bool armed, bool depressed) {
    return selected ? (depressed ? 2 : armed ? 1 : 0) : (armed ? 4 : 3);
}

} // namespace csol::client
