// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace csol {

// GoldSrc R_StudioClientEvents (stock hw 101F128D..101F12B5; CSO hw
// 378DF066..378DF092) resets the event window start to -0.01 while a
// NON-looping sequence still has latched.sequencetime == curstate.animtime.
// The engine latches sequencetime from animtime on each sequence change
// (stock hw 101F6146/101FA0BE), so until the server advances animtime every
// rendered frame dispatches all events already passed again. A monster that
// stops thinking after death (deadline FreeThink) stays in this state and
// would replay frame-1 sounds every frame. Network updates can relatch both
// timestamps even while the same sequence advances. Observe the network pose
// on every event draw, including draws with no event, to distinguish forward
// progress from a real restart. Viewmodel animations instead start at animtime.
class StudioEventRepeatGuard
{
public:
    void Observe(const void* entity, const void* model, int sequence, bool looping,
        float frame, float animTime, float rate, int message, bool viewmodel)
    {
        if (!entity) return;
        if (!model || looping || !std::isfinite(frame) || !std::isfinite(animTime) ||
            !std::isfinite(rate) || rate < 0)
        {
            m_states.erase(entity);
            return;
        }
        auto& state = m_states[entity];
        const bool identityChanged = state.model != model || state.sequence != sequence || state.viewmodel != viewmodel;
        const bool laterPose = animTime > state.animTime || (animTime == state.animTime && message > state.message);
        const bool restarted = viewmodel ? state.animTime != animTime : laterPose && frame < state.frame;
        if (identityChanged || restarted) state.fired.clear();
        // An older network pose cannot establish a new forward-play instance.
        if (!identityChanged && !viewmodel && animTime < state.animTime) return;
        state.model = model;
        state.sequence = sequence;
        state.frame = frame;
        state.animTime = animTime;
        state.message = message;
        state.viewmodel = viewmodel;
    }

    // True when this exact event record already fired in the same instance.
    bool Repeated(const void* entity, const void* model, int sequence, bool looping,
        float sequenceTime, float animTime, const void* event)
    {
        if (looping || !entity || !event) return false;
        const auto found = m_states.find(entity);
        if (found == m_states.end()) return false;
        auto& state = found->second;
        if (state.model != model || state.sequence != sequence || state.animTime != animTime) return false;
        if (std::find(state.fired.begin(), state.fired.end(), event) != state.fired.end())
        {
            if (sequenceTime == animTime)
            {
                ++m_suppressed;
                return true;
            }
            return false;
        }
        state.fired.push_back(event);
        return false;
    }
    // Entity and model pointers are reused across maps and sessions.
    void Clear() { m_states.clear(); m_suppressed = 0; }
    unsigned long long Suppressed() const { return m_suppressed; }

private:
    struct State
    {
        const void* model{};
        int sequence{-1};
        float frame{}, animTime{};
        int message{};
        bool viewmodel{};
        std::vector<const void*> fired;
    };
    std::unordered_map<const void*, State> m_states;
    unsigned long long m_suppressed{};
};

} // namespace csol
