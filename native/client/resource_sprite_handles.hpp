// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct model_s;

namespace goldcraft::resource_sprite_handles {
// HL10210 SPR_Load 1019CF80 owns this 12-byte table. HSPRITE is index + 1.
struct Entry { model_s* model{}; char* name{}; int frames{}; };
struct Binding {
    Entry** entries{};
    int* capacity{};
    int (*load)(const char*){};
};
struct Handle { int value{}; std::string name; };

// Called synchronously before engine model retirement, while readers are
// paused. Strings are copied; no hunk headers or table entries escape.
const char* Capture(const Binding&, std::vector<Handle>&);
// The table may have moved while loading; reread its current pointer for every
// result. A failure is fatal to admission, never permission to resume drawing.
const char* Reload(const Binding&, const std::vector<Handle>&);
}
