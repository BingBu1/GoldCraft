// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/client/menu_input.hpp"
#include <cstdlib>
#include <iostream>

namespace {
unsigned checks{};
void Check(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
void CaptureAndRelease() {
    csol::client::MenuButtonInput input;
    Check(input.Event(true, 3) == -1, "press dispatched a command");
    Check(input.Held() && input.Captured() == 3 && input.Depressed(3), "press capture missing");
    input.Move(4);
    Check(input.Selected(3) && !input.Depressed(3) && input.Armed(4), "drag changed capture");
    Check(input.Event(false, 4) == -1, "release over different control clicked");
    Check(!input.Held() && input.Captured() == -1, "release retained capture");
    input.Event(true, 3); input.Move(-1); input.Move(3);
    Check(input.Depressed(3) && input.Event(false, 3) == 3, "drag back did not click");
    input.Event(true, -1);
    Check(input.Event(false, 3) == -1, "outside press armed a control");
}
void MixedDelivery() {
    csol::client::MenuButtonInput input;
    input.Poll(true, 2);
    Check(input.Event(true, 2) == -1, "poll then event repeated press");
    Check(input.Event(false, 2) == 2, "engine release lost poll capture");
    Check(input.Poll(true, 2) == -1 && !input.Held(), "stale poll replayed engine press");
    input.Event(true, 2);
    Check(input.Poll(false, 2) == -1 && input.Held(), "same cycle poll released capture");
    Check(input.Poll(false, 2) == 2, "fallback did not recover missing engine release");
    input.Poll(true, 2);
    Check(input.Poll(false, 2) == 2, "poll fallback permanently disabled");
    input.Event(true, 2);
    Check(input.Event(false, 2) == 2, "first same-frame click missing");
    input.Event(true, 2);
    Check(input.Event(false, 2) == 2, "second same-frame click missing");
    Check(input.Poll(false, 2) == -1, "poll duplicated same-frame click");
}
void RepeatsAndReset() {
    csol::client::MenuButtonInput input;
    for (int i = 0; i < 64; ++i) {
        Check(input.Event(true, i) == -1, "down fired command");
        Check(input.Event(true, i) == -1, "duplicate down fired command");
        Check(input.Event(false, i) == i, "release lost command");
        Check(input.Event(false, i) == -1, "duplicate release fired command");
    }
    input.Event(true, 3); input.Reset();
    Check(input.Event(false, 3) == -1, "hide/focus reset retained capture");
    input.Reset(true);
    Check(input.Event(true, 3) == -1 && !input.Selected(3), "opening held click became selected");
    Check(input.Event(false, 3) == -1, "opening held release selected a role");
    input.Event(true, 3);
    Check(input.Event(false, 3) == 3, "fresh click after reset lost");
    csol::client::MenuButtonInput right;
    input.Event(true, 1); right.Event(true, 2);
    Check(input.Event(false, 1) == 1 && right.Held(), "left release changed right capture");
    Check(right.Event(false, 2) == 2, "independent button release lost");
}
void TabStates() {
    // Literal truth table from original 2CA7520 branches, including states
    // that normally cannot occur (depressed without selected).
    constexpr int expected[]{3,3,4,4,0,2,1,2};
    for (int bits = 0; bits < 8; ++bits)
        Check(csol::client::TabVisualGroup(bits & 4, bits & 2, bits & 1) == expected[bits],
              "tab state selected wrong image group");
}
}
int main() {
    CaptureAndRelease(); MixedDelivery(); RepeatsAndReset(); TabStates();
    std::cout << checks << " input checks passed\n";
}
