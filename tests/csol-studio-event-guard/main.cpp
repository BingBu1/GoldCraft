// SPDX-License-Identifier: GPL-3.0-or-later
// Replays the GoldSrc R_StudioClientEvents window (stock hw 101F1258..101F12F6)
// with and without the production repeat guard, using the real Oberon
// zbs_bossL_big02.mdl sequence timing/events.
#include "../../native/csol/studio_event_guard.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

namespace {
struct Event { int frame; };
struct Sequence { float fps; int frames; bool looping; std::vector<Event> events; };
struct Entity { int sequence{}; float frame{}; float animtime{}; float sequencetime{}; float framerate{1}; };

unsigned checks{}, failures{};
void Check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", what); }
}

// Fires events like the engine; returns how many reach the client callback.
struct Client
{
    csol::StudioEventRepeatGuard guard;
    bool guarded{};
    int dispatched{};
    // Engine: end = StudioEstimateFrame at cl.time; start subtracts the same
    // frame's (cl.time - cl.oldtime) advance, so consecutive windows abut.
    void Frame(const Entity& entity, const Sequence& sequence, double now, double previous)
    {
        guard.Observe(&entity, &sequence, entity.sequence, sequence.looping,
            entity.frame, entity.animtime, entity.framerate, 0, false);
        double end = entity.frame + (now - entity.animtime) * sequence.fps * entity.framerate;
        if (sequence.looping) end = std::fmod(end, double(sequence.frames));
        else if (end > sequence.frames - 1) end = double(sequence.frames - 1);
        double start = end - entity.framerate * (now - previous) * sequence.fps;
        if (entity.sequencetime == entity.animtime && !sequence.looping) start = -0.01;
        for (const auto& event : sequence.events)
            if (double(event.frame) > start && double(event.frame) <= end)
            {
                if (guarded && guard.Repeated(&entity, &sequence, entity.sequence, sequence.looping,
                        entity.sequencetime, entity.animtime, &event)) continue;
                ++dispatched;
            }
    }
};

// Runs the client at fps; the server advances animtime every serverStep
// seconds when serverStep > 0 (0 = frozen, as after a deadline FreeThink).
// 97 FPS keeps window edges off integer event frames (100 FPS with a 30 fps
// sequence lands an edge exactly on frame 66 and both paths skip it).
int Run(bool guarded, const Sequence& sequence, double duration, double serverStep, double fps = 97.0)
{
    Client client;
    client.guarded = guarded;
    Entity entity;
    entity.animtime = entity.sequencetime = 10.0f; // sequence change latches sequencetime
    const double begin = entity.animtime;
    const int frames = int(duration * fps);
    double nextServer = begin + serverStep, previous = begin;
    for (int i = 1; i < frames; ++i)
    {
        const double now = begin + i / fps;
        if (serverStep > 0 && now >= nextServer)
        {
            // StudioFrameAdvance: frame += interval*fps; animtime = server time.
            entity.frame += float((nextServer - entity.animtime) * sequence.fps);
            if (!sequence.looping && entity.frame > sequence.frames - 1) entity.frame = float(sequence.frames - 1);
            entity.animtime = float(nextServer);
            nextServer += serverStep;
        }
        client.Frame(entity, sequence, now, previous);
        previous = now;
    }
    return client.dispatched;
}
}

int main()
{
    // Original zbs_bossL_big02.mdl sequences (models/monster).
    const Sequence death{30, 301, false, {{1}}};                  // 5005 death.wav
    const Sequence attack3{30, 121, false, {{1}, {66}}};          // jump + attack3
    const Sequence bomb{15, 168, false, {{44}, {88}, {133}}};     // attack_bomb x3
    const Sequence walk{25, 101, true, {{5}, {30}, {55}, {80}}};  // 5008 footsteps

    const int frozenRaw = Run(false, death, 60.0f, 0);
    const int frozenGuarded = Run(true, death, 60.0f, 0);
    std::printf("death 60s frozen animtime: raw=%d guarded=%d\n", frozenRaw, frozenGuarded);
    Check(frozenRaw > 1000, "engine model reproduces the per-frame death.wav replay");
    Check(frozenGuarded == 1, "dead Oberon plays death.wav once");

    for (const float step : {1.0f / 30.0f, 0.1f})
    {
        const int raw = Run(false, attack3, 4.0f, step), guarded = Run(true, attack3, 4.0f, step);
        std::printf("attack3 server step %.4f: raw=%d guarded=%d\n", step, raw, guarded);
        Check(raw >= 2, "raw attack events fire at least once each");
        Check(guarded == 2, "attack3 jump/impact sounds each play once");
        Check(step > 0.05f ? raw > guarded : raw == guarded, "slower server updates duplicate frame-1 cues without the guard");
        const int bombRaw = Run(false, bomb, 12.0f, step), bombGuarded = Run(true, bomb, 12.0f, step);
        std::printf("attack_bomb server step %.4f: raw=%d guarded=%d\n", step, bombRaw, bombGuarded);
        Check(bombGuarded == 3, "attack_bomb three cues each play once");
    }
    Check(Run(true, bomb, 12.0f, 0) == 3, "frozen bomb sequence still plays all three cues once");

    // Looping sequences are never in the engine reset state: identical counts.
    const int walkRaw = Run(false, walk, 8.0f, 0), walkGuarded = Run(true, walk, 8.0f, 0);
    std::printf("walk looping 8s: raw=%d guarded=%d\n", walkRaw, walkGuarded);
    Check(walkRaw > 4 && walkRaw == walkGuarded, "looping footsteps repeat each cycle unchanged");

    // A new sequence instance (new animtime) fires again; Clear forgets state.
    csol::StudioEventRepeatGuard guard;
    const int entity{}, model{};
    const Event cue{1};
    guard.Observe(&entity, &model, 20, false, 0, 5, 1, 1, true);
    Check(!guard.Repeated(&entity, &model, 20, false, 5, 5, &cue), "first dispatch passes");
    Check(guard.Repeated(&entity, &model, 20, false, 5, 5, &cue), "same instance repeat suppressed");
    guard.Observe(&entity, &model, 20, false, 0, 7, 1, 2, true);
    Check(!guard.Repeated(&entity, &model, 20, false, 7, 7, &cue), "restarted viewmodel sequence fires again");
    Check(!guard.Repeated(&entity, &model, 20, false, 7, 8, &cue), "advanced animtime is not filtered");
    Check(!guard.Repeated(&entity, &model, 20, true, 7, 7, &cue), "looping sequence is not filtered");
    const int other{};
    guard.Observe(&other, &model, 20, false, 0, 7, 1, 2, true);
    Check(!guard.Repeated(&other, &model, 20, false, 7, 7, &cue), "entities are independent");
    guard.Clear();
    guard.Observe(&entity, &model, 20, false, 0, 7, 1, 2, true);
    Check(!guard.Repeated(&entity, &model, 20, false, 7, 7, &cue), "clear forgets session state");
    Check(guard.Suppressed() == 0, "clear starts a new session suppression counter");

    std::printf("studio event guard: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
