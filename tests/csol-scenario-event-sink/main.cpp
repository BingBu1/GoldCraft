// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK pool -> event dispatcher -> message encoder; engine services and
// network callbacks are controlled. This is not live network/client evidence.
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../../native/csol/server/scenario_event_sink.h"
#include "../../native/csol/server/scenario_messages.h"
#include <iostream>

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* what)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << what << '\n'; } }
struct Call
{
    char kind;
    int value, id;
    const float* origin;
    edict_t* recipient;
};
struct Network
{
    inline static Network* current{};
    std::vector<Call> calls;
    int registrations{}, returnedId{85};
    std::string registeredName;
    int registeredSize{};
    Network()
    {
        current=this;
        g_engfuncs.pfnRegUserMsg=Register;
        g_engfuncs.pfnMessageBegin=Begin;
        g_engfuncs.pfnWriteByte=Byte;
        g_engfuncs.pfnWriteShort=Short;
        g_engfuncs.pfnMessageEnd=End;
    }
    ~Network(){current=nullptr;}
    static int Register(const char* name,int size)
    { ++current->registrations;current->registeredName=name;current->registeredSize=size;return current->returnedId; }
    static void Begin(int destination,int id,const float* origin,edict_t* recipient)
    {current->calls.push_back({'B',destination,id,origin,recipient});}
    static void Byte(int value){current->calls.push_back({'b',value,0,nullptr,nullptr});}
    static void Short(int value){current->calls.push_back({'s',value,0,nullptr,nullptr});}
    static void End(){current->calls.push_back({'E',0,0,nullptr,nullptr});}
    void Sent(int id)
    {
        Check(calls.size()==5,"real message sender emits exactly five engine operations");
        if(calls.size()!=5)return;
        Check(calls[0].kind=='B'&&calls[0].value==MSG_ALL&&calls[0].id==returnedId&&!calls[0].origin&&!calls[0].recipient,"broadcast uses registered user message and no recipient");
        Check(calls[1].kind=='b'&&calls[1].value==27&&calls[2].kind=='b'&&calls[2].value==7,"message envelope preserved by pool sink");
        Check(calls[3].kind=='s'&&calls[3].value==id&&calls[4].kind=='E',"message ID and end preserved");
        calls.clear();
    }
};
WaveRow Event(int type,int id=193)
{WaveRow row;row.spawnPoint=9999;row.types={type};row.weights={1};row.eventMessage=std::to_string(id);return row;}
void Lifecycle()
{
    PoolFixture fixture; Network network; CCSOLScenarioMessages messages;ConfigError error;
    Check(messages.Register()&&network.registrations==1&&network.registeredName=="MPToCL"&&network.registeredSize==-1,"actual message registration used");
    CCSOLScenarioEventSink sink(fixture.pool,messages);
    Check(DispatchWaveEvent(Event(1),sink,error)==WaveEventResult::Suppressed&&sink.Valid()&&network.calls.empty(),"empty parked pool suppresses ordinary type without invalidating sink");
    auto* monster=fixture.Acquire(0);
    Check(monster&&monster->Pool()==&fixture.pool,"event source is an actual borrowed registered entity");
    if(!monster)return;
    Check(DispatchWaveEvent(Event(1),sink,error)==WaveEventResult::Sent&&sink.Valid(),"matching real solid type passes event gate");network.Sent(193);
    Check(DispatchWaveEvent(Event(30200),sink,error)==WaveEventResult::Suppressed&&network.calls.empty(),"other type does not use active count as membership");
    monster->pev->health=0;monster->pev->deadflag=DEAD_DEAD;monster->pev->team=2;
    Check(DispatchWaveEvent(Event(1,204),sink,error)==WaveEventResult::Sent&&sink.Valid(),"gate adds no health deadflag or team condition");network.Sent(204);
    monster->pev->deadflag=DEAD_NO;
    fixture.points.ConsumePointQuota(1,7);
    Check(fixture.pool.BeginDeath(*monster,20,fixture.points),"actual pool begins delayed death");
    Check(fixture.pool.ActiveCount()==1&&monster->pev->solid==SOLID_NOT,"dying monster remains borrowed but loses solid immediately");
    Check(DispatchWaveEvent(Event(1),sink,error)==WaveEventResult::Suppressed&&sink.Valid()&&network.calls.empty(),"death suppresses before delayed Free");
    monster->Think();
    Check(fixture.pool.ActiveCount()==0&&monster->TypeId()==1,"actual FreeThink parks entity retaining type identity");
    Check(DispatchWaveEvent(Event(1),sink,error)==WaveEventResult::Suppressed&&network.calls.empty(),"retained parked type is not a live gate");
    Check(DispatchWaveEvent(Event(9999,65535),sink,error)==WaveEventResult::Sent&&sink.Valid(),"original 9999 bypass ignores absent monster");network.Sent(-1);
    Check(DispatchWaveEvent(Event(30000,32768),sink,error)==WaveEventResult::Sent&&sink.Valid(),"original 30000 bypass ignores absent monster");network.Sent(-32768);
}
void FailureLatch()
{
    PoolFixture fixture;Network network;CCSOLScenarioMessages messages;ConfigError error;
    CCSOLScenarioEventSink unregistered(fixture.pool,messages);
    Check(DispatchWaveEvent(Event(9999),unregistered,error)==WaveEventResult::Sent&&!unregistered.Valid()&&network.calls.empty(),"dispatcher callback result does not hide failed real sender");
    Check(messages.Register(),"register after failed first sink");
    Check(DispatchWaveEvent(Event(9999),unregistered,error)==WaveEventResult::Sent&&!unregistered.Valid()&&network.calls.empty(),"failed sink stays failed and does not resume sending");
    CCSOLScenarioEventSink restored(fixture.pool,messages);
    Check(DispatchWaveEvent(Event(9999),restored,error)==WaveEventResult::Sent&&restored.Valid(),"fresh update sink may use repaired sender");network.Sent(193);
    CCSOLScenarioEventSink callbackMissing(fixture.pool,messages);
    g_engfuncs.pfnWriteShort=nullptr;
    Check(DispatchWaveEvent(Event(9999),callbackMissing,error)==WaveEventResult::Sent&&!callbackMissing.Valid()&&network.calls.empty(),"missing SDK callback fails before partial Begin");
    g_engfuncs.pfnWriteShort=Network::Short;
    callbackMissing.BroadcastEvent(std::array<std::uint8_t,4>{27,7,193,0});
    Check(!callbackMissing.Valid()&&network.calls.empty(),"callback repair does not erase prior update failure");
    CCSOLScenarioEventSink badPayload(fixture.pool,messages);
    badPayload.BroadcastEvent(std::array<std::uint8_t,4>{26,7,193,0});
    Check(!badPayload.Valid()&&network.calls.empty(),"wrong envelope fails through actual sender");
    CCSOLScenarioEventSink badRow(fixture.pool,messages);
    auto row=Event(9999);row.eventMessage="2147483648";
    Check(DispatchWaveEvent(row,badRow,error)==WaveEventResult::Invalid&&badRow.Valid()&&network.calls.empty(),"row error remains dispatcher responsibility and does not call sink");
}
void BrokenPool()
{
    PoolFixture fixture;Network network;CCSOLScenarioMessages messages;ConfigError error;
    Check(messages.Register(),"broken-pool sender registered");
    auto* monster=fixture.Acquire(0);Check(monster!=nullptr,"broken-pool actual entity allocated");
    if(!monster)return;
    fixture.engine.DestroyPrivate(monster->edict());
    Check(fixture.pool.State()==CSOLMonsterPoolState::Failed,"actual OnDestroy invalidates pool");
    CCSOLScenarioEventSink sink(fixture.pool,messages);
    Check(DispatchWaveEvent(Event(1),sink,error)==WaveEventResult::Suppressed&&!sink.Valid()&&network.calls.empty(),"failed actual registry invalidates sink instead of disguising failure as absent type");
    Check(DispatchWaveEvent(Event(9999),sink,error)==WaveEventResult::Sent&&!sink.Valid()&&network.calls.empty(),"failure remains visible even on later ungated event");
    CCSOLMonsterPool empty;
    CCSOLScenarioEventSink emptySink(empty,messages);
    Check(DispatchWaveEvent(Event(1),emptySink,error)==WaveEventResult::Suppressed&&!emptySink.Valid()&&network.calls.empty(),"uninitialized registry ordinary query fails sink");
}
}
int main()
{
    try{Lifecycle();FailureLatch();BrokenPool();}
    catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"scenario_event_sink checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
