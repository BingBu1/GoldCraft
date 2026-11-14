// SPDX-License-Identifier: GPL-3.0-or-later
#include "automatic_sound.hpp"

namespace guest::cof
{
namespace
{
AutomaticSoundResult Check(const AutomaticSoundFrame& frame, const AutomaticSoundHost& host) noexcept
{
    if (frame.clip < 0 || frame.action < 0) return AutomaticSoundResult::InvalidInput;
    if (!frame.nativeMultiplayer) return AutomaticSoundResult::UnsupportedRules;
    if (!host.current || !host.event) return AutomaticSoundResult::MissingHost;
    return host.current(host.context) ? AutomaticSoundResult::Unchanged : AutomaticSoundResult::Cancelled;
}
bool Emit(const AutomaticSoundHost& host, const AutomaticSoundEvent& event) noexcept
{
    return host.current(host.context) && host.event(host.context, event) && host.current(host.context);
}
AutomaticSoundEvent Play(AutomaticSoundSample sample, bool quiet) noexcept
{
    return {sample, 1, 0, 100, quiet ? .25F : 1.F, .8F};
}
}

AutomaticSoundResult StartAutomaticNativeSound(AutomaticSoundState& state, const AutomaticSoundFrame& frame,
    const AutomaticSoundHost& host) noexcept
{
    const auto ready = Check(frame, host);
    if (ready != AutomaticSoundResult::Unchanged) return ready;
    if (state.playing || !frame.hasFire) return AutomaticSoundResult::Unchanged;
    state.playing = true;
    return Emit(host, Play(AutomaticSoundSample::Fire, frame.quietAudio)) ?
        AutomaticSoundResult::Started : AutomaticSoundResult::Cancelled;
}

AutomaticSoundResult AdvanceAutomaticNativeSound(AutomaticSoundState& state, const AutomaticSoundFrame& frame,
    const AutomaticSoundHost& host) noexcept
{
    const auto ready = Check(frame, host);
    if (ready != AutomaticSoundResult::Unchanged) return ready;
    if (!state.playing || (frame.ownerAlive && frame.clip > 0 && !frame.loading && frame.attackHeld && !frame.action))
        return AutomaticSoundResult::Unchanged;
    state.playing = false;
    if (frame.hasFire && !Emit(host, {AutomaticSoundSample::Fire, 1, 32, 100, 0, 0}))
        return AutomaticSoundResult::Cancelled;
    if (frame.hasTail && !Emit(host, Play(AutomaticSoundSample::Tail, frame.quietAudio)))
        return AutomaticSoundResult::Cancelled;
    return AutomaticSoundResult::Stopped;
}
}
