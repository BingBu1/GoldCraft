// Included by the Renderer GL fixture. Both consumers and EditTransaction are
// production translation units. Engine PM fallback is an analytical box adapter;
// this does not replace actual engine callback/network/gameplay acceptance.
#include <pm_defs.h>
#include <sstream>
#include "map_edit_transaction.hpp"

namespace joint_edit_test {
using namespace goldcraft;
std::unique_ptr<playermove_t> movement;
float rayStart[3]{}, rayEnd[3]{};
pmtrace_t rayResult{};

pmtrace_t nativeTrace(float* from, float* to, int flags, int ignored, int (*filter)(physent_t*)) {
    assert(flags == PM_NORMAL && from[0] == to[0] && from[1] == to[1] && from[2] > to[2]);
    pmtrace_t result{};
    result.fraction = 1; result.ent = -1;
    std::copy_n(to, 3, result.endpos);
    for (int i = 0; i < movement->numphysent; ++i) {
        auto& pe = movement->physents[i];
        if (i == ignored || (filter && filter(&pe))) continue;
        const auto* model = pe.model;
        bool outside = false;
        for (int axis = 0; axis < 2; ++axis)
            outside |= from[axis] < pe.origin[axis] + model->mins[axis] ||
                       from[axis] > pe.origin[axis] + model->maxs[axis];
        const float top = pe.origin[2] + model->maxs[2];
        if (outside || from[2] < top || to[2] >= top) continue;
        const float fraction = (from[2] - top) / (from[2] - to[2]);
        if (fraction < result.fraction) {
            result.fraction = fraction; result.ent = i;
            result.plane.normal[2] = 1; result.plane.dist = top;
        }
    }
    result.endpos[2] = from[2] + result.fraction * (to[2] - from[2]);
    return result;
}
pmtrace_t nativeSimple(float* a, float* b, int flags, int ignored) { return nativeTrace(a, b, flags, ignored, nullptr); }
pmtrace_t nativeFiltered(float* a, float* b, int flags, int (*filter)(physent_t*)) { return nativeTrace(a, b, flags, -1, filter); }
void queryCallback(playermove_t* pm, int) { rayResult = pm->PM_PlayerTrace(rayStart, rayEnd, PM_NORMAL, -1); }
bool ray(float x, float z) {
    rayStart[0] = rayEnd[0] = x;
    rayStart[1] = rayEnd[1] = 48;
    rayStart[2] = z + 16; rayEnd[2] = z - 4;
    client_map::move(movement.get(), 0, queryCallback);
    assert(movement->PM_PlayerTrace == nativeSimple && movement->PM_PlayerTraceEx == nativeFiltered);
    return rayResult.fraction == 1;
}
void frame(client_map::EditTransaction& transaction, IMetaRendererWorldEdit2* renderer,
           model_t* world, std::uint64_t epoch, std::uint64_t revision, std::uint32_t sequence) {
    brush::Frame value{epoch, revision, sequence, 2};
    value.entries[0] = {10, 123, 1}; value.entries[1] = {11, 456, 1};
    std::array<std::uint8_t, 184> encoded{};
    const auto size = brush::encode(value, 0, encoded);
    assert(client_map::brush_identities().accept(std::span(encoded).first(size)));
    fixtureEntities[10]->curstate.messagenum = fixtureEntities[11]->curstate.messagenum = int(sequence);
    transaction.publish(renderer, world, renderer);
}
void draw(const std::shared_ptr<CWorldSurfaceModel>& model, float xOffset) {
    assert(model && model->GetLeafByIndex(0)->m_bIsReady);
    glViewport(0, 0, 64, 64); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT); glUseProgram(shader);
    float matrix[16]{1,0,0,0, 0,1,0,0, 0,0,1,0, xOffset,0,0,1};
    glUniformMatrix4fv(entityMatrixLocation, 1, GL_FALSE, matrix);
    shadow = false; gbuffer = false; recordStates = false;
    R_DrawWorldSurfaceLeafStatic(model.get(), model->GetLeafByIndex(0).get());
    R_DrawWorldSurfaceLeafAnim(model.get(), model->GetLeafByIndex(0).get());
}
bool pixel(const std::shared_ptr<CWorldSurfaceModel>& model, float offset, int x) {
    draw(model, offset);
    std::array<byte, 4> value{};
    glReadPixels(x, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value.data());
    assert(value[0] > 250 || value[1] > 250);
    return value[0] > 250;
}
void coupled(bool worldCut, bool brushCut, int cutShift = 0) {
    auto selected = R_FindEditedBrushSurfaceModel(fixtureEntities[10]);
    assert(bool(selected) == brushCut);
    assert(!R_FindEditedBrushSurfaceModel(fixtureEntities[11]));
    // Each revision has a different hole. Check both the open hole and the
    // closed locations, so a stale/partial geometry cannot satisfy recovery.
    for (const int shift : {0, 32, 64}) {
        const bool worldOpen = worldCut && shift == cutShift;
        const bool brushOpen = brushCut && shift == cutShift;
        assert(pixel(g_WorldSurfaceModels[1], 0, 24 + shift / 2) == worldOpen);
        assert(pixel(selected ? selected : g_WorldSurfaceModels[2], -128, 24 + shift / 2) == brushOpen);
        assert(!pixel(g_WorldSurfaceModels[2], -128, 24 + shift / 2));
        assert(ray(48.0f + shift, 0) == worldOpen && ray(176.0f + shift, 64) == brushOpen);
        assert(!ray(432.0f + shift, 64)); // Same model, independent sibling incarnation.
    }
    assert(glGetError() == GL_NO_ERROR);
}
void collisionRevision(std::uint64_t expected) {
    std::ostringstream status;
    client_map::write_status(status);
    assert(status.str().find("\"mapCollisionRevision\":" + std::to_string(expected) + ",") != std::string::npos);
}
void finish(client_map::EditTransaction& transaction, IMetaRendererWorldEdit2* renderer, model_t* world) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (transaction.pending()) {
        assert(std::chrono::steady_clock::now() < limit);
        transaction.pump(renderer, world, renderer);
        if (transaction.pending()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void run() {
    auto* renderer = static_cast<IMetaRendererWorldEdit2*>(CreateInterface(METARENDERER_WORLD_EDIT2_INTERFACE_VERSION, nullptr));
    assert(renderer); renderer->Reset();
    entityMatrixLocation = glGetUniformLocation(shader, "entityMatrix");
    assert(entityMatrixLocation >= 0);
    Scene scene(2);
    std::array<mplane_t, 12> planes{};
    std::array<dclipnode_t, 12> clips{};
    for (int box = 0; box < 2; ++box)
        for (int face = 0; face < 6; ++face) {
            const int index = box * 6 + face, axis = face / 2;
            auto& plane = planes[index]; plane.normal[axis] = 1; plane.type = byte(axis);
            const float low = axis == 0 ? box * 128.0f : axis == 1 ? 0.0f : -16.0f;
            const float high = axis == 0 ? (box + 1) * 128.0f : axis == 1 ? 128.0f : 0.0f;
            plane.dist = face % 2 ? high : low;
            clips[index].planenum = index;
            clips[index].children[face % 2] = short(face == 5 ? CONTENTS_SOLID : index + 1);
            clips[index].children[1 - face % 2] = CONTENTS_EMPTY;
        }
    scene.model.planes = planes.data(); scene.model.numplanes = int(planes.size());
    scene.model.numsubmodels = 2; scene.model.nummodelsurfaces = 1;
    scene.model.mins[2] = -16; scene.model.maxs[0] = scene.model.maxs[1] = 128;
    for (auto& hull : scene.model.hulls) hull = {clips.data(), planes.data(), 0, 11};
    worldModel = &scene.model;
    model_t brush = scene.model;
    std::strcpy(brush.name, "*1"); brush.firstmodelsurface = 1;
    brush.mins[0] = 128; brush.maxs[0] = 256;
    for (auto& hull : brush.hulls) hull.firstclipnode = 6;
    candidateInline = &brush;
    gEngfuncs.CL_LoadModel = [](const char* name, int* index) -> model_t* {
        if (std::strcmp(name, "*1")) return nullptr;
        *index = 2; return candidateInline;
    };
    gEngfuncs.hudGetModelByIndex = [](int index) -> model_t* { return index == 2 ? candidateInline : nullptr; };
    gEngfuncs.GetEntityByIndex = [](int index) -> cl_entity_t* { return index >= 0 && index < 64 ? fixtureEntities[index] : nullptr; };
    gEngfuncs.pfnAngleVectors = AngleVectors;
    cl_entity_t first{}, second{};
    for (auto* entity : {&first, &second}) {
        entity->model = &brush; entity->curstate.modelindex = 2; entity->curstate.solid = 4;
        entity->index = entity->curstate.number = entity == &first ? 10 : 11;
        fixtureEntities[entity->index] = entity;
    }
    const auto rebuild = [&] {
        g_WorldSurfaceModels = {nullptr, R_PrepareWorldSurfaceLeafModel(scene.geometry, &scene.model, 0),
                               R_PrepareWorldSurfaceLeafModel(scene.geometry, &brush, 0)};
        Finish(g_WorldSurfaceModels[1]); Finish(g_WorldSurfaceModels[2]);
        g_WorldSurfaceWorldModels = {nullptr, scene.geometry};
    };
    rebuild();
    movement = std::make_unique<playermove_t>(); movement->numphysent = 3; movement->usehull = 2;
    for (int i = 0; i < 3; ++i) {
        auto& pe = movement->physents[i]; pe.model = i ? &brush : &scene.model;
        pe.solid = 4; pe.info = i ? 9 + i : 0; pe.origin[2] = i ? 64.0f : 0.0f;
        pe.origin[0] = i == 2 ? 256.0f : 0.0f;
    }
    movement->PM_PlayerTrace = nativeSimple; movement->PM_PlayerTraceEx = nativeFiltered;
    movement->PM_TestPlayerPosition = [](float*, pmtrace_t*) -> int { assert(false); return 0; };
    movement->PM_TestPlayerPositionEx = [](float*, pmtrace_t*, int (*)(physent_t*)) -> int { assert(false); return 0; };
    movement->PM_PointContents = [](float*, int*) -> int { assert(false); return 0; };
    movement->PM_TruePointContents = [](float*) -> int { assert(false); return 0; };
    movement->PM_TraceLine = [](float*, float*, int, int, int) -> pmtrace_t* { assert(false); return nullptr; };
    movement->PM_TraceLineEx = [](float*, float*, int, int, int (*)(physent_t*)) -> pmtrace_t* { assert(false); return nullptr; };
    movement->PM_HullPointContents = [](hull_t*, int, float*) -> int { assert(false); return 0; };
    client_map::engine_initialized(&gEngfuncs);
    client_map::EditTransaction transaction; transaction.reset(9);
    frame(transaction, renderer, worldModel, 9, 2, 40);
    edits::Snapshot state{9, 2, {{1, {10,123,1}, {{160,32,-8}, {192,64,8}}}}};
    assert(transaction.accept(state) == edits::Applied::changed);
    holdWork = true; ResetEvent(resume); ResetEvent(entered);
    transaction.pump(renderer, worldModel, renderer);
    assert(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
    assert(transaction.pending() && !transaction.applied().revision); coupled(false, false);
    holdWork = false; SetEvent(resume); finish(transaction, renderer, worldModel);
    assert(transaction.applied().revision == 2); coupled(false, true);

    state.revision = 3; state.cuts.push_back({3, {}, {{32,32,-8},{64,64,8}}});
    frame(transaction, renderer, worldModel, 9, 3, 41);
    assert(transaction.accept(state) == edits::Applied::changed);
    finish(transaction, renderer, worldModel);
    assert(transaction.applied().revision == 3); coupled(true, true);

    // Losing a complete entity frame revokes both consumers without losing the
    // static world revision. A new complete frame authorizes both again.
    client_map::brush_identities().invalidate(); transaction.publish(renderer, worldModel, renderer);
    coupled(true, false);
    frame(transaction, renderer, worldModel, 9, 3, 42); coupled(true, true);

    // Invalid physics fails before Renderer starts any upload/preparation.
    const auto previousWorld = g_WorldSurfaceModels[1];
    const auto previousBrush = R_FindEditedBrushSurfaceModel(&first);
    const auto preserved = [&] {
        assert(transaction.applied().revision == 3); collisionRevision(3);
        assert(g_WorldSurfaceModels[1] == previousWorld);
        assert(R_FindEditedBrushSurfaceModel(&first) == previousBrush);
        coupled(true, true);
    };
    state.revision = 4;
    for (auto& cut : state.cuts) { cut.box.min[0] += 64; cut.box.max[0] += 64; }
    assert(transaction.accept(state) == edits::Applied::changed);
    const auto before = uploads; brush.type = mod_studio;
    Reject([&] { transaction.pump(renderer, worldModel, renderer); }); brush.type = mod_brush;
    assert(!transaction.ready() && uploads == before); preserved();

    // Real GL preparation and worker creation failures preserve both old views.
    assert(transaction.accept(state) == edits::Applied::changed); failUpload = true;
    Reject([&] { transaction.pump(renderer, worldModel, renderer); });
    assert(!transaction.ready()); preserved();
    assert(transaction.accept(state) == edits::Applied::changed); failCreate = true;
    Reject([&] { transaction.pump(renderer, worldModel, renderer); }); failCreate = false;
    assert(!transaction.ready()); preserved();

    // Fail a leaf upload during Poll, after Begin has already returned a ticket.
    assert(transaction.accept(state) == edits::Applied::changed);
    holdWork = true; ResetEvent(resume); ResetEvent(entered);
    transaction.pump(renderer, worldModel, renderer);
    assert(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0 && transaction.pending());
    failUpload = true; holdWork = false; SetEvent(resume);
    Reject([&] { finish(transaction, renderer, worldModel); });
    assert(!transaction.ready()); preserved();

    // Cancel queued work with a newer complete snapshot, then finish recovery.
    assert(transaction.accept(state) == edits::Applied::changed);
    holdWork = true; ResetEvent(resume); ResetEvent(entered);
    transaction.pump(renderer, worldModel, renderer);
    assert(WaitForSingleObject(entered, 5000) == WAIT_OBJECT_0);
    state.revision = 5;
    for (auto& cut : state.cuts) { cut.box.min[0] -= 32; cut.box.max[0] -= 32; }
    assert(transaction.accept(state) == edits::Applied::changed); preserved();
    holdWork = false; SetEvent(resume); R_DrainWorldLeafTasks();
    frame(transaction, renderer, worldModel, 9, 5, 43);
    finish(transaction, renderer, worldModel);
    assert(transaction.applied().revision == 5); collisionRevision(5); coupled(true, true, 32);

    // Steady joint publication, physics and draw must not allocate or upload.
    auto selected = R_FindEditedBrushSurfaceModel(&first);
    draw(selected, -128); ray(208,64); R_DrainWorldLeafTasks();
    const auto warmUploads = uploads; countedAllocations = 0; countAllocations = true;
    for (int i = 0; i < 256; ++i) {
        transaction.pump(renderer, worldModel, renderer);
        assert(ray(208,64)); draw(R_FindEditedBrushSurfaceModel(&first), -128);
    }
    countAllocations = false;
    assert(!countedAllocations && uploads == warmUploads); coupled(true, true, 32);

    Reject([&] { transaction.publish(nullptr, worldModel); });
    assert(!transaction.applied().revision && !client_map::brush_identities().frame());
    rebuild(); coupled(false, false);
    transaction.reset(10); state.epoch = 10; state.revision = 5;
    frame(transaction, renderer, worldModel, 10, 5, 1);
    assert(transaction.accept(state) == edits::Applied::changed);
    finish(transaction, renderer, worldModel); collisionRevision(5); coupled(true, true, 32);
    transaction.reset(0); client_map::shutdown_events();
    R_DrainWorldLeafTasks(); g_WorldSurfaceModels.clear(); g_WorldSurfaceWorldModels.clear();
    fixtureEntities[10] = fixtureEntities[11] = nullptr; candidateInline = nullptr;
    gEngfuncs.CL_LoadModel = nullptr; gEngfuncs.hudGetModelByIndex = nullptr;
    gEngfuncs.GetEntityByIndex = nullptr; gEngfuncs.pfnAngleVectors = nullptr;
    movement.reset(); worldModel = nullptr;
    assert(!liveWork && glGetError() == GL_NO_ERROR);
    std::puts("{\"jointWorldEditGL\":true,\"productionTransaction\":true,\"productionCollision\":true,\"productionRenderer\":true,\"realPixelsAndRays\":true,\"sameModelIsolation\":true,\"preparingPreserved\":true,\"physicsFailure\":true,\"beginGpuFailure\":true,\"workerFailure\":true,\"pollGpuFailure\":true,\"cancelRecovery\":true,\"identityRevocation\":true,\"providerRecovery\":true,\"warmFrames\":256,\"warmAllocations\":0,\"warmUploads\":0,\"passed\":true}");
}
} // namespace joint_edit_test
