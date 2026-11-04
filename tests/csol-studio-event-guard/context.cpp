// SPDX-License-Identifier: GPL-3.0-or-later
// Linked with the exact production StudioEvent guard prefix by run_context.py.
#include "../../native/csol/client/studio_event_context.hpp"
#include "../../native/csol/studio_event_guard.hpp"
#include <array>
#include <climits>
#include <cstdio>
#include <limits>

namespace csol::client
{
engine_studio_api_t studio{};
cl_enginefunc_t engine{};
void* currentStudioHeader{};
model_t* currentStudioModel{};
StudioEventRepeatGuard studioEventGuard;
unsigned dispatched{};
}
namespace
{
using namespace csol::client;
struct StudioBlob
{
    studiohdr_t header{};
    std::array<mstudioseqdesc_t, 2> descriptions{};
    std::array<mstudioevent_t, 4> events{};
} blob;
model_t generic{}, actual{}, otherModel{};
cl_entity_t entity{}, otherEntity{};
cl_entity_t* activeEntity{};
cl_entity_t* viewEntity{};
void* extra{};
unsigned checks{}, failures{};
cl_entity_t* Current() { return activeEntity; }
cl_entity_t* View() { return viewEntity; }
void* Extra(model_t*) { return extra; }
void Check(bool ok, const char* reason)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", reason); }
}
void Reset()
{
    blob = {};
    blob.header.length = sizeof(blob);
    blob.header.numseq = 2;
    blob.header.seqindex = offsetof(StudioBlob, descriptions);
    blob.descriptions[0].numevents = 3;
    blob.descriptions[0].eventindex = offsetof(StudioBlob, events);
    blob.descriptions[1].numevents = 1;
    blob.descriptions[1].eventindex = offsetof(StudioBlob, events) + 3 * sizeof(mstudioevent_t);
    blob.events[0].event = 5005;
    blob.events[1].event = 5004;
    blob.events[2].event = 5001;
    blob.events[3].event = 5008;
    actual.type = otherModel.type = generic.type = mod_studio;
    entity = {};
    entity.model = &generic;
    entity.curstate.animtime = entity.latched.sequencetime = 10;
    entity.curstate.framerate = 1;
    currentStudioModel = &actual;
    currentStudioHeader = extra = &blob.header;
    activeEntity = &entity;
    studio.GetCurrentEntity = Current;
    studio.Mod_Extradata = Extra;
    engine.GetViewModel = View;
    viewEntity = nullptr;
    studioEventGuard.Clear();
    dispatched = 0;
}
void Twice(const mstudioevent_t* event) { StudioEvent(event, &entity); StudioEvent(event, &entity); }
void PassThrough(const char* reason)
{
    Twice(&blob.events[0]);
    Check(dispatched == 2, reason);
}
}
int main()
{
    Reset(); Twice(&blob.events[0]);
    Check(dispatched == 1 && studioEventGuard.Suppressed() == 1, "verified frozen sequence suppresses repeat");
    StudioEvent(&blob.events[1], &entity); StudioEvent(&blob.events[2], &entity);
    Check(dispatched == 3, "distinct records including non-audio event still dispatch");
    StudioEvent(&blob.events[2], &entity);
    Check(dispatched == 3, "the repeated engine window cannot duplicate a one-shot visual event");
    entity.curstate.sequence = 1; StudioEvent(&blob.events[3], &entity);
    Check(dispatched == 4, "different sequence at the same time dispatches");
    entity.curstate.sequence = 0; StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 5, "return from another observed sequence starts a new instance");
    entity.curstate.frame = 200;
    ObserveStudioEventSequence(&entity, &actual, STUDIO_EVENTS);
    entity.curstate.frame = 0;
    entity.curstate.animtime = entity.latched.sequencetime = 11; Twice(&blob.events[0]);
    Check(dispatched == 6, "restarted instance fires once again");
    currentStudioModel = &otherModel; StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 7, "active-model change retires prior event state");
    studioEventGuard.Clear(); StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 8 && studioEventGuard.Suppressed() == 0, "scene clear permits same pointer and timestamp in a new session");

    Reset(); blob.descriptions[0].flags = STUDIO_LOOPING; PassThrough("actual player header loop flag overrides generic entity model");
    Reset(); entity.curstate.animtime = 11; PassThrough("advanced animation is not deduplicated");
    Reset(); entity.curstate.animtime = std::numeric_limits<float>::quiet_NaN(); PassThrough("unordered time is not the engine equality case");
    Reset(); activeEntity = &otherEntity; PassThrough("foreign active entity passes untouched");
    Reset(); extra = nullptr; PassThrough("stale active header passes untouched");
    Reset(); currentStudioHeader = nullptr; PassThrough("missing header passes untouched");
    Reset(); actual.type = mod_brush; PassThrough("non-studio context passes untouched");
    Reset(); mstudioevent_t foreign{}; Twice(&foreign); Check(dispatched == 2, "synthetic stack event is not a model event record");
    Reset(); Twice(reinterpret_cast<const mstudioevent_t*>(reinterpret_cast<const char*>(&blob.events[0]) + 1));
    Check(dispatched == 2, "unaligned foreign event is forwarded without dereference");
    Reset(); Twice(&blob.events[3]); Check(dispatched == 2, "another sequence record is not suppressed");
    Reset(); entity.curstate.sequence = -1; PassThrough("negative sequence passes untouched");
    Reset(); entity.curstate.sequence = 2; PassThrough("sequence outside active model passes untouched");
    Reset(); blob.header.length = sizeof(studiohdr_t) - 1; PassThrough("short studio header passes untouched");
    Reset(); blob.header.seqindex = INT_MAX; PassThrough("out-of-buffer sequence offset passes untouched");
    Reset(); blob.header.numseq = INT_MAX; PassThrough("sequence count overflow passes untouched");
    Reset(); blob.descriptions[0].eventindex = INT_MAX; PassThrough("out-of-buffer event offset passes untouched");
    Reset(); blob.descriptions[0].numevents = INT_MAX; PassThrough("event count overflow passes untouched");
    Reset(); blob.descriptions[0].numevents = 0; PassThrough("empty event table passes untouched");

    // Captured on B: one attack's first event was delivered at both packet
    // poses because both network timestamps were relatched as frame advanced.
    Reset(); entity.curstate.sequence = 0;
    entity.curstate.frame = 3; entity.curstate.animtime = entity.latched.sequencetime = 250.63f;
    entity.curstate.messagenum = 1;
    StudioEvent(&blob.events[0], &entity);
    entity.curstate.frame = 9; entity.curstate.animtime = entity.latched.sequencetime = 250.69f;
    entity.curstate.messagenum = 2;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 1, "live attack frame3 to9 timestamp relatch is one action");
    entity.curstate.frame = 121; entity.curstate.animtime = entity.latched.sequencetime = 251.89f;
    entity.curstate.messagenum = 3;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 1, "late relatch at advanced network frame cannot replay the first cue");

    // Event-free draw observes progress; the next identical sequence must be
    // allowed even when both first sound callbacks themselves report frame0.
    Reset(); StudioEvent(&blob.events[0], &entity);
    entity.curstate.frame = 200; entity.curstate.animtime = 11;
    ObserveStudioEventSequence(&entity, &actual, STUDIO_EVENTS);
    entity.curstate.frame = 0; entity.curstate.animtime = entity.latched.sequencetime = 12;
    ObserveStudioEventSequence(&entity, &actual, STUDIO_EVENTS);
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 2, "continuous same-sequence replay is retained through event-free draw observation");

    Reset(); StudioEvent(&blob.events[0], &entity);
    entity.curstate.frame = 200;
    ObserveStudioEventSequence(&entity, &actual, STUDIO_EVENTS);
    entity.curstate.frame = 0; entity.curstate.messagenum = 1;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 2, "newer packet may reset frame before advancing animtime");

    Reset(); entity.latched.sequencetime = 9;
    StudioEvent(&blob.events[0], &entity);
    entity.curstate.frame = 6; entity.curstate.animtime = entity.latched.sequencetime = 10.07f;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 1, "ordinary first dispatch is retained when a later update enters reset-window state");

    Reset(); viewEntity = &entity;
    StudioEvent(&blob.events[0], &entity);
    entity.curstate.animtime = entity.latched.sequencetime = 11;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 2, "viewmodel frame0 animtime restart preserves repeated gun animation");

    Reset(); StudioEvent(&blob.events[0], &entity);
    entity.curstate.sequence = 1;
    blob.descriptions[1].flags = STUDIO_LOOPING;
    ObserveStudioEventSequence(&entity, &actual, STUDIO_EVENTS);
    entity.curstate.sequence = 0;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 2, "event-free looping interval retires the preceding one-shot instance");

    Reset(); StudioEvent(&blob.events[0], &entity);
    entity.curstate.frame = 200;
    ObserveStudioEventSequence(&entity, &actual, STUDIO_RENDER);
    entity.curstate.frame = 0; entity.curstate.animtime = entity.latched.sequencetime = 11;
    StudioEvent(&blob.events[0], &entity);
    Check(dispatched == 1, "render-only shadow or attachment pass cannot establish event progress");

    Reset(); entity.curstate.framerate = -1; PassThrough("reverse playback is never suppressed by forward-play guard");
    std::printf("studio event SDK context: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
