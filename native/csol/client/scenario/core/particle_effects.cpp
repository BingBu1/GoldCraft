// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "particle_effects.hpp"
#include "precise_trigonometry.hpp"
#include "../../../scenario/bosses/teddy_spark_particle.hpp"
#include "../../../scenario/bosses/teddy_blackhole_smoke.hpp"
#include "../../../scenario/bosses/teddy_sliding.hpp"
#include "../../../scenario/bosses/teddy_meteor.hpp"
#include "../../../scenario/core/emitter_runtime.hpp"
#include "../../../scenario/core/particle_runtime.hpp"
#include "../../../scenario/core/particle_visibility.hpp"
#include <event_api.h>
#include <pmtrace.h>
#include <pm_movevars.h>
#include <triangleapi.h>
#include <ostream>
#include <variant>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
namespace spark = teddy_sparks;
namespace smoke = teddy_blackhole_smoke;
namespace sliding = teddy_sliding;
namespace meteor = teddy_meteor;
using EmitterData = std::variant<spark::Emitter, smoke::Emitter, sliding::Emitter, meteor::Emitter>;
struct SlidingParticle { spark::Particle value; };
using ParticleData = std::variant<std::monostate, spark::Particle, smoke::Particle, SlidingParticle, meteor::Particle>;
struct Slot
{
    ParticleData particle;
    const model_t* model{};
    particles::Vec3 angles{};
    float nextPvsCheck{};
    bool inPvs{};
};
particles::TemporaryPool<EmitterData, const model_t*> emitters;
particles::OrdinaryPool<Slot> particlePool;
particles::Frustum frustum;
std::array<const model_t*,3> burnSprites{};
bool burnPrepared{};
const int* parseCount{};
static_assert(offsetof(cl_entity_t, curstate) == 0x2b0);
static_assert(offsetof(entity_state_t, messagenum) == 0x0c);
static_assert(offsetof(cl_entity_t, origin) == 0xb48);
static_assert(sizeof(cl_entity_t) == 0xbb8);
using Record = decltype(emitters)::Record;
PreciseTrigonometry preciseMath;
struct NativeCameraFields
{
    const int* observer{};
    const float* render{};
    const float* chase{};
    bool Initialize()
    {
        const auto resolve = [](const char* name, void** result)
        {
            return api->ResolveGameSymbol(api->GetClientBase(), name,
                MH_GAMESYMBOL_KIND_GLOBAL, result) == MH_GAMESYMBOL_OK && *result;
        };
        void *state{}, *renderAngles{}, *chaseAngles{};
        if (!resolve("GoldCraftParticle_Observer", &state) ||
            !resolve("GoldCraftParticle_RenderAngles", &renderAngles) ||
            !resolve("GoldCraftParticle_ChaseAngles", &chaseAngles)) return false;
        observer = static_cast<const int*>(state);
        render = static_cast<const float*>(renderAngles);
        chase = static_cast<const float*>(chaseAngles);
        return true;
    }
} cameraFields;
particles::Vec3 clientDataAngles{};
pfnUserMsgHook previous{}, previousSmoke{};
float hudTime{};
bool busy{};
std::uint64_t epoch{}, received{}, emitted{}, invalid{}, exhausted{}, draws{}, smokeMessages{}, smokeEmitted{};
std::uint64_t slidingEmitted{}, ownerMismatches{};
std::uint64_t meteorEmitted{}, emitterStops{};
struct Operation { Operation() { busy = true; } ~Operation() { busy = false; } };
struct Cancelled {};
void CheckGeneration(std::uint64_t generation)
{
    if (generation != epoch) throw Cancelled{};
}
struct Random
{
    std::uint64_t generation;
    float operator()(float low, float high) const
    {
        CheckGeneration(generation);
        const float value = engine.pfnRandomFloat(low, high);
        CheckGeneration(generation);
        return value;
    }
};
struct IntegerRandom
{
    std::uint64_t generation;
    int operator()(int low,int high) const
    {
        CheckGeneration(generation);
        const int value=engine.pfnRandomLong(low,high);
        CheckGeneration(generation);
        return value;
    }
};
void PrepareBurnSprites(std::uint64_t generation)
{
    if(burnPrepared)return;
    std::array<const model_t*,3> loaded{};
    for(unsigned i=0;i<loaded.size();++i)
    {
        const auto handle=engine.pfnSPR_Load(meteor::Sprites[i].data());
        CheckGeneration(generation);
        const auto* model=engine.GetSpritePointer(handle);
        CheckGeneration(generation);
        if(model && model->type==mod_sprite && model->numframes==1)loaded[i]=model;
    }
    burnSprites=loaded;
    burnPrepared=true;
}
std::uint16_t EmitterOwner(const EmitterData& payload)
{
    if(const auto* event=std::get_if<sliding::Emitter>(&payload))return event->owner;
    if(const auto* event=std::get_if<meteor::Emitter>(&payload))return event->owner;
    return 0;
}
const spark::Particle* SparkValue(const Slot& slot)
{
    if (const auto* p = std::get_if<spark::Particle>(&slot.particle)) return p;
    if (const auto* p = std::get_if<meteor::Particle>(&slot.particle)) return &p->value;
    const auto* p = std::get_if<SlidingParticle>(&slot.particle);
    return p ? &p->value : nullptr;
}
const particles::Vec3& Position(const Slot& slot)
{
    if (const auto* p = SparkValue(slot)) return p->origin;
    return std::get<smoke::Particle>(slot.particle).origin;
}
float Size(const Slot& slot)
{
    if (const auto* p = SparkValue(slot)) return p->size;
    return std::get<smoke::Particle>(slot.particle).size;
}
struct AddHost
{
    std::uint64_t generation;
    bool isSmoke{};
    bool isSliding{};
    bool isMeteor{};
    int LoadSprite(const Record& record)
    {
        isSmoke = std::holds_alternative<smoke::Emitter>(record.payload);
        isSliding = std::holds_alternative<sliding::Emitter>(record.payload);
        isMeteor = std::holds_alternative<meteor::Emitter>(record.payload);
        // Original companions are bundled and prepared at VidInit. Prepare at
        // accepted Add in this FastDL host, after assets can have arrived. No
        // frame/draw loading or missing-model substitution is introduced.
        if(isMeteor)PrepareBurnSprites(generation);
        const auto path = isSmoke ? smoke::Sprite.data() :
            isSliding ? sliding::Sprite.data() :
            isMeteor ? meteor::Sprites[0].data() :
            spark::Sprites[std::get<spark::Emitter>(record.payload).sprite].data();
        return engine.pfnSPR_Load(path);
    }
    const model_t* GetModel(int handle) const
    {
        const auto* model = engine.GetSpritePointer(handle);
        if (generation != epoch || !model || model->type != mod_sprite || model->numframes < 1 ||
            model->numframes > static_cast<int>(spark::FrameCapacity)) return nullptr;
        // The fixed smoke consumer was proved against its twelve-frame resource.
        return (isSmoke && model->numframes != smoke::Particle::Frames) ||
            (isMeteor && model->numframes != 1) ||
            (isSliding && model->numframes != 15) ? nullptr : model;
    }
    float HudTime() const { return hudTime; }
    void Overflow(const Record&) const { ++exhausted; }
};
particles::EmitterResult AddRecord(const Record& record)
{
    AddHost host{epoch};
    const auto result = emitters.Add(record, host);
    if (result == particles::EmitterResult::Invalidated) throw Cancelled{};
    if (result == particles::EmitterResult::Complete) ++received;
    return result;
}
std::span<const std::uint8_t> Bytes(int size, void* data)
{
    return data && size > 0 && size <= 192 ?
        std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)) :
        std::span<const std::uint8_t>{};
}
int Receive(const char* name, int size, void* data)
{
    const auto bytes = Bytes(size, data);
    if(meteor::ReadStop(bytes))
    {
        if(busy){++invalid;return 1;}
        Operation operation;
        SynchronizeSession();
        emitters.StopTagged(meteor::Group,0,EmitterOwner);
        ++emitterStops;
        return 1;
    }
    const auto burn=meteor::Read(bytes);
    const bool isSliding = bytes.size() > sliding::Sprite.size() && bytes[sliding::Sprite.size()] == 0 &&
        std::string_view(reinterpret_cast<const char*>(bytes.data()), sliding::Sprite.size()) == sliding::Sprite;
    if (!spark::Identify(bytes) && !isSliding && !burn) return previous ? previous(name, size, data) : 1;
    const auto event = spark::Read(bytes);
    const auto slide = isSliding ? sliding::Read(bytes) : std::optional<sliding::Emitter>{};
    if ((!event && !slide && !burn) || busy || !engine.pfnSPR_Load || !engine.GetSpritePointer ||
        ((isSliding || burn) && (!parseCount || !engine.GetEntityByIndex)) ||
        (burn && (!engine.pfnRandomLong || !engine.pfnRandomFloat))) { ++invalid; return 1; }
    Operation operation;
    SynchronizeSession();
    Record record;
    record.total = burn ? -1 : 3;
    record.burst = 1;
    record.interval = burn ? .01f : isSliding ? .1f : event->interval;
    if (burn) {record.payload=*burn;record.tag=meteor::Group;}
    else if (slide) record.payload = *slide;
    else record.payload = *event;
    try { AddRecord(record); }
    catch (const Cancelled&) {}
    return 1;
}
bool IdentifySmoke(std::span<const std::uint8_t> bytes)
{
    return bytes.size() > smoke::Sprite.size() + 1 && bytes[0] == 88 &&
        bytes[smoke::Sprite.size() + 1] == 0 &&
        std::string_view(reinterpret_cast<const char*>(bytes.data() + 1), smoke::Sprite.size()) == smoke::Sprite;
}
int ReceiveSmoke(const char* name, int size, void* data)
{
    const auto bytes = Bytes(size, data);
    if (!IdentifySmoke(bytes)) return previousSmoke ? previousSmoke(name, size, data) : 1;
    const auto ring = smoke::Read(bytes);
    if (!ring || busy || !engine.pfnSPR_Load || !engine.GetSpritePointer || !engine.pfnRandomFloat ||
        !engine.pEventAPI || !engine.pEventAPI->EV_SetTraceHull || !engine.pEventAPI->EV_PlayerTrace ||
        !preciseMath.Module()) { ++invalid; return 1; }
    Operation operation;
    SynchronizeSession();
    Random random{epoch};
    auto add = [](const smoke::Emitter& event)
    {
        Record record;
        record.total = event.remaining;
        record.burst = 1;
        record.interval = .09f;
        record.payload = event;
        AddRecord(record); // Overflow retains the original remaining ring RNG.
    };
    // Original parser's sourceNext is zero. Add reads the HUD cache separately
    // after each sprite/model callback, including when it changes during a ring.
    try { smoke::Prepare(*ring, 0.f, preciseMath, random, add); ++smokeMessages; }
    catch (const Cancelled&) {}
    return 1;
}
struct EmitHost
{
    std::uint64_t generation;
    particles::Vec3 ownerOrigin{};
    float Time() const { return static_cast<float>(engine.GetClientTime()); }
    void BadSprite(const Record&) const { ++invalid; }
    particles::EmitterPrepare Prepare(Record& record, float)
    {
        const auto ownerIndex=EmitterOwner(record.payload);
        if (ownerIndex)
        {
            const auto* owner = engine.GetEntityByIndex(ownerIndex);
            CheckGeneration(generation);
            // Original dereferences a null owner. Stop this host frame safely;
            // do not manufacture a stamp or claim an original graceful skip.
            if (!owner) { ++invalid; return particles::EmitterPrepare::StopFrame; }
            if (owner->curstate.messagenum != *parseCount)
            {
                record.total = record.remaining = 0;
                ++ownerMismatches;
                return particles::EmitterPrepare::NextRecord;
            }
            const auto* burn=std::get_if<meteor::Emitter>(&record.payload);
            const auto* origin=burn?owner->attachment[burn->attachment]:owner->origin;
            ownerOrigin = {origin[0],origin[1],origin[2]};
        }
        return particles::EmitterPrepare::Emit;
    }
    bool Create(const Record& record, float now) const
    {
        Random random{generation};
        std::optional<meteor::Prepared> prepared;
        const auto* burn=std::get_if<meteor::Emitter>(&record.payload);
        if(burn)
        {
            IntegerRandom integer{generation};
            prepared=meteor::Prepare(*burn,ownerOrigin,random,integer);
            // Original factory rejects its selected null model after pre-RNG.
            if(!burnSprites[prepared->sprite]){++invalid;return false;}
        }
        auto* slot = particlePool.Allocate(true);
        if (!slot) { ++exhausted; return false; }
        ParticleData particle;
        float nextPvs = now;
        const model_t* model=record.model;
        if (const auto* event = std::get_if<spark::Emitter>(&record.payload))
            particle = spark::Spawn(*event, now, random);
        else if (const auto* event = std::get_if<sliding::Emitter>(&record.payload))
        {
            nextPvs = Time();
            CheckGeneration(generation);
            const float born = Time();
            CheckGeneration(generation);
            auto value = sliding::Spawn(*event, ownerOrigin, born, random);
            value.die = now + .5f;
            particle = SlidingParticle{value};
        }
        else if(burn)
        {
            nextPvs=Time();CheckGeneration(generation);
            const float born=Time();CheckGeneration(generation);
            particle=meteor::Spawn(*burn,*prepared,now,born,random);
            model=burnSprites[prepared->sprite];
        }
        else
            particle = smoke::Spawn(std::get<smoke::Emitter>(record.payload), now, random);
        CheckGeneration(generation);
        *slot = {particle, model, {0, 0, 1}, nextPvs, true};
        ++emitted;
        smokeEmitted += std::holds_alternative<smoke::Particle>(particle);
        slidingEmitted += std::holds_alternative<SlidingParticle>(particle);
        meteorEmitted += std::holds_alternative<meteor::Particle>(particle);
        return true;
    }
};
struct Camera
{
    std::uint64_t generation;
    particles::Vec3 operator()() const
    {
        CheckGeneration(generation);
        const int mode = cameraFields.observer[0];
        // Original native-gameplay/observer branches of02AD39F0. The VXL
        // cutscene and mode53 camera providers have no native host binding yet.
        if (mode && cameraFields.observer[2] > 0 && cameraFields.observer[1] > 0) return {};
        if (mode == 1 || mode == 4)
            return {cameraFields.render[0], cameraFields.render[1], cameraFields.render[2]};
        if (mode && mode != 2 && mode != 3)
            return {cameraFields.chase[0], cameraFields.chase[1], cameraFields.chase[2]};
        particles::Vec3 angles;
        engine.GetViewAngles(angles.data());
        CheckGeneration(generation);
        return angles;
    }
};
struct AngleVectors
{
    std::uint64_t generation;
    void operator()(const particles::Vec3& angles) const
    {
        particles::Vec3 forward, right, up;
        engine.pfnAngleVectors(angles.data(), forward.data(), right.data(), up.data());
        CheckGeneration(generation);
    }
};
struct World
{
    std::uint64_t generation;
    smoke::Trace operator()(int hull, const smoke::Vec3& start, const smoke::Vec3& end, int flags, int ignore) const
    {
        auto* events = engine.pEventAPI;
        if (!events || !events->EV_SetTraceHull || !events->EV_PlayerTrace) throw Cancelled{};
        auto a = start, b = end; // Legacy API takes mutable pointers to the query.
        pmtrace_t trace{};
        static_assert(sizeof(pmtrace_t) == 68);
        events->EV_SetTraceHull(hull);
        CheckGeneration(generation);
        events->EV_PlayerTrace(a.data(), b.data(), flags, ignore, &trace);
        CheckGeneration(generation);
        return {trace.fraction, {trace.endpos[0], trace.endpos[1], trace.endpos[2]},
            {trace.plane.normal[0], trace.plane.normal[1], trace.plane.normal[2]}, trace.ent};
    }
};
struct ParticleHost
{
    std::uint64_t generation;
    float Time() const { return static_cast<float>(engine.GetClientTime()); }
    bool Visible(Slot& slot) const
    {
        const float extent = Size(slot) / 5.f;
        const float now = Time();
        CheckGeneration(generation);
        auto* tri = engine.pTriAPI;
        if (now >= slot.nextPvsCheck && tri->BoxInPVS)
        {
            const auto& origin = csol::client::Position(slot);
            float mins[3], maxs[3];
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                mins[axis] = origin[axis] - extent;
                maxs[axis] = origin[axis] + extent;
            }
            const bool visible = tri->BoxInPVS(mins, maxs) != 0;
            CheckGeneration(generation);
            slot.inPvs = visible;
            const float resampled = Time();
            CheckGeneration(generation);
            slot.nextPvsCheck = static_cast<float>(static_cast<double>(resampled) + static_cast<double>(.1f));
        }
        return particles::FixedBoundsVisible(csol::client::Position(slot), extent, frustum, slot.inPvs);
    }
    particles::Vec3 SortOrigin() const
    {
        const auto* local = engine.GetLocalPlayer();
        CheckGeneration(generation);
        if (!local) throw Cancelled{}; // Invalid host pointer; never manufacture an origin.
        return {local->origin[0], local->origin[1], local->origin[2]};
    }
    particles::Vec3 Position(const Slot& slot) const { return csol::client::Position(slot); }
    void Update(Slot& slot, float snapshot) const
    {
        Camera camera{generation};
        AngleVectors angleVectors{generation};
        if (auto* p = std::get_if<spark::Particle>(&slot.particle))
        {
            spark::Tick(*p, snapshot, particlePool.PreviousTime(), particlePool.Gravity(),
                p->sprite ? 17 : 16, slot.model->numframes);
            const particles::Vec3 view{camera()[0], camera()[1], camera()[2]};
            angleVectors(view);
            slot.angles = {view[0], view[1], p->roll};
        }
        else if (auto* p = std::get_if<SlidingParticle>(&slot.particle))
        {
            sliding::Tick(p->value, snapshot, particlePool.PreviousTime(), particlePool.Gravity(),
                30, slot.model->numframes);
            const particles::Vec3 view{camera()[0], camera()[1], camera()[2]};
            angleVectors(view);
            slot.angles = {view[0], view[1], p->value.roll};
        }
        else if (auto* p = std::get_if<meteor::Particle>(&slot.particle))
        {
            meteor::Tick(*p,snapshot,particlePool.PreviousTime(),particlePool.Gravity(),1);
            const particles::Vec3 view{camera()[0],camera()[1],camera()[2]};
            angleVectors(view);
            slot.angles={view[0],view[1],p->value.roll};
        }
        else
        {
            World world{generation};
            smoke::Tick(std::get<smoke::Particle>(slot.particle), snapshot, particlePool.PreviousTime(),
                particlePool.Gravity(), world, camera, angleVectors);
        }
    }
    float Die(const Slot& slot) const
    {
        if (const auto* p = SparkValue(slot)) return p->die;
        return std::get<smoke::Particle>(slot.particle).die;
    }
    // Both fixed consumers have extraFlags0:02AD36D0 ->38FC ->01DE2F00 ret0.
    void Retire(Slot&) const {}
    void Draw(const Slot& slot) const
    {
        const float now = Time();
        CheckGeneration(generation);
        if (Die(slot) == now) return;
        auto* tri = engine.pTriAPI;
        const auto* sparkParticle = SparkValue(slot);
        const auto angles = sparkParticle ? slot.angles : std::get<smoke::Particle>(slot.particle).angles;
        particles::Vec3 forward, right, up;
        engine.pfnAngleVectors(angles.data(), forward.data(), right.data(), up.data());
        CheckGeneration(generation);
        spark::Quad corners;
        std::array<float, 4> color;
        int frame, mode;
        if (sparkParticle)
        {
            corners = spark::Corners(*sparkParticle, right, up);
            color = {sparkParticle->color[0] / 255.f, sparkParticle->color[1] / 255.f,
                sparkParticle->color[2] / 255.f, sparkParticle->alpha / 255.f};
            frame = sparkParticle->frame;
            mode = kRenderTransAdd;
        }
        else
        {
            const auto prepared = smoke::PrepareDraw(std::get<smoke::Particle>(slot.particle), now, right, up);
            corners = prepared->corners;
            color = prepared->color;
            frame = prepared->frame;
            mode = prepared->Mode;
        }
        // SDK lookup is const; this legacy binding ABI predates that qualifier.
        const bool bound = tri->SpriteTexture(const_cast<model_t*>(slot.model), frame) != 0;
        CheckGeneration(generation);
        if (!bound) return;
        struct Restore
        {
            decltype(tri) api;
            bool begun{};
            ~Restore()
            {
                if (begun) api->End();
                api->Color4f(1, 1, 1, 1);
                api->RenderMode(kRenderNormal);
                api->CullFace(TRI_FRONT);
            }
        } restore{tri};
        tri->RenderMode(mode);
        CheckGeneration(generation);
        tri->CullFace(TRI_NONE);
        CheckGeneration(generation);
        restore.begun = true;
        tri->Begin(TRI_QUADS);
        CheckGeneration(generation);
        tri->Color4f(color[0], color[1], color[2], color[3]);
        CheckGeneration(generation);
        constexpr float uv[4][2]{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        for (unsigned corner = 0; corner < 4; ++corner)
        {
            tri->TexCoord2f(uv[corner][0], uv[corner][1]);
            CheckGeneration(generation);
            tri->Vertex3f(corners[corner][0], corners[corner][1], corners[corner][2]);
            CheckGeneration(generation);
        }
        ++draws;
    }
};
}

