// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace guest::cof::client
{
struct MouseHistory { std::int32_t x{}, y{}; };
struct MouseInput
{
    std::int32_t x{}, y{};
    float filter{}, hudSensitivity{}, sensitivity{};
    std::int32_t wholeFov{}, defaultFov{};
};
struct MouseOutput
{
    std::int32_t x{}, y{};
    MouseHistory history;
};
// client.dll d2a04641,1007B635..1007B792. The caller supplies the actual
// raw history and owns its lifecycle. This never invents a reset/seed policy.
MouseOutput ScaleMouse(const MouseHistory&, const MouseInput&) noexcept;
}
