// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"

namespace model_events_test
{
namespace
{
void Ready(Fixture& f)
{
    auto& monster = f.Spawn();
    monster.pev->sequence = 1; monster.pev->modelindex = 7;
    monster.pev->frame = 0; monster.pev->animtime = 5; monster.pev->framerate = 1;
    monster.m_flFrameRate = 100; monster.m_flLastEventCheck = 5;
    monster.m_fSequenceFinished = FALSE;
    f.model.sequence[1].numframes = 257;
    f.model.sequence[1].flags = 0;
}
std::vector<int> Ids(const Fixture& f)
{
    std::vector<int> result;
    for (const auto& event : f.eventCalls) result.push_back(event.id);
    return result;
}
}
void ScanCases()
{
    {
        Fixture f(true); Ready(f);
        f.SetEvents({{0,101},{9,102},{10,103},{0,5000},{0,5001}});
        Require(f.Dispatch(), "actual SDK scan accepts valid model event table");
        Check(Ids(f) == std::vector<int>({101,102}), "event window includes start, excludes end and all client events");
        Check(f.eventCalls[0].options == "original-option", "model event options reach virtual consumer");
        Check(Near(f.Monster().m_flLastEventCheck, 5.1f) && !f.Monster().m_fSequenceFinished,
            "fixed .1 interval advances event clock and resets finished false");
        f.eventCalls.clear(); f.Monster().pev->frame = 10; f.Monster().pev->animtime = 5.1f;
        Require(f.Dispatch(), "next actual frame scans next half-open interval");
        Check(Ids(f) == std::vector<int>({103}), "prior end-boundary event delivered once next frame");
    }
    {
        Fixture f(true); Ready(f); f.Monster().pev->frame = 250;
        f.model.sequence[1].flags = STUDIO_LOOPING;
        f.SetEvents({{250,201},{255,202},{0,203},{3,204},{4,205}});
        Require(f.Dispatch(), "looping SDK event window wraps");
        Check(Ids(f) == std::vector<int>({201,202,203,204}), "wrap retains table order and excludes wrapped end");
        Check(f.Monster().m_fSequenceFinished, "end beyond256 marks sequence finished");
    }
    {
        Fixture f(true); Ready(f); f.Monster().pev->frame = 250;
        f.SetEvents({{250,201},{255,202},{0,203}});
        Require(f.Dispatch(), "non-looping SDK end scan succeeds");
        Check(Ids(f) == std::vector<int>({201,202}), "non-looping sequence never emits wrapped initial event");
    }
    for (int frames : {0,1})
    {
        Fixture f(true); Ready(f); f.model.sequence[1].numframes = frames;
        f.SetEvents({{0,301},{1,302}});
        Require(f.Dispatch(), "zero and one frame sequence retain original SDK special window");
        Check(Ids(f) == std::vector<int>({301}), "SDK frame-count special case scans[0,1)");
    }
    {
        Fixture f(true); Ready(f); f.Monster().pev->framerate = 0; f.Monster().pev->frame = 20;
        f.SetEvents({{20,401}}); Require(f.Dispatch(), "frozen zero-rate event scan remains valid");
        Check(f.eventCalls.empty() && Near(f.Monster().m_flLastEventCheck,5.1f),
            "zero rate still updates fixed time clock without inventing event interval");
    }
    {
        Fixture f(true); Ready(f); f.Monster().pev->frame = 10; f.Monster().pev->framerate = -1;
        f.SetEvents({{0,401},{5,402},{10,403}}); Require(f.Dispatch(), "reverse rate follows SDK interval behavior");
        Check(f.eventCalls.empty() && f.Monster().m_fSequenceFinished, "reverse end<=0 marks finished without reversing SDK event order");
    }
    {
        Fixture f; Ready(f); f.SetEvents({{0,101},{9,102}});
        const auto health = f.Monster().pev->health;
        Require(f.Dispatch(), "production ordinary monster inherits default empty event consumer");
        Check(f.eventCalls.empty() && f.Monster().pev->health == health,
            "default handler creates no sound, damage or custom service event");
    }
}
void ValidationCases()
{
    for (int test = 0; test < 12; ++test)
    {
        Fixture f(true); Ready(f); f.SetEvents({{0,101}});
        auto& header = f.model.header; auto& sequence = f.model.sequence[1]; auto& monster = f.Monster();
        switch(test)
        {
        case 0: header.length = sizeof(studiohdr_t)-1; break;
        case 1: header.seqindex = -1; break;
        case 2: header.numseq = -1; break;
        case 3: header.seqindex = header.length; break;
        case 4: monster.pev->sequence = -1; break;
        case 5: monster.pev->sequence = header.numseq; break;
        case 6: sequence.numframes = -1; break;
        case 7: sequence.numevents = -1; break;
        case 8: sequence.eventindex = header.length; break;
        case 9: monster.pev->frame = std::numeric_limits<float>::quiet_NaN(); break;
        case 10: monster.m_flLastEventCheck = std::numeric_limits<float>::infinity(); break;
        case 11: monster.pev->animtime = std::numeric_limits<float>::infinity(); break;
        }
        monster.m_fSequenceFinished = TRUE;
        const float clock = monster.m_flLastEventCheck;
        Check(!f.Dispatch() && f.eventCalls.empty(), "malformed span or nonfinite window rejected before virtual event");
        Check(monster.m_fSequenceFinished && (monster.m_flLastEventCheck == clock),
            "invalid model does not publish event clock or finished state");
    }
    {
        Fixture f(true); Ready(f); f.nullModel = true;
        f.Monster().m_fSequenceFinished = TRUE;
        Check(f.Dispatch() && f.Monster().m_flLastEventCheck == 5 && f.Monster().m_fSequenceFinished,
            "original missing-model path succeeds without touching event clocks");
    }
    {
        Fixture f(true); Ready(f); g_engfuncs.pfnGetModelPtr = nullptr;
        Check(!f.Dispatch() && f.Monster().m_flLastEventCheck == 5,
            "missing engine model service rejects before clock writes");
    }
}
void CancellationCases()
{
    for (int change = 0; change < 4; ++change)
    {
        Fixture f(true); Ready(f); f.SetEvents({{0,101},{1,102}});
        bool valid = true;
        f.onModel = [&]
        {
            if (change == 0) ++f.Monster().pev->sequence;
            if (change == 1) ++f.Monster().pev->modelindex;
            if (change == 2) valid = false;
            if (change == 3) f.ReleaseMap();
        };
        Check(!f.Dispatch([](const void* pointer){return *static_cast<const bool*>(pointer);},&valid),
            "model callback cancellation stops stale dispatcher before model data access");
        Check(f.eventCalls.empty(), "model callback cancellation emits no event");
    }
    for (int change = 0; change < 5; ++change)
    {
        Fixture f(true); Ready(f); f.SetEvents({{0,101},{1,102}});
        bool valid = true;
        f.onEvent = [&]
        {
            if (change == 0) ++f.Monster().pev->sequence;
            if (change == 1) ++f.Monster().pev->modelindex;
            if (change == 2) valid = false;
            if (change == 3) f.ReleaseMap();
            if (change == 4) f.Reborrow(f.Monster());
        };
        Check(!f.Dispatch([](const void* pointer){return *static_cast<const bool*>(pointer);},&valid),
            "event virtual callback rechecks sequence model owner session and lease");
        Check(Ids(f) == std::vector<int>({101}), "cancelled first event prevents delivery of second event");
    }
}
void OwnerIntegrationCases()
{
    {
        Fixture f(true); auto& monster = f.Spawn(); auto& state = f.CaptureState();
        state.position.updateAfter = 10000;
        f.SpawnState().animationAt = 0; f.SpawnState().updateAt = 10000;
        monster.pev->sequence = 1; monster.pev->frame = 0; monster.pev->animtime = 100;
        monster.pev->framerate = 1; monster.m_flFrameRate = 100; monster.m_flLastEventCheck = 100;
        f.model.sequence[1].numframes = 257; f.model.sequence[1].flags = 0;
        f.SetEvents({{0,101},{9,102},{10,103}});
        // Actual StudioFrameAdvance adds3.125. The fixed event end is13.125,
        // and lastCheck compensation leaves the scan start exactly0.
        f.Tick(100.03125f);
        Check(Ids(f) == std::vector<int>({101,102,103}), "ordinary Think uses real SDK frame advance then model event dispatcher");
        Check(Near(monster.pev->frame,3.125f) && Near(monster.m_flLastEventCheck,100.13125f),
            "ordinary event interval remains .1 rather than 30Hz frame elapsed");
        Check(Near(f.SpawnState().animationAt,100.03125f+Frame), "successful real dispatch publishes actual factory animation clock");
    }
    for (bool fromEvent : {false,true})
    {
        Fixture f(true); auto& monster = f.Spawn(); f.CaptureState();
        f.SpawnState().animationAt = 0; f.SpawnState().updateAt = 10000;
        monster.pev->sequence = 1; monster.pev->frame = 0; monster.pev->animtime = 101;
        monster.m_flLastEventCheck = 101; monster.m_flFrameRate = 100; monster.pev->framerate = 1;
        f.model.sequence[1].numframes = 257; f.SetEvents({{0,201},{1,202}});
        bool reset{};
        auto cancel = [&]
        {
            if (reset) return;
            reset = true;
            Require(f.Reset(), "same-lease reset during real event model or handler callback");
            f.SpawnState().animationAt = 777;
        };
        if (fromEvent) f.onEvent = cancel; else f.onModel = cancel;
        f.Tick(101);
        Check(reset && f.SpawnState().animationAt == 777 && f.Behavior().Error().message.empty(),
            "actual ordinary owner guard cancels same-lease event path without overwriting new clock");
        Check(f.eventCalls.size() == (fromEvent ? 1u : 0u), "real Think stops event iteration at reset boundary");
    }
    for(bool missing : {false,true})
    {
        Fixture f; f.Spawn(); f.CaptureState();
        f.SpawnState().animationAt=0;f.SpawnState().updateAt=10000;
        f.nullModel=missing;
        if(!missing)f.model.header.seqindex=-1;
        f.Tick(101);
        Check(f.Behavior().Error().message.empty()==missing,
            "ordinary Think distinguishes original null-model no-op from malformed model failure");
        Check((f.SpawnState().animationAt>101)==missing,
            "only successful null-model dispatch publishes next animation deadline");
    }
}
}