void InitializeParticleEffects()
{
    void* count{};
    const bool ownerReady = api->ResolveGameSymbol(api->GetEngineBase(), "cl_parsecount",
        MH_GAMESYMBOL_KIND_GLOBAL, &count) == MH_GAMESYMBOL_OK && count;
    if (!preciseMath.Initialize() || !cameraFields.Initialize() || !ownerReady)
    {
        api->SysError("CSOLSupport: matched particle camera symbols or precise math exports are unavailable");
        std::abort();
    }
    parseCount = static_cast<const int*>(count);
    particlePool.Prepare();
    emitters.Initialize();
    InstallChainedUserMessage(spark::Message, Receive, previous);
    InstallChainedUserMessage("MPToCL", ReceiveSmoke, previousSmoke);
}
void ResetParticleEffects()
{
    // Retain existing ClearScene/HudShutdown host cleanup. This does not
    // establish a new original disconnect or ResetHUD clearing policy.
    ++epoch;
    particlePool.Clear();
    emitters.ResetVideoRecords();
    burnSprites={};burnPrepared=false;
}
void ShutdownParticleEffects()
{
    ResetParticleEffects();
    preciseMath.Shutdown(); // Explicit host plugin ownership, not map cleanup.
    cameraFields = {};
    parseCount = nullptr;
}
void CacheParticleHudTime(float time) { hudTime = time; }
void CacheParticleClientData(const client_data_s& data)
{
    clientDataAngles = {data.viewangles[0], data.viewangles[1], data.viewangles[2]};
}
void UpdateParticleEmitters()
{
    if (busy || !engine.GetClientTime || !engine.pfnRandomFloat) return;
    Operation operation;
    EmitHost host{epoch};
    try { emitters.Frame(host); }
    catch (const Cancelled&) {}
}
void DrawParticleEffects()
{
    if (busy || !engine.GetClientTime || !engine.GetViewAngles || !engine.pfnAngleVectors ||
        !engine.GetLocalPlayer || !view.movevars || !cameraFields.observer) return;
    auto* tri = engine.pTriAPI;
    if (!tri || !tri->SpriteTexture || !tri->RenderMode || !tri->CullFace ||
        !tri->Begin || !tri->End || !tri->Color4f || !tri->TexCoord2f || !tri->Vertex3f || !tri->GetMatrix) return;
    Operation operation;
    const auto generation = epoch;
    auto clock = [] { return static_cast<float>(engine.GetClientTime()); };
    const int mode = cameraFields.observer[0];
    const auto angles = mode == 0 || mode == 3 ? clientDataAngles :
        particles::Vec3{cameraFields.render[0], cameraFields.render[1], cameraFields.render[2]};
    if (!particlePool.SetEnvironment(view.movevars->gravity, angles, clock) || generation != epoch) return;
    // Original outer render stage prepares six planes once for this view,
    // before traversing particles. The SDK owns the actual matrix read ABI.
    particles::Matrix projection, model;
    tri->GetMatrix(0x0ba7, projection.data());
    if (generation != epoch) return;
    tri->GetMatrix(0x0ba6, model.data());
    if (generation != epoch) return;
    frustum = particles::GenerateFrustum(projection, model);
    ParticleHost host{generation};
    try { particlePool.Frame(host); }
    catch (const Cancelled&) {}
}
void ParticleEffectsDiagnostics(std::ostream& out)
{
    std::size_t pending{};
    for (const auto& record : emitters.Records())
        pending += record.remaining > 0 || record.total == -1;
    out << "particles received=" << received << " emitted=" << emitted << " active=" << particlePool.ActiveCount()
        << " pending=" << pending << " invalid=" << invalid << " exhausted=" << exhausted << " draws=" << draws
        << " blackholeMessages=" << smokeMessages << " blackholeEmitted=" << smokeEmitted << '\n';
    out << "particles slidingEmitted=" << slidingEmitted << " ownerMismatches=" << ownerMismatches << '\n';
    out << "particles meteorEmitted=" << meteorEmitted << " stops=" << emitterStops << '\n';
}
}
