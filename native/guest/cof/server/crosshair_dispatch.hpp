// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../crosshair_protocol.hpp"
#include "sound_dispatch.hpp"
class CBasePlayer;
namespace guest::cof
{
enum class CrosshairSend { Sent, Invalid, MissingBackend, Busy, Cancelled };
// The caller invokes this from an actual outgoing snapshot, not PostThink.
CrosshairSend SendCrosshairSnapshot(CBasePlayer&,const CrosshairSnapshot&,int message,
                                   const PlaySfxCall&) noexcept;
}
