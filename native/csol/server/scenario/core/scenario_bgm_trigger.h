// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "cbase.h"
#include <string>
#include <cstdint>

class CCSOLTriggerBGM final:public CBaseTrigger
{
public:
    void Spawn() override;
    void KeyValue(KeyValueData* data) override;
    void OnDestroy() override;
    void PlayUse(CBaseEntity*,CBaseEntity*,USE_TYPE,float);
private:
    std::string m_filename;
    bool m_loop{},m_playing{},m_prepared{};
    std::uint32_t m_musicRevision{};
};
