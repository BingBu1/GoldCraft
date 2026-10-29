// SPDX-License-Identifier: GPL-3.0-or-later
// Actual SDK entities, EHANDLE, pool, animation reset and sound wrapper.
// Engine allocation/model/sound services are controlled; no gameplay claim.
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../../native/csol/server/scenario_monster_death.h"
#include "../../native/csol/server/scenario_monster_models.h"
#include "../../native/csol/scenario/monster_death.hpp"
#include <bit>
#include <iostream>
#include <limits>
#include <string_view>
#include "expected_death.hpp"

using namespace csol::scenario;
namespace
{
int checks{}, failures{};
void Check(bool value, const char* text)
{ ++checks; if (!value) { ++failures; std::cerr << "FAIL " << text << '\n'; } }
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Float(std::uint32_t value) { return std::bit_cast<float>(value); }
std::function<void(CBaseAnimating&)> afterReset;
int resets{};

struct Services
{
    inline static Services* current{};
    struct Sound { edict_t* entity; int channel; std::string sample; float volume, attenuation; int flags, pitch; };
    std::vector<std::string> precaches;
    std::vector<Sound> sounds;
    std::function<void()> onPrecache;
    std::function<void(edict_t*)> onSound;
    int failAt{}, queries{};
    struct Model { studiohdr_t header{}; std::array<mstudioseqdesc_t, 64> sequences{}; } model;
    Services()
    {
        current = this;
        g_engfuncs.pfnPrecacheSound = Precache;
        g_engfuncs.pfnEmitSound = Emit;
        g_engfuncs.pfnGetModelPtr = ModelPointer;
        model.header.numseq = static_cast<int>(model.sequences.size());
        model.header.seqindex = static_cast<int>(reinterpret_cast<char*>(model.sequences.data()) - reinterpret_cast<char*>(&model));
        for (auto& row : model.sequences) { row.fps = 10; row.numframes = 11; row.flags = 0; }
    }
    ~Services() { current = nullptr; afterReset = {}; }
    static int Precache(const char* name)
    {
        auto& self = *current; self.precaches.emplace_back(name);
        if (self.onPrecache) self.onPrecache();
        return self.failAt == static_cast<int>(self.precaches.size()) ? 0 : static_cast<int>(self.precaches.size()) + 10;
    }
    static void Emit(edict_t* entity, int channel, const char* sample, float volume, float attenuation, int flags, int pitch)
    {
        auto& self = *current;
        self.sounds.push_back({entity, channel, sample, volume, attenuation, flags, pitch});
        if (self.onSound) self.onSound(entity);
    }
    static void* ModelPointer(edict_t*) { ++current->queries; return &current->model; }
};

struct Fixture : PoolFixture
{
    Services services;
    CCSOLMonsterModels models;
    CCSOLMonsterAnimation animation;
    MonsterSequences sequences;
    Fixture()
    {
        resets = 0; engine.globals.time = 20;
        for (auto& type : types) type.appearance = "zombi_host";
        for (std::size_t i = 0; i < sequences.values.size(); ++i) sequences.values[i] = static_cast<std::int16_t>(i + 1);
        MonsterTypeTable table; table.rows.assign(types.begin(), types.end());
        for (std::size_t i = 0; i < table.rows.size(); ++i) table.byId[table.rows[i].Key()] = i;
        ConfigError error;
        if (!models.Precache(table, error)) throw std::runtime_error(error.message);
    }
    CCSOLScenarioMonster* Get(std::size_t priority = 0)
    {
        auto* monster = Acquire(priority);
        if (!monster) throw std::runtime_error("death fixture acquire failed");
        monster->pev->health = 100; monster->pev->takedamage = DAMAGE_AIM;
        monster->pev->flags |= FL_ONGROUND; monster->pev->frame = 72;
        monster->pev->nextthink = 77;
        points.ConsumePointQuota(1, 7);
        return monster;
    }
    std::uint32_t Quota() { return points.FindMonsterPoint(1, 7)->consumed; }
};

void CatalogAndRandom()
{
    for (const auto& expected : ExpectedDeaths)
    {
        const auto& row = MonsterAppearances()[expected.index];
        Check(row.index == expected.index && row.name == expected.name, "raw original catalog identity");
        Check(Bits(row.deathSeconds) == expected.seconds, "raw original death duration bits");
        Check(row.deathSound == expected.sound, "raw original death sound bytes including null-to-empty");
    }
    MonsterSequences sequences;
    for (std::size_t i = 0; i < sequences.values.size(); ++i) sequences.values[i] = static_cast<std::int16_t>(-100 + i);
    const std::array slots{MonsterSequence::Death1, MonsterSequence::Death2, MonsterSequence::Death3,
        MonsterSequence::Gutshot, MonsterSequence::Left, MonsterSequence::Back, MonsterSequence::Right, MonsterSequence::Forward};
    std::set<int> observed;
    for (unsigned seed = 0; seed < 64; ++seed)
    {
        std::srand(seed); const int first = std::rand(), second = std::rand();
        std::srand(seed); Check(SelectOrdinaryMonsterDeathSequence(nullptr, 1) == 0 && std::rand() == first, "null type returns zero without random");
        std::srand(seed); Check(SelectOrdinaryMonsterDeathSequence(&sequences, 1) == sequences[MonsterSequence::Head] && std::rand() == first, "Head signed slot uses no random");
        for (const int hitgroup : {0, 2, 7, -1, INT32_MAX, INT32_MIN})
        {
            std::srand(seed);
            Check(SelectOrdinaryMonsterDeathSequence(&sequences, hitgroup) == sequences[slots[first % 8]] && std::rand() == second,
                "nonhead exactly one CRT draw with original eight-slot order");
        }
        observed.insert(first % 8);
    }
    Check(observed.size() == 8, "random fixtures exercise every original slot");
}

void Timing()
{
    struct Case { MonsterDeathTiming input; float wanted; };
    for (const auto& c : {Case{{20, 3, false, 1, 0, 0, 98}, 23}, {{20, 60, true, 1, 0, 0, 98}, 23},
        {{20, 60, true, 10, 0, 0, 98}, 30}, {{20, 60, true, 10, 11, 0, 98}, 20},
        {{20, 60, true, 10, 13, 1, 361}, 25}, {{20, 60, true, 10, 11, -1, 236}, 25},
        {{20, 60, true, 10, 11, 0, 236}, 20}, {{20, 0, false, 1, 0, 0, 98}, 20},
        {{0, 0, false, 1, 0, 0, 98}, 0}, {{20, -30, false, 1, 0, 0, 98}, -10},
        {{20, 3, false, 1, -32768, -128, 361}, 25}, {{20, 3, false, 1, 13, 1, 237}, 20}})
    {
        float out = 555;
        Check(ResolveMonsterDeathDeadline(c.input, out) && Bits(out) == Bits(c.wanted), "ordered deadline and signed gates");
    }
    for (const auto bits : {0x7f800000u, 0xff800000u, 0x7fc12345u})
    {
        float out = 555;
        Check(!ResolveMonsterDeathDeadline({Float(bits), 3, false, 1, 0, 0, 98}, out) && out == 555, "invalid clock leaves output intact");
        Check(!ResolveMonsterDeathDeadline({20, Float(bits), true, 10, 11, 1, 236}, out) && out == 555,
            "local robustness boundary rejects nonfinite behavior input even if override could replace it");
    }
    float out = 555;
    const float max = std::numeric_limits<float>::max();
    Check(!ResolveMonsterDeathDeadline({max, max, false, 1, 0, 0, 98}, out) && out == 555, "final overflow does not publish");
    Check(ResolveMonsterDeathDeadline({max, max, false, 1, 11, 0, 98}, out) && out == max, "finite inputs may overflow early then be replaced");
}

void OrdinaryStages()
{
    for (const bool silent : {false, true}) for (const std::size_t priority : {0u, 1u, 2u})
    {
        Fixture f; auto* m = f.Get(priority); CCSOLMonsterDeathStages death(*m);
        Check(death.Current() == m && !death.Scheduled(), "valid borrowed death capture");
        Check(!death.MarkDead() && !death.ScheduleReturn(3, false, 1, 0, 98), "out of order phases refuse without consuming capture");
        Check(death.Animate(f.animation, f.sequences, 1), "head animation phase succeeds");
        Check(m->pev->health == 0 && m->pev->movetype == MOVETYPE_TOSS && !(m->pev->flags & FL_ONGROUND), "original preanimation entity writes");
        Check(m->pev->sequence == f.sequences[MonsterSequence::Head] && m->pev->frame == 0 && m->pev->framerate == 1, "actual SDK death animation applied");
        Check(resets == 1 && f.services.queries == 2 && m->pev->animtime == 20 && m->m_flFrameRate == 256, "actual ResetSequenceInfo consumed model");
        Check(!death.Animate(f.animation, f.sequences, 0), "repeated animation rejected");
        f.services.onSound = [&](edict_t* ed) {
            Check(ed == m->edict() && f.Quota() == (priority == 2 ? 1u : 0u), "quota return precedes sound engine callback");
            Check(ed->v.deadflag == DEAD_NO && ed->v.solid == SOLID_SLIDEBOX && ed->v.takedamage == DAMAGE_AIM,
                "sound precedes final three death writes");
            Check(!death.ReturnPointAndSound(f.points, f.models, false), "sound callback cannot recursively return quota");
        };
        Check(death.ReturnPointAndSound(f.points, f.models, silent), "point and sound phase succeeds");
        Check(f.Quota() == (priority == 2 ? 1u : 0u), "priority two bypass uses current type");
        Check(f.services.sounds.size() == (silent ? 0u : 1u), "silent skips ordinary sound only");
        if (!silent)
        {
            const auto& sound = f.services.sounds.front();
            Check(sound.sample == "zombi/ZBS_Death_1.wav" && sound.channel == 4 && Bits(sound.volume) == Bits(1) &&
                Bits(sound.attenuation) == Bits(.8f) && sound.flags == 0 && sound.pitch == 100, "exact SDK sound wrapper parameters");
        }
        Check(!death.ReturnPointAndSound(f.points, f.models, silent) && f.Quota() == (priority == 2 ? 1u : 0u), "point stage cannot double return");
        Check(m->pev->deadflag == DEAD_NO && m->pev->nextthink == 77, "owner money gap remains before final death state");
        Check(death.MarkDead(), "death flags phase succeeds before duration callbacks");
        Check(m->pev->takedamage == DAMAGE_NO && m->pev->deadflag == DEAD_DEAD && m->pev->solid == SOLID_NOT && m->pev->nextthink == 77,
            "duration getter sees death flags and old deadline");
        Check(!death.MarkDead(), "death flags cannot repeat");
        Check(death.ScheduleReturn(3, false, 1, 0, 98) && death.Scheduled() && m->pev->nextthink == 23, "late FreeThink installed with appearance deadline");
        Check(!death.ScheduleReturn(60, false, 1, 0, 98) && m->pev->nextthink == 23, "late scheduling cannot repeat");
        Check(f.pool.ActiveCount() == 1, "death does not immediately return ordinary slot");
        m->Think();
        Check(f.pool.ActiveCount() == 0 && m->m_pfnThink == nullptr && m->m_pfnTouch == nullptr, "actual SDK Think dispatch runs idempotent late Free");
    }
}

void EarlyFreeAndIdentity()
{
    for (int at = 0; at < 4; ++at)
    {
        Fixture f; auto* m = f.Get(); const auto lease = f.pool.Lease(*m); CCSOLMonsterDeathStages death(*m);
        if (at == 0) Check(f.pool.Free(*m), "beforeDeath early Free");
        if (at == 1) afterReset = [&](CBaseAnimating&) { Check(f.pool.Free(*m), "animation posthook early Free"); };
        Check(death.Animate(f.animation, f.sequences, 1), "same lease early Free permits animation continuation");
        afterReset = {};
        if (at == 2) f.services.onSound = [&](edict_t*) { Check(f.pool.Free(*m), "sound callback early Free"); };
        Check(death.ReturnPointAndSound(f.points, f.models, false), "same lease permits quota and sound");
        if (at == 3) Check(f.pool.Free(*m), "owner money gap early Free");
        Check(death.Current() == m && f.pool.Lease(*m) == lease && f.pool.ActiveCount() == 0, "Free retains lease and live type");
        Check(death.MarkDead() && death.ScheduleReturn(3, false, 1, 0, 98), "same lease schedules late Think despite Borrowed false");
        m->Think(); Check(f.pool.ActiveCount() == 0 && f.Quota() == 0 && !m->m_pfnThink, "late Free neither double queues nor returns quota");
    }
    {
        Fixture f; auto* m = f.Get(); CCSOLMonsterDeathStages death(*m); const auto lease = f.pool.Lease(*m);
        f.Fill(38); Check(f.pool.Free(*m), "make exactly one free slot");
        auto* newer = f.Acquire(0);
        Check(newer == m && f.pool.Lease(*newer) != lease && !death.Current(), "same pointer new Acquire lease invalidates old death");
        Check(!death.Animate(f.animation, f.sequences, 1) && newer->pev->movetype == MOVETYPE_STEP, "old death cannot mutate new occupant");
    }
    for (int phase = 0; phase < 4; ++phase)
    {
        Fixture f; auto* m = f.Get(); CCSOLMonsterDeathStages death(*m);
        if (phase >= 1) Check(death.Animate(f.animation, f.sequences, 1), "restart setup animate");
        if (phase >= 2) Check(death.ReturnPointAndSound(f.points, f.models, true), "restart setup point");
        if (phase >= 3) Check(death.MarkDead(), "restart setup death flags");
        Check(f.pool.RestartRound() && !death.Current(), "RestartRound clears type and invalidates old stage");
        Check(!death.Animate(f.animation, f.sequences, 1) && !death.ReturnPointAndSound(f.points, f.models, true) &&
            !death.MarkDead() && !death.ScheduleReturn(3, false, 1, 0, 98), "round reset blocks every remaining stage");
        Check(m->pev->movetype == MOVETYPE_NONE && !m->m_pfnThink, "round reset parked state stays intact");
    }
    {
        Fixture f; auto* m = f.Get(); f.types[0].skill = 0; CCSOLMonsterDeathStages death(*m);
        Check(death.Animate(f.animation, f.sequences, 1), "live type setup animate");
        f.types[0].priority = 2;
        Check(death.ReturnPointAndSound(f.points, f.models, true) && f.Quota() == 1, "point gate uses live priority not acquired category");
        Check(death.MarkDead(), "live type setup mark");
        f.types[0].priority = 0; f.types[0].skill = 0;
        Check(death.ScheduleReturn(60, false, 1, 11, 361) && m->pev->nextthink == 20, "slotSkill is copied attribute and map priority is live");
    }
    {
        Fixture f; auto* m = f.Get(); CCSOLMonsterDeathStages death(*m);
        Check(death.Animate(f.animation, f.sequences, 1) && death.ReturnPointAndSound(f.points, f.models, true) && death.MarkDead(), "live map override setup");
        f.types[0].priority = 1; f.types[0].skill = 0;
        Check(death.ScheduleReturn(60, false, 1, 13, 236) && m->pev->nextthink == 25, "changed live priority overrides copied slot skill");
    }
    for (const bool duringSound : {false, true})
    {
        Fixture f; auto* m = f.Get(); f.Fill(38); CCSOLMonsterDeathStages death(*m);
        const auto lease = f.pool.Lease(*m);
        const auto reborrow = [&] {
            Check(f.pool.Free(*m), "callback frees captured slot");
            Check(f.Acquire(0) == m && f.pool.Lease(*m) != lease, "callback reacquires exact entity with new lease");
            m->pev->nextthink = 123;
        };
        if (duringSound)
        {
            Check(death.Animate(f.animation, f.sequences, 1), "sound reborrow setup");
            f.services.onSound = [&](edict_t*) { reborrow(); };
            Check(!death.ReturnPointAndSound(f.points, f.models, false), "same entity sound reborrow rejects old phase commit");
        }
        else
        {
            afterReset = [&](CBaseAnimating&) { reborrow(); };
            Check(!death.Animate(f.animation, f.sequences, 1), "same entity reset posthook reborrow rejects old phase commit");
        }
        afterReset = {}; f.services.onSound = {};
        Check(!death.Current() && !death.MarkDead() && !death.ScheduleReturn(3, false, 1, 0, 98), "new lease cannot resume old death");
        Check(m->pev->movetype == MOVETYPE_STEP && m->pev->nextthink == 123, "old phase preserves reborrowed occupant state");
    }
}

void RejectionAndSentinels()
{
    {
        Fixture f; auto* m = f.Get(); m->pev->health = -1; CCSOLMonsterDeathStages death(*m);
        Check(death.Current() == m, "lethal negative health is not death-entry gate");
        m->pev->deadflag = DEAD_DEAD; CCSOLMonsterDeathStages alreadyDead(*m);
        Check(!alreadyDead.Current() && !alreadyDead.Animate(f.animation, f.sequences, 1), "DEAD_DEAD rejects fresh death entry");
    }
    {
        Fixture f; auto* m = f.Get(); Check(f.pool.Free(*m), "fresh parked capture setup");
        CCSOLMonsterDeathStages parked(*m);
        Check(!parked.Current(), "new capture cannot start from returned slot");
    }
    for (const int sequence : {-3, -1, 4})
    {
        Fixture f; auto* m = f.Get(); m->pev->sequence = 4; m->pev->framerate = 7;
        f.animation.SynchronizeSequence(4); f.sequences[MonsterSequence::Head] = static_cast<std::int16_t>(sequence);
        CCSOLMonsterDeathStages death(*m);
        Check(death.Animate(f.animation, f.sequences, 1), "death setter sentinel accepted");
        Check(m->pev->sequence == (sequence == -1 ? 0 : 4) && m->pev->frame == (sequence == -3 ? 72 : 0), "death sentinel sequence/frame behavior");
        Check(m->pev->framerate == (sequence == -3 ? 7 : 1) && resets == (sequence == -1 ? 1 : 0), "death same cache and minus-three avoid SDK reset");
    }
    {
        Fixture f; auto* m = f.Get(); CCSOLMonsterDeathStages death(*m);
        Check(death.Animate(f.animation, f.sequences, 1), "missing sound setup");
        g_engfuncs.pfnEmitSound = nullptr;
        Check(!death.ReturnPointAndSound(f.points, f.models, false) && f.Quota() == 0, "sound failure follows quota side effect");
        Check(!death.ReturnPointAndSound(f.points, f.models, true) && f.Quota() == 0 && !death.MarkDead(), "sound failure cannot retry quota or advance");
    }
    {
        Fixture f; auto* m = f.Get(); CCSOLMonsterDeathStages death(*m);
        Check(death.Animate(f.animation, f.sequences, 1) && death.ReturnPointAndSound(f.points, f.models, true) && death.MarkDead(), "invalid deadline setup");
        Check(!death.ScheduleReturn(std::numeric_limits<float>::infinity(), false, 1, 0, 98) && m->pev->nextthink == 77 && !m->m_pfnThink,
            "invalid adjusted duration leaves deadline and Think intact");
        Check(!death.ScheduleReturn(3, false, 1, 0, 98), "failed deadline phase cannot be silently retried");
    }
}

enum class Fault { Destroy, Replace, Kill, Recycle, Missing, Restart, Shutdown };
void CallbackInvalidation()
{
    for (const bool duringSound : {false, true})
    for (const auto fault : {Fault::Destroy, Fault::Replace, Fault::Kill, Fault::Recycle, Fault::Missing, Fault::Restart, Fault::Shutdown})
    {
        Fixture f; auto* m = f.Get(); auto* edict = m->edict(); CCSOLMonsterDeathStages death(*m);
        const auto invalidate = [&] {
            switch (fault)
            {
            case Fault::Destroy: f.engine.DestroyPrivate(edict); break;
            case Fault::Replace: f.engine.ReplaceWithBase(edict); break;
            case Fault::Kill: edict->v.flags |= FL_KILLME; break;
            case Fault::Recycle: ++edict->serialnumber; break;
            case Fault::Missing: f.engine.orphanedPrivate.push_back(edict->pvPrivateData); edict->pvPrivateData = nullptr; break;
            case Fault::Restart: Check(f.pool.RestartRound(), "callback round reset succeeds"); break;
            case Fault::Shutdown: Check(f.pool.Shutdown(), "callback pool shutdown succeeds"); break;
            }
        };
        if (duringSound)
        {
            Check(death.Animate(f.animation, f.sequences, 1), "sound invalidation setup");
            f.services.onSound = [&](edict_t*) { invalidate(); };
            Check(!death.ReturnPointAndSound(f.points, f.models, false), "sound invalidation prevents stage commit");
        }
        else
        {
            afterReset = [&](CBaseAnimating&) { invalidate(); };
            Check(!death.Animate(f.animation, f.sequences, 1), "SDK reset posthook invalidation prevents stage commit");
        }
        afterReset = {}; f.services.onSound = {};
        Check(!death.Current() && !death.MarkDead() && !death.ScheduleReturn(3, false, 1, 0, 98), "invalid identity cannot resume tail");
        Check(f.Quota() == (duringSound ? 0u : 1u), "earlier quota side effect is neither undone nor duplicated");
    }
}

void DeletedOwner()
{
    // Guard pages make a stale pool read deterministic, not dependent on heap reuse.
    PoolEngine engine; SpawnPoints points; MonsterType type; type.id = 1; type.appearance = "zombi_host";
    void* memory = VirtualAlloc(nullptr, sizeof(CCSOLMonsterPool), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!memory) throw std::runtime_error("pool guard allocation failed");
    auto* pool = new (memory) CCSOLMonsterPool;
    Check(pool->Initialize(), "guarded pool initialized");
    auto* m = pool->Acquire(type, 7, points);
    if (!m) throw std::runtime_error("guarded pool acquire failed");
    CCSOLMonsterDeathStages death(*m);
    pool->~CCSOLMonsterPool();
    DWORD oldProtect{};
    if (!VirtualProtect(memory, sizeof(CCSOLMonsterPool), PAGE_NOACCESS, &oldProtect)) throw std::runtime_error("pool guard protection failed");
    Check(!death.Current(), "owner deletion resolves detached entity without reading inaccessible pool");
    VirtualFree(memory, 0, MEM_RELEASE);
}

void SoundCache()
{
    PoolFixture f; Services services; CCSOLMonsterModels models; ConfigError error;
    MonsterTypeTable table;
    for (const auto& appearance : MonsterAppearances())
    {
        if (FindMonsterAppearance(appearance.name) != &appearance) continue;
        MonsterType row; row.id = appearance.index + 1; row.appearance = appearance.name;
        table.byId[row.Key()] = table.rows.size(); table.rows.push_back(row);
    }
    std::set<std::string> unique;
    for (const auto& row : table.rows)
    {
        const auto* appearance = FindMonsterAppearance(row.appearance);
        if (!appearance->deathSound.empty()) unique.emplace(appearance->deathSound);
    }
    bool nestedReset = true, nestedPrecache = true;
    services.onPrecache = [&] { nestedReset = models.Reset(); ConfigError nested; nestedPrecache = models.Precache(table, nested); };
    Check(models.Precache(table, error) && !nestedReset && !nestedPrecache, "sound precache rejects reentrant cache mutation");
    Check(services.precaches.size() == unique.size() && std::set<std::string>(services.precaches.begin(), services.precaches.end()) == unique,
        "all effective catalog sounds cached once by exact path");
    services.onPrecache = {};
    auto* m = f.Acquire(0); Check(m != nullptr, "sound direct subject acquired"); if (!m) return;
    for (const auto& row : table.rows)
    {
        const auto* appearance = FindMonsterAppearance(row.appearance); const auto before = services.sounds.size();
        Check(models.EmitDeathSound(*m, *appearance), "every cached sound or original empty entry succeeds");
        Check(services.sounds.size() == before + (appearance->deathSound.empty() ? 0u : 1u), "empty entries make no engine call");
    }
    auto copy = MonsterAppearances()[0]; Check(!models.EmitDeathSound(*m, copy), "copied catalog row rejected");
    Check(models.Reset() && !models.EmitDeathSound(*m, MonsterAppearances()[0]), "reset invalidates sound cache");
    services.precaches.clear(); services.failAt = 2;
    Check(!models.Precache(table, error) && !models.Ready() && !models.EmitDeathSound(*m, MonsterAppearances()[0]), "partial sound failure commits no cache");
    services.failAt = 0; services.precaches.clear();
    Check(models.Precache(table, error), "sound cache recovers after failure");
    g_engfuncs.pfnEmitSound = nullptr;
    Check(!models.EmitDeathSound(*m, MonsterAppearances()[0]), "missing engine emit fails without dereference");
    g_engfuncs.pfnPrecacheSound = nullptr;
    Check(!models.Precache(table, error) && !models.Ready(), "missing engine sound precache fails atomically");
}

int Oracle()
{
    char kind{};
    while (std::cin >> kind)
    {
        if (kind == 'S')
        {
            int present{}, hit{}; unsigned seed{}; MonsterSequences seq;
            if (!(std::cin >> present >> hit >> seed)) return 2;
            for (std::size_t i = 15; i < 24; ++i) { int n{}; if (!(std::cin >> n)) return 2; seq.values[i] = static_cast<std::int16_t>(n); }
            std::srand(seed); const int first = std::rand(), second = std::rand(); std::srand(seed);
            const int selected = SelectOrdinaryMonsterDeathSequence(present ? &seq : nullptr, hit); const int next = std::rand();
            std::cout << selected << ' ' << next << ' ' << first << ' ' << second << '\n';
        }
        else if (kind == 'D')
        {
            std::uint32_t now{}, duration{}; int shortDeath{}, subtype{}, skill{}, priority{}, map{};
            if (!(std::cin >> now >> duration >> shortDeath >> subtype >> skill >> priority >> map)) return 2;
            float result = 555;
            const bool accepted = ResolveMonsterDeathDeadline({Float(now), Float(duration), shortDeath != 0, subtype,
                static_cast<std::int16_t>(skill), static_cast<std::int8_t>(priority), map}, result);
            std::cout << accepted << ' ' << Bits(result) << '\n';
        }
        else return 2;
    }
    return std::cin.eof() ? 0 : 2;
}
}

// Execute the actual SDK body, then model a ReAPI posthook boundary. The
// registry dispatch itself and arbitrary destructive prehooks are not covered.
void CBaseAnimating::ResetSequenceInfo()
{
    ++resets; ResetSequenceInfo_OrigFunc(); if (afterReset) afterReset(*this);
}
int SENTENCEG_Lookup(const char*, char (&)[32])
{ throw std::runtime_error("unexpected sentence in original death sound catalog"); }

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--oracle") return Oracle();
    if (argc != 1) return 2;
    try { CatalogAndRandom(); Timing(); OrdinaryStages(); EarlyFreeAndIdentity(); RejectionAndSentinels(); CallbackInvalidation(); DeletedOwner(); SoundCache(); }
    catch (const std::exception& error) { ++failures; std::cerr << error.what() << '\n'; }
    std::cout << "monster_death checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
