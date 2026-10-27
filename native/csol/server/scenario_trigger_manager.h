// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_trigger.h"

enum class CSOLTriggerCollection { NotCollected, Ready, NeedsMapTriggerFile };

class CCSOLTriggerManager
{
public:
    // Like spawn markers, trigger markers are consumed once after classname
    // registration. Ordinary rounds must retain this geometry snapshot.
    void BeginMap();
    CSOLTriggerCollection CollectMap();
    CSOLTriggerCollection Status() const { return m_status; }
    const csol::scenario::TriggerVolumes& Volumes() const { return m_volumes; }

private:
    csol::scenario::TriggerVolumes m_volumes;
    CSOLTriggerCollection m_status{CSOLTriggerCollection::NotCollected};
};
