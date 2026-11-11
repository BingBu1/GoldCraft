// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "vector.h"
#include <cmath>

namespace guest::cof::detail
{
// Authoritative thread only. This coordinates the CoF senders; it is not an
// engine-wide lock and cannot make an unrelated nested MESSAGE_BEGIN safe.
inline bool messageOpen{};
struct MessagePacket
{
    MessagePacket() noexcept { messageOpen = true; }
    ~MessagePacket() { messageOpen = false; }
    MessagePacket(const MessagePacket&) = delete;
    MessagePacket& operator=(const MessagePacket&) = delete;
};

inline bool NativeCoordinates(const Vector& value) noexcept
{
    for (unsigned i = 0; i < 3; ++i)
    {
        // PF_WriteCoord_I converts double(value)*8 to int32 and writes its low
        // word. Preserve native wrapping; reject only undefined conversions.
        const double scaled = static_cast<double>(value[i]) * 8.0;
        if (!std::isfinite(scaled) || scaled < -2147483648.0 || scaled >= 2147483648.0)
            return false;
    }
    return true;
}
}
