// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace wall_test
{
void TargetCases()
{
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");
        auto& relay=e.Relay("relay","end");w.pev->target=Engine::String("relay");
        auto& standing=e.New<CBaseEntity>("standing");standing.pev->flags|=FL_ONGROUND;
        standing.pev->groundentity=w.edict();standing.pev->absmin=Vector(0,0,64);standing.pev->absmax=Vector(1,1,66);
        auto& distant=e.New<CBaseEntity>("distant");distant.pev->flags|=FL_ONGROUND;
        distant.pev->groundentity=w.edict();distant.pev->absmin=Vector(100,100,100);distant.pev->absmax=Vector(102,102,102);
        w.Use(nullptr,nullptr,USE_OFF,0);
        Check(sink.calls==1&&sink.lastType==USE_TOGGLE&&sink.lastActivator.Get<CBaseEntity>()==&relay,"real nested relay preserves original Use contract");
        Check(!(standing.pev->flags&FL_ONGROUND)&&!standing.pev->groundentity&&(distant.pev->flags&FL_ONGROUND),"actual SDK contiguous-edict AABB traversal clears only standing overlap");
        Require(!e.messages.empty(),"real Die sends shard packet");
        const auto& packet=e.messages.front();Check(packet.type==SVC_TEMPENTITY&&packet.integers==std::vector<int>({TE_BREAKMODEL,10,7,0,25,BREAK_WOOD})&&packet.coordinates.size()==9,"actual material shard message retained");
    }
    {
        Engine e;auto& w=e.Wall();auto& killed=e.Target("remove_me");auto& sink=e.Target("end");
        w.m_iszKillTarget=Engine::String("remove_me");w.pev->target=Engine::String("end");
        sink.callback=[&](){Check((killed.pev->flags&FL_KILLME)&&!killed.pev->targetname,"actual killtarget processed before target callback");};
        w.Use(nullptr,nullptr,USE_TOGGLE,0);Check(sink.calls==1&&!killed.edict()->free,"UTIL_Remove retains deferred private storage");
    }
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");auto& relay=e.Relay("relay","end");
        relay.m_iszKillTarget=Engine::String("relay");w.pev->target=Engine::String("relay");
        w.Use(nullptr,nullptr,USE_TOGGLE,0);
        Check((relay.pev->flags&FL_KILLME)&&sink.calls==0,"self killtarget cancels remaining nested relay target");
    }
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");w.pev->targetname=Engine::String("self");
        w.m_iszKillTarget=Engine::String("self");w.pev->target=Engine::String("end");w.pev->nextthink=42;
        w.Use(nullptr,nullptr,USE_TOGGLE,0);
        Check((w.pev->flags&FL_KILLME)&&sink.calls==0&&w.pev->nextthink==42,"self-deleted wall stops target and post-Die writes");
    }
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");auto& first=e.Target("chain");auto& second=e.Target("chain");
        auto& relay=e.Relay("relay","chain");w.pev->target=Engine::String("relay");
        first.callback=[&](){w.Restart();};second.callback=[&](){sink.Use(nullptr,nullptr,USE_TOGGLE,0);};
        w.Use(nullptr,nullptr,USE_TOGGLE,0);
        Check(first.calls==1&&second.calls==0&&sink.calls==0&&w.pev->solid==SOLID_BSP&&w.pev->health==1000,"Restart completes inside chain and cancels old ancestry across relay");
    }
    {
        Engine e;auto& w=e.Wall();auto& overlay=e.New<CBaseEntity>("func_illusionary");
        overlay.pev->targetname=Engine::String("box1_render");overlay.pev->rendermode=2;overlay.pev->renderamt=50;
        overlay.pev->renderfx=3;overlay.pev->rendercolor=Vector(12,34,56);
        auto& render=e.New<CRenderFxManager>("env_render");render.pev->targetname=Engine::String("box1");
        render.pev->target=Engine::String("box1_render");render.pev->rendermode=2;render.pev->renderamt=0;render.Spawn();
        w.pev->target=Engine::String("box1");w.Use(nullptr,nullptr,USE_TOGGLE,0);
        Check(overlay.pev->renderamt==0&&w.pev->solid==SOLID_NOT,"real env_render hides actual target entvars with wall");
        w.Restart();render.Restart();
        Check(overlay.pev->renderamt==50&&overlay.pev->renderfx==3&&overlay.pev->rendercolor==Vector(12,34,56)&&w.pev->solid==SOLID_BSP,"real render backup and wall Restart restore original state");
    }
}
void DelayCases()
{
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");w.pev->target=Engine::String("end");w.m_flDelay=.5f;
        w.Use(nullptr,nullptr,USE_TOGGLE,0);auto delays=e.Delays();Require(delays.size()==1,"actual delayed entity created");
        Check(sink.calls==0&&delays[0]->v.nextthink==10.5f&&(CBaseEntity::Instance(delays[0])->ObjectCaps()&FCAP_DONT_SAVE),"guarded delayed target deadline and nonsaved cap");
        e.Think(delays[0]);Check(sink.calls==1&&delays[0]->free&&e.Delays().empty(),"actual delayed use fires once and cleans itself");
    }
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");auto& relay=e.Relay("relay","end",.25f,true);
        w.pev->target=Engine::String("relay");w.m_flDelay=.5f;w.Use(nullptr,nullptr,USE_TOGGLE,0);
        auto first=e.Delays();Require(first.size()==1,"first link delay exists");e.Think(first[0]);
        auto second=e.Delays();Require(second.size()==1&&second[0]!=first[0],"second delay survives first transient entity cleanup");
        Check(first[0]->free&&(relay.pev->flags&FL_KILLME)&&sink.calls==0,"fireonce relay is transient ancestry only");
        e.Think(second[0]);Check(sink.calls==1&&second[0]->free,"chained delay retains wall lifetime but not removed parent delay or relay");
    }
    for(int cancel=0;cancel<4;++cancel)
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");auto rules=std::make_shared<Rules>();w.BindRules(rules);
        w.pev->target=Engine::String("end");w.m_flDelay=1;w.Use(nullptr,nullptr,USE_TOGGLE,0);
        auto delays=e.Delays();Require(delays.size()==1,"cancellation delay setup");
        if(cancel==0)w.Restart();else if(cancel==1)rules->Invalidate();else if(cancel==2)w.BindRules(nullptr);else e.Destroy(w.edict());
        e.Think(delays[0]);Check(sink.calls==0&&delays[0]->free,"Restart/rule generation/unbind/private free cancel delayed target but still clean timer");
    }
    {
        Engine e;auto& w=e.Wall();auto& first=e.Target("end");auto& second=e.Target("end");w.pev->target=Engine::String("end");w.m_flDelay=1;
        first.callback=[&](){auto* caller=first.lastCaller.Get<CBaseEntity>();Require(caller!=nullptr,"delay is current caller");e.Destroy(caller->edict());};
        w.Use(nullptr,nullptr,USE_TOGGLE,0);auto delays=e.Delays();e.Think(delays[0]);
        Check(first.calls==1&&second.calls==0&&delays[0]->free,"target raw-frees active delayed entity without second target or double cleanup");
    }
    {
        Engine e;auto& w=e.Wall();auto& sink=e.Target("end");w.pev->target=Engine::String("end");w.m_flDelay=1;
        e.boundary=[&](std::string_view stage,CBaseEntity* entity){if(stage=="make-string-class"&&entity){e.Destroy(entity->edict());}};
        w.Use(nullptr,nullptr,USE_TOGGLE,0);
        Check(sink.calls==0&&e.Delays().empty()&&e.removes==1,"new delay deleted during classname registration is not initialized or removed twice");
    }
    {
        Engine e;auto& relay=e.Relay("ordinary","end",.5f);auto& sink=e.Target("end");
        relay.Use(nullptr,nullptr,USE_TOGGLE,0);auto delays=e.Delays();Require(delays.size()==1,"ordinary SDK delay created outside wall scope");
        Check(!(CBaseEntity::Instance(delays[0])->ObjectCaps()&FCAP_DONT_SAVE),"ordinary CBaseDelay retains original save capability");
        e.Think(delays[0]);Check(sink.calls==1&&delays[0]->free,"ordinary nonwall DelayThink remains functional");
    }
}
}
