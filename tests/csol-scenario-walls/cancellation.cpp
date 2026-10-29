// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace wall_test
{
void CancellationCases()
{
    for(const char* stage:{"player","credit"})for(int action=0;action<3;++action)
    {
        Engine e;auto& p=e.New<Player>("player");auto& w=e.Wall();auto rules=std::make_shared<Rules>();w.BindRules(rules);
        auto* wallEdict=w.edict();rules->callback=[&](std::string_view at){if(at!=stage)return;if(action==0)w.Restart();else if(action==1)rules->Invalidate();else e.Destroy(wallEdict);};
        const auto result=w.TakeDamage(p.pev,p.pev,100,DMG_BULLET);
        Check(!result&&e.messages.empty(),"callback invalidation aborts prior to base damage");
        if(action!=2)Check(w.pev->health==1000,"invalidated operation preserves reset/current health");
        else Check(wallEdict->free,"private free does not provoke cleanup dereference");
    }
    {
        Engine e;auto& p=e.New<Player>("player");auto& w=e.Wall();auto rules=std::make_shared<Rules>();w.BindRules(rules);
        rules->callback=[&](std::string_view stage){if(stage=="credit")Check(!w.TakeDamage(p.pev,p.pev,999,DMG_BULLET),"same wall recursive credit damage rejected");};
        Check(w.TakeDamage(p.pev,p.pev,100,DMG_BULLET)&&w.pev->health==900&&rules->credited.size()==1,"outer damage survives rejected recursion exactly once");
    }
    {
        Engine e;auto& p=e.New<Player>("player");auto& w=e.Wall();auto rules=std::make_shared<Rules>();w.BindRules(rules);
        rules->callback=[&](std::string_view stage){if(stage=="credit")e.Destroy(p.edict());};
        Check(!w.TakeDamage(p.pev,p.pev,100,DMG_BULLET)&&w.pev->health==1000,"removed attacker re-resolved before base dereference");
    }
    for(const char* boundary:{"sound","message-end"})
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");w.pev->target=Engine::String("end");auto* ed=w.edict();
        e.boundary=[&](std::string_view at,CBaseEntity*){if(at==boundary)e.Destroy(ed);};
        w.Use(nullptr,nullptr,USE_TOGGLE,0);Check(ed->free&&sink.calls==0,"Die cancels after sound/message callback raw-frees source");
    }
    {
        Engine e;auto& p=e.New<Player>("player");auto& w=e.Wall();auto rules=std::make_shared<Rules>();w.BindRules(rules);
        auto& master=e.New<Master>("test_master");master.enabled=true;master.pev->targetname=Engine::String("m");Engine::KV(w,"master","m");
        master.callback=[&](){w.Restart();};Check(!w.TakeDamage(p.pev,p.pev,100,DMG_BULLET)&&rules->calls.empty()&&w.pev->health==1000,"master-trigger reentry invalidates old operation");
    }
    {
        Engine e;auto& w=e.Wall("0","0");auto& monster=e.New<Actor>("test_monster");auto& sink=e.Target("end");w.pev->target=Engine::String("end");
        monster.callback=[&](){w.Restart();};w.Touch(&monster);Check(sink.calls==0&&w.pev->solid==SOLID_BSP,"virtual Classify callback cancellation keeps restored wall intact");
    }
    {
        Engine e;auto& w=e.Wall();e.boundary=[&](std::string_view at,CBaseEntity*){if(at=="set-model")w.BindRules(nullptr);};
        w.pev->team=4;w.Restart();Check(w.pev->team==4,"set-model rebind invalidates old Restart completion");
    }
}
}
