// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "scenario_bgm_trigger.h"
#include "scenario_music.h"
#include "game_rules_gate.h"
#include <cstdlib>

LINK_ENTITY_TO_CLASS(trigger_bgm,CCSOLTriggerBGM,CCSTrigger)

void CCSOLTriggerBGM::KeyValue(KeyValueData* data)
{
    if(FStrEq(data->szKeyName,"loop")){m_loop=std::atoi(data->szValue)!=0;data->fHandled=TRUE;return;}
    if(FStrEq(data->szKeyName,"filename")){m_filename=data->szValue;data->fHandled=TRUE;return;}
    CBaseTrigger::KeyValue(data);
}
void CCSOLTriggerBGM::Spawn()
{
    CSOLScenarioSdkScope sdk;
    m_prepared=false;m_playing=false;m_musicRevision=0;
    if(!sdk.Scenario()||!sdk.Current())return;
    const EHANDLE self(this);
    InitTrigger();
    if(!sdk.Current()||self.Get<CBaseEntity>()!=this)return;
    // Original11526C40 uses InitTrigger and spawnflag2 to disable solidity.
    // It installs PlayUse, without inventing a touch-to-play callback.
    if(pev->spawnflags&2)pev->solid=SOLID_NOT;
    SetUse(&CCSOLTriggerBGM::PlayUse);
    // The original reports an empty filename only when used. Such an inert
    // marker is not a reason to reject otherwise valid map resources.
    if(m_filename.empty())return;
    const auto music=CSOL_ScenarioMusic(sdk.EntityAccess().get());
    csol::scenario::ResourceError error;
    const bool prepared=music&&music->PrecacheFile(m_filename,error);
    if(!sdk.Current()||self.Get<CBaseEntity>()!=this)return;
    m_prepared=prepared;
    if(!m_prepared&&!error.detail.message.empty())
        CSOL_RejectGameRules(error.detail.message.c_str());
}
void CCSOLTriggerBGM::PlayUse(CBaseEntity*,CBaseEntity*,USE_TYPE,float)
{
    CSOLScenarioSdkScope sdk;
    if(!m_prepared||!sdk.Scenario()||!sdk.Current())return;
    const auto music=CSOL_ScenarioMusic(sdk.EntityAccess().get());
    if(!music)return;
    const EHANDLE self(this);
    // Original11523C00 toggles from its own active byte, independent of the
    // incoming USE_TYPE. A second Use fades; first Use stops the other BGMs.
    if(m_playing){m_playing=false;music->Fade();return;}
    CBaseEntity* other{};
    while((other=UTIL_FindEntityByClassname(other,"trigger_bgm")))
    {
        if(!sdk.Current()||self.Get<CBaseEntity>()!=this)return;
        auto* trigger=dynamic_cast<CCSOLTriggerBGM*>(other);
        if(trigger&&trigger!=this)trigger->m_playing=false;
    }
    if(!sdk.Current()||self.Get<CBaseEntity>()!=this)return;
    // The host MP3 play command replaces its previous stream. Clearing all
    // active flags plus one play avoids one redundant fade per inactive entity.
    auto expectedRevision=music->Revision()+1;if(!expectedRevision)++expectedRevision;
    m_playing=true;
    const bool played=music->Play(m_filename,m_loop);
    if(!sdk.Current()||self.Get<CBaseEntity>()!=this)return;
    m_playing=played&&music->Revision()==expectedRevision;
    m_musicRevision=m_playing?expectedRevision:0;
}
void CCSOLTriggerBGM::OnDestroy()
{
    CSOLScenarioSdkScope sdk;
    if(m_playing&&sdk.Scenario()&&sdk.Current())
    {
        m_playing=false;
        const auto music=CSOL_ScenarioMusic(sdk.EntityAccess().get());
        // Entity retirement must not fade a newer stage or another trigger.
        if(music&&music->Revision()==m_musicRevision)music->Fade();
    }
    CBaseTrigger::OnDestroy();
}
