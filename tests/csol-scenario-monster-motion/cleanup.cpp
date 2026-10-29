// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace motion_test
{
namespace
{
bool Removal(const death_events_test::Packet& packet,int entity,int effect,bool builtIn)
{
    return packet.destination==MSG_ALL&&packet.message==96&&packet.slot==0&&packet.origin==nullptr&&packet.complete&&
        packet.bytes==std::vector<std::uint8_t>{0x61,static_cast<std::uint8_t>(entity),static_cast<std::uint8_t>(entity>>8),
        static_cast<std::uint8_t>(effect),static_cast<std::uint8_t>(effect>>8),static_cast<std::uint8_t>(builtIn)};
}
}
void CleanupCases()
{
    {
        Fixture f;auto& state=f.monster->SpeedState();const int index=Engine::Index(f.monster->edict());
        for(int id:{28,57,38,29,39,999})Require(state.AddLimit(id,10,80,.3f,0,10),"seed cleanup ID");
        state.boostUntil=40;state.recovery=.5f;state.AddEffect(2,80,0,10,0,Vector(3,4,0),10);f.monster->pev->gravity=.3f;
        std::size_t packetIndex{};
        f.engine.onMessage=[&](char phase)
        {
            if(phase!='b')return;
            Check(state.clearingLimits&&state.limits[packetIndex].until==-1,"invalidate current record before its removal packet");
            Check(!state.AddLimit(90,10,50,1,0,10)&&!CSOL_ClearMonsterSpeedLimits(*f.call,f.messages),"ring modification and nested clear rejected during message callback");
            Check(!f.messages.SendMonsterEffectRemoval(index,1,true),"nested packet writer rejected");
            ++packetIndex;
        };
        Require(CSOL_ClearMonsterSpeedLimits(*f.call,f.messages),"real ring clear and messages");
        Check(state.limits.empty()&&!state.clearingLimits&&state.effects.size()==1&&state.boostUntil==40&&state.recovery==.5f&&f.monster->pev->gravity==.3f,"clear removes only ring and retains independent effects, recovery, boost and gravity");
        Check(f.modelNames==std::vector<std::string>{"models/ef_m95tiger_nethit.mdl","models/ef_m95tiger_nethit.mdl","models/ef_m95tigerm_nethit.mdl"},"exact removal model mapping");
        Require(f.engine.packets.size()==5,"only recognized IDs emit removal");
        for(int i=0;i<3;++i)Check(Removal(f.engine.packets[i],index,501,false),"model-index removal packet bytes");
        Check(Removal(f.engine.packets[3],index,0x56,true)&&Removal(f.engine.packets[4],index,0x5b,true),"builtin removal packet bytes and flag");
    }
    {
        Fixture f;auto& state=f.monster->SpeedState();state.AddLimit(28,5,50,1,0,10);f.modelIndex=0;
        Check(CSOL_ClearMonsterSpeedLimits(*f.call,f.messages)&&state.limits.empty()&&f.engine.packets.empty(),"missing model index skips visual notification but still clears ring");
        state.AddLimit(29,5,50,1,0,10);CCSOLScenarioMessages unregistered;
        Check(!CSOL_ClearMonsterSpeedLimits(*f.call,unregistered)&&state.limits.size()==1&&state.limits.front().until==-1&&!state.clearingLimits,"missing message registration fails explicitly after record invalidation");
        Check(CSOL_ClearMonsterSpeedLimits(*f.call,f.messages)&&state.limits.empty(),"same valid lease can complete after missing service is restored");
    }
    {
        Fixture f;
        for(int value:{0,-1,65536})Check(!f.messages.SendMonsterEffectRemoval(value,1,false)&&!f.messages.SendMonsterEffectRemoval(1,value,false),"invalid short identities never emit packet");
        Check(f.messages.SendMonsterEffectRemoval(65535,65535,true)&&Removal(f.engine.packets.back(),65535,65535,true),"full unsigned16 wire range retains exact bits");
    }
    // Model-index, ENTINDEX and MESSAGE_BEGIN are all externally callable.
    // Every old owner must stop after the call; a packet already begun is
    // completed using copied scalars before the clear observes cancellation.
    for(int stage=0;stage<3;++stage)for(int kind=0;kind<3;++kind)
    {
        Fixture f;auto* original=f.monster;original->SpeedState().AddLimit(stage==0?28:29,5,50,1,0,10);
        original->SpeedState().AddLimit(39,5,40,1,0,10);bool fired{};
        const auto cancel=[&]
        {
            if(fired)return;fired=true;
            if(kind==0)f.requests.Reset();
            if(kind==1)
            {
                f.Reborrow(*original);
                auto& replacement=original->SpeedState();
                Check(!replacement.clearingLimits,"Acquire resets host clearing guard for the new lease");
                replacement.limits={{999,99,123,.9f}};
            }
            if(kind==2)f.DestroyMapStorage();
        };
        if(stage==0)f.onModelIndex=cancel;
        if(stage==1)f.engine.onQuery=[&](char code,int){if(code=='i')cancel();};
        if(stage==2)f.engine.onMessage=[&](char code){if(code=='b')cancel();};
        Check(!CSOL_ClearMonsterSpeedLimits(*f.call,f.messages)&&fired,"engine/message cancellation stops old clear");
        if(kind==1)Check(original->SpeedState().limits.size()==1&&original->SpeedState().limits.front().id==999&&original->SpeedState().limits.front().until==99&&!original->SpeedState().clearingLimits,"old callback never clears or rewrites replacement lease state");
        if(stage<2)Check(f.engine.packets.empty(),"cancel before MessageBegin sends nothing");
        else Check(f.engine.packets.size()==1&&f.engine.packets.front().complete&&!f.engine.packetOpen,"already-started removal completes one packet then stops");
    }
}
}
