// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../scenario/wave_events.hpp"

class CCSOLMonsterPool;
class CCSOLScenarioMessages;

// Construct for one wave update. A failed send remains visible to the owner;
// DispatchWaveEvent::Sent alone only means its void callback was invoked.
class CCSOLScenarioEventSink final : public csol::scenario::WaveEventActions
{
public:
    CCSOLScenarioEventSink(const CCSOLMonsterPool& monsters, const CCSOLScenarioMessages& messages)
        : m_monsters(monsters), m_messages(messages) {}
    bool HasSolidMonster(int type) override;
    void BroadcastEvent(std::span<const std::uint8_t, 4> payload) override;
    bool Valid() const { return m_valid; }
private:
    const CCSOLMonsterPool& m_monsters;
    const CCSOLScenarioMessages& m_messages;
    bool m_valid{true};
};
