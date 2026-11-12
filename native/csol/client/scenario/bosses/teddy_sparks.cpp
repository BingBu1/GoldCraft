// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../runtime.hpp"
#include "../../user_message_chain.hpp"
#include "teddy_sparks.hpp"
#include "../../../scenario/bosses/teddy_spark_particle.hpp"
#include "../../../scenario/core/emitter_runtime.hpp"
#include "../../../scenario/core/particle_runtime.hpp"
#include <pm_movevars.h>
#include <triangleapi.h>
#include <ostream>

#pragma clang fp contract(off)

namespace csol::client
{
namespace
{
namespace spark = teddy_sparks;
struct Slot
{
    spark::Particle particle;
    const model_t* model{};
    float nextPvsCheck{};
    bool inPvs{true};
};
// One emitter owner and an independent ordinary particle owner for this
// supported TempEmit subset. MPToCL88/model producers are not registered here.
particles::TemporaryPool<spark::Emitter, const model_t*> emitters;
particles::OrdinaryPool<Slot> particlePool;
using Record = decltype(emitters)::Record;
pfnUserMsgHook previous{};
float hudTime{};
bool busy{};
std::uint64_t epoch{}, received{}, emitted{}, invalid{}, exhausted{}, draws{};
struct Operation { Operation() { busy = true; } ~Operation() { busy = false; } };
struct Random
{
    std::uint64_t generation;
    float operator()(float low, float high) const
    { return generation == epoch ? engine.pfnRandomFloat(low, high) : 0.f; }
};
struct AddHost
{
    std::uint64_t generation;
    int LoadSprite(const Record& record) const
    { return engine.pfnSPR_Load(spark::Sprites[record.payload.sprite].data()); }
    const model_t* GetModel(int handle) const
    {
        auto* model = engine.GetSpritePointer(handle);
        // Host resource-safety boundary, not an original sprite frame limit.
        return generation == epoch && model && model->type == mod_sprite && model->numframes > 0 &&
            model->numframes <= static_cast<int>(spark::FrameCapacity) ? model : nullptr;
    }
    float HudTime() const { return hudTime; }
    void Overflow(const Record&) const { ++exhausted; }
};
int Receive(const char* name, int size, void* data)
{
    const auto bytes = data && size > 0 && size <= 192 ?
        std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), static_cast<std::size_t>(size)) :
        std::span<const std::uint8_t>{};
    if (!spark::Identify(bytes)) return previous ? previous(name, size, data) : 1;
    const auto event = spark::Read(bytes);
    if (!event || busy || !engine.pfnSPR_Load || !engine.GetSpritePointer) { ++invalid; return 1; }
    Operation operation;
    SynchronizeSession();
    Record record;
    record.total = 3;
    record.burst = 1;
    record.interval = event->interval;
    record.payload = *event;
    AddHost host{epoch};
    if (emitters.Add(record, host) == particles::EmitterResult::Complete) ++received;
    return 1;
}
struct EmitHost
{
    std::uint64_t generation;
    float Time() const { return static_cast<float>(engine.GetClientTime()); }
    void BadSprite(const Record&) const { ++invalid; }
    particles::EmitterPrepare Prepare(Record&, float) const
    { return particles::EmitterPrepare::Emit; } // Supported packets are ownerless.
    bool Create(const Record& record, float now) const
    {
        auto* slot = particlePool.Allocate(true);
        if (!slot) { ++exhausted; return false; }
        Random random{generation};
        const auto particle = spark::Spawn(record.payload, now, random);
        if (generation != epoch) return false;
        *slot = {particle, record.model, now, true};
        ++emitted;
        return true;
    }
};
struct ParticleHost
{
    std::uint64_t generation;
    float Time() const { return static_cast<float>(engine.GetClientTime()); }
    bool Visible(Slot& slot) const
    {
        const float now = Time();
        if (generation != epoch) return false;
        auto* tri = engine.pTriAPI;
        if (now >= slot.nextPvsCheck && tri->BoxInPVS)
        {
            const float extent = slot.particle.size / 5.f;
            float mins[3], maxs[3];
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                mins[axis] = slot.particle.origin[axis] - extent;
                maxs[axis] = slot.particle.origin[axis] + extent;
            }
            const bool visible = tri->BoxInPVS(mins, maxs) != 0;
            if (generation != epoch) return false;
            slot.inPvs = visible;
            const float resampled = Time();
            if (generation != epoch) return false;
            slot.nextPvsCheck = static_cast<float>(static_cast<double>(resampled) + static_cast<double>(.1f));
        }
        return slot.inPvs;
    }
    particles::Vec3 SortOrigin() const
    {
        const auto* local = engine.GetLocalPlayer();
        if (generation != epoch || !local) return {};
        return {local->origin[0], local->origin[1], local->origin[2]};
    }
    particles::Vec3 Position(const Slot& slot) const { return slot.particle.origin; }
    void Update(Slot& slot, float snapshot) const
    {
        spark::Tick(slot.particle, snapshot, particlePool.PreviousTime(), particlePool.Gravity(),
            slot.particle.sprite ? 17 : 16, slot.model->numframes);
    }
    float Die(const Slot& slot) const { return slot.particle.die; }
    // Fixed field150==0:02AD36D0 ->02AD38FC ->01DE2F00 ret0.
    void Retire(Slot&) const {}
    void Draw(const Slot& slot) const
    {
        const float now = Time();
        if (generation != epoch || slot.particle.die == now) return;
        auto* tri = engine.pTriAPI;
        const auto particle = slot.particle;
        const auto camera = particlePool.Camera();
        float angles[]{camera[0], camera[1], particle.roll};
        spark::Vec3 forward, right, up;
        engine.pfnAngleVectors(angles, forward.data(), right.data(), up.data());
        if (generation != epoch) return;
        const auto corners = spark::Corners(particle, right, up);
        // SDK SPR lookup is const; the legacy bind ABI predates that qualifier.
        const bool bound = tri->SpriteTexture(const_cast<model_t*>(slot.model), particle.frame) != 0;
        if (generation != epoch || !bound) return;
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
        tri->RenderMode(kRenderTransAdd);
        if (generation != epoch) return;
        tri->CullFace(TRI_NONE);
        if (generation != epoch) return;
        restore.begun = true;
        tri->Begin(TRI_QUADS);
        if (generation != epoch) return;
        tri->Color4f(particle.color[0] / 255.f, particle.color[1] / 255.f,
            particle.color[2] / 255.f, particle.alpha / 255.f);
        if (generation != epoch) return;
        constexpr float uv[4][2]{{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        for (unsigned corner = 0; corner < 4; ++corner)
        {
            tri->TexCoord2f(uv[corner][0], uv[corner][1]);
            if (generation != epoch) return;
            tri->Vertex3f(corners[corner][0], corners[corner][1], corners[corner][2]);
            if (generation != epoch) return;
        }
        ++draws;
    }
};
}

