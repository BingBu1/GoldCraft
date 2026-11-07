// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "scenario_trigger.h"
#include "scenario_entity_access.h"
#include <memory>
#include <utility>

enum class CSOLTriggerCollection { NotCollected, Ready, NeedsMapTriggerFile };

class CCSOLTriggerManager
{
public:
    // Like spawn markers, trigger markers are consumed once after classname
    // registration. Ordinary rounds must retain this geometry snapshot.
    void BeginMap();
    void BindEntityAccess(std::shared_ptr<const CSOLScenarioEntityAccess> access) { m_access = std::move(access); }
    CSOLTriggerCollection CollectMap();
    CSOLTriggerCollection Status() const { return m_status; }
    const csol::scenario::TriggerVolumes& Volumes() const { return m_volumes; }

private:
    csol::scenario::TriggerVolumes m_volumes;
    CSOLTriggerCollection m_status{CSOLTriggerCollection::NotCollected};
    std::shared_ptr<const CSOLScenarioEntityAccess> m_access{std::make_shared<CSOLScenarioEntityAccess>()};
};
