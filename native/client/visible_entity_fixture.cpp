// Opt-in local render fixture. These are distinct client entities submitted
// through the real engine API, never server edicts or simulated gameplay input.
#include <metahook.h>
#include <com_model.h>
#include <entity_types.h>
#include "visible_entities.hpp"
#include "goldcraft/visible_entities_api.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <ostream>
#include <vector>

extern cl_enginefunc_t gEngfuncs;

namespace goldcraft::visible_entities {
namespace {
std::vector<cl_entity_t> fixture;
std::vector<int> slots;
std::vector<bool> drawn;
double until = 0;
unsigned serial = 0, frames = 0, accepted = 0, rejected = 0, unique_draws = 0, max_draws = 0;
int target = 0, before = 0, after = 0;
bool slot513 = false, slot4096 = false;
model_t* model = nullptr;

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void fixture_clear() { until = 0; model = nullptr; }

void fixture_start(int target_count, double seconds) {
    fixture_clear();
    if (!std::getenv("GOLDCRAFT_TEST_COMMAND") || target_count < 1 || target_count > capacity + 1 ||
        seconds <= 0 || seconds > 10) return;
    const auto api = GoldCraftVisibleEntitiesInterface();
    if (!api) return;
    // CL_LoadModel only finds server-precached models. Load this existing local
    // diagnostic sprite through the public sprite API instead.
    const int sprite = gEngfuncs.pfnSPR_Load("sprites/dot.spr");
    model = sprite ? const_cast<model_t*>(gEngfuncs.GetSpritePointer(sprite)) : nullptr;
    if (!model || model->type != mod_sprite) { model = nullptr; return; }
    fixture.assign(capacity + 1, {});
    slots.assign(capacity + 1, 0);
    drawn.assign(capacity + 1, false);
    target = target_count;
    frames = accepted = rejected = unique_draws = max_draws = 0;
    slot513 = slot4096 = false;
    before = after = 0;
    ++serial;
    until = now() + seconds;
}

void create_entities(const float* origin, const float* angles) {
    if (!model) return;
    if (now() >= until) { fixture_clear(); return; }
    const auto api = GoldCraftVisibleEntitiesInterface();
    if (!api || *api->count < 0 || *api->count > capacity) return;
    float forward[3], right[3], up[3];
    gEngfuncs.pfnAngleVectors(angles, forward, right, up);
    before = *api->count;
    accepted = rejected = unique_draws = 0;
    std::fill(drawn.begin(), drawn.end(), false);
    std::fill(slots.begin(), slots.end(), 0);
    const int amount = std::max(0, target - before);
    for (int i = 0; i < amount; ++i) {
        auto& entity = fixture[i];
        entity = {};
        entity.model = model;
        entity.curstate.rendermode = kRenderTransColor;
        entity.curstate.renderamt = 255;
        entity.curstate.rendercolor = {40, 160, 255};
        entity.curstate.scale = .04f;
        entity.curstate.effects = EF_NOINTERP;
        const int slot = *api->count + 1;
        if (slot == 513 || slot == 4096) {
            entity.curstate.rendercolor = slot == 513 ? color24{255, 40, 40} : color24{40, 255, 40};
            entity.curstate.scale = .12f;
        }
        const float x = (float(i % 64) - 31.5f) * 1.5f;
        const float y = (float(i / 64) - 31.5f) * 1.0f;
        for (int axis = 0; axis < 3; ++axis)
            entity.curstate.origin[axis] = entity.origin[axis] = origin[axis] +
                forward[axis] * 96 + right[axis] * x + up[axis] * y;
        if (gEngfuncs.CL_CreateVisibleEntity(ET_NORMAL, &entity)) {
            slots[i] = slot;
            ++accepted;
        } else ++rejected;
    }
    after = *api->count;
    ++frames;
}

void sprite_drawn(const cl_entity_t* entity) {
    if (!model || now() >= until || fixture.empty()) return;
    const auto address = reinterpret_cast<uintptr_t>(entity);
    const auto base = reinterpret_cast<uintptr_t>(fixture.data());
    if (address < base || address >= base + fixture.size() * sizeof(cl_entity_t) ||
        (address - base) % sizeof(cl_entity_t)) return;
    const auto index = (address - base) / sizeof(cl_entity_t);
    if (!slots[index] || drawn[index]) return;
    drawn[index] = true;
    ++unique_draws;
    max_draws = std::max(max_draws, unique_draws);
    slot513 |= slots[index] == 513;
    slot4096 |= slots[index] == 4096;
}

void fixture_write_status(std::ostream& out) {
    out << ",\"visibleFixture\":{\"serial\":" << serial << ",\"active\":" << (model && now() < until)
        << ",\"target\":" << target << ",\"frames\":" << frames << ",\"before\":" << before
        << ",\"after\":" << after << ",\"accepted\":" << accepted << ",\"rejected\":" << rejected
        << ",\"uniqueDraws\":" << unique_draws << ",\"maxUniqueDraws\":" << max_draws
        << ",\"slot513Drawn\":" << slot513 << ",\"slot4096Drawn\":" << slot4096 << '}';
}
}