void InitializeTeddySparks()
{
    particlePool.Prepare();
    emitters.Initialize();
    InstallChainedUserMessage(spark::Message, Receive, previous);
}
void ResetTeddySparks()
{
    // Retain existing ClearScene/HudShutdown host cleanup. This does not
    // establish a new original disconnect or ResetHUD clearing policy.
    ++epoch;
    particlePool.Clear();
    emitters.ResetVideoRecords();
}
void TeddySparksHudTime(float time) { hudTime = time; }
void TeddySparksUpdateClientData()
{
    if (busy || !engine.GetClientTime || !engine.pfnRandomFloat) return;
    Operation operation;
    EmitHost host{epoch};
    emitters.Frame(host);
}
void DrawTeddySparks()
{
    if (busy || !engine.GetClientTime || !engine.pfnAngleVectors || !engine.GetLocalPlayer || !view.movevars) return;
    auto* tri = engine.pTriAPI;
    if (!tri || !tri->SpriteTexture || !tri->RenderMode || !tri->CullFace ||
        !tri->Begin || !tri->End || !tri->Color4f || !tri->TexCoord2f || !tri->Vertex3f) return;
    Operation operation;
    const auto generation = epoch;
    auto clock = [] { return static_cast<float>(engine.GetClientTime()); };
    if (!particlePool.SetEnvironment(view.movevars->gravity,
        {view.viewangles[0], view.viewangles[1], view.viewangles[2]}, clock) || generation != epoch) return;
    ParticleHost host{generation};
    particlePool.Frame(host);
}
void TeddySparksDiagnostics(std::ostream& out)
{
    std::size_t pending{};
    for (const auto& record : emitters.Records())
        pending += record.remaining > 0 || record.total == -1;
    out << "teddySparks received=" << received << " emitted=" << emitted << " active=" << particlePool.ActiveCount()
        << " pending=" << pending << " invalid=" << invalid << " exhausted=" << exhausted << " draws=" << draws << '\n';
}
}
