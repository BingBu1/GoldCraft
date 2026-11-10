// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace goldcraft::csol_resources {
inline constexpr char interface_name[] = "GoldCraftCsolResource002";
inline constexpr std::uint32_t api_version = 2;
// Version 2 requires shared HUD-directory augmentation; version 1 physically
// wrapped the native allocator and is unsafe with the common directory owner.
// Host adaptation for the verified SHA resource gate, not a CSOL wire format.
// All calls run on the client's main thread, outside Paint/model drawing.
// token is a nonzero, monotonically increasing local connection identity.
// BeforeSwitch precedes overlay changes. AfterSwitch follows final VFS SHA
// verification AND engine cache retirement, before original precaching resumes.
// Failure keeps consumers suspended. Cancel is required on disconnect/failure;
// it only affects the matching token and never unblocks a newer connection.
struct Api {
    std::uint32_t size;
    std::uint32_t version;
    bool (__cdecl* available)();
    int (__cdecl* beforeSwitch)(std::uint64_t token, char* error, std::uint32_t capacity);
    int (__cdecl* afterSwitch)(std::uint64_t token, char* error, std::uint32_t capacity);
    int (__cdecl* cancelSwitch)(std::uint64_t token, char* error, std::uint32_t capacity);
    // Alternate completion on disconnect/ordinary-server transition. Caller
    // restored base VFS and retired engine caches first; CSOL files may be absent.
    int (__cdecl* resetToBase)(std::uint64_t token, char* error, std::uint32_t capacity);
};
}
