// Actual copied hw.dll PM routines, with controlled public entity/model state.
// This is collision evidence, not rendered/networked gameplay.
std::array<cl_entity_t, 1025> brush_entities;
std::array<int, MAX_PHYSENTS> predicate_order;
std::size_t predicate_count = 0;
int predicate_skip = -1, predicate_change = 0;
int brush_line_flags = 0, predicate_throws = 0;
int ordered_brush_predicate(physent_t *entity) {
    assert(predicate_count < predicate_order.size());
    predicate_order[predicate_count++] = entity->info;
    if (entity->info == 23 && predicate_throws) {
        --predicate_throws;
        throw std::runtime_error("Injected native line predicate failure");
    }
    if (entity->info == 23 && predicate_change) {
        const int action = std::exchange(predicate_change, 0);
        if (action == 1) client_map::brush_identities().invalidate();
        if (action == 2) brush_entities[7].curstate.messagenum++;
        if (action == 3) client_map::reset();
        if (action == 4) {
            auto &receiver = client_map::brush_identities();
            const auto saved = *receiver.frame();
            receiver.reset(saved.epoch);
            std::array<std::uint8_t, 184> encoded;
            const auto size = brush::encode(saved, 0, encoded);
            assert(receiver.accept(std::span(encoded).first(size)));
        }
        if (action == 5) {
            const auto before = predicate_count;
            client_map::move(pm.get(), 0, [](playermove_t *movement, int) {
                const auto hit = movement->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1);
                assert(hit.fraction == 1);
                const auto line = movement->PM_TraceLineEx(queried_start, queried_end, 1, 2, nullptr);
                assert(line && line->fraction == 1);
            });
            assert(predicate_count == before);
        }
    }
    return entity->info == predicate_skip;
}
void engine_brushes(Bsp &bsp, unsigned char *base_address, event_api_t *native, event_api_t *api) {
    auto *math = LoadLibraryExW(L"api-ms-win-crt-math-l1-1-0.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(math);
    std::array<FARPROC *, 2> imports{reinterpret_cast<FARPROC *>(base_address + 0x2b245c),
                                    reinterpret_cast<FARPROC *>(base_address + 0x2b2460)};
    const std::array<FARPROC, 2> original_imports{*imports[0], *imports[1]};
    const std::array<const char *, 2> names{"_libm_sse2_sin_precise", "_libm_sse2_cos_precise"};
    for (std::size_t i = 0; i < imports.size(); ++i) {
        DWORD old;
        assert(VirtualProtect(imports[i], sizeof(FARPROC), PAGE_READWRITE, &old));
        *imports[i] = GetProcAddress(math, names[i]);
        assert(*imports[i]);
        assert(VirtualProtect(imports[i], sizeof(FARPROC), old, &old));
    }
    struct RestoreMath {
        HMODULE module;
        const decltype(imports) &slots;
        const decltype(original_imports) &saved;
        ~RestoreMath() {
            client_map::engine_initialized(nullptr);
            for (std::size_t i = 0; i < slots.size(); ++i) {
                DWORD old;
                VirtualProtect(slots[i], sizeof(FARPROC), PAGE_READWRITE, &old);
                *slots[i] = saved[i];
                VirtualProtect(slots[i], sizeof(FARPROC), old, &old);
            }
            FreeLibrary(module);
        }
    } restore_math{math, imports, original_imports};
    bsp.load(11); // Actual cs_assault func_door_rotating, model-local brush geometry.
    // Execute the public engine lookup callbacks too. The server precache slot,
    // inline-model number, entity slot and PM array index are all distinct.
    static_assert(sizeof(cl_entity_t) == 3000);
    auto *engine_table = reinterpret_cast<cl_enginefunc_t *>(base_address + 0x319b08);
    assert(reinterpret_cast<unsigned char *>(engine_table->CL_LoadModel) == base_address + 0x196110);
    assert(reinterpret_cast<unsigned char *>(engine_table->hudGetModelByIndex) == base_address + 0x195e00);
    assert(reinterpret_cast<unsigned char *>(engine_table->GetEntityByIndex) == base_address + 0x1960b0);
    auto **models = reinterpret_cast<model_t **>(base_address + 0x13f6410);
    auto **entities = reinterpret_cast<cl_entity_t **>(base_address + 0x32ead8);
    auto *entity_count = reinterpret_cast<int *>(base_address + 0x1257f60);
    std::array<model_t *, 512> saved_models;
    std::copy_n(models, saved_models.size(), saved_models.begin());
    struct RestoreEngineState {
        model_t **models;
        const decltype(saved_models) &saved;
        cl_entity_t **entities, *saved_entities;
        int *count, saved_count;
        ~RestoreEngineState() {
            std::copy(saved.begin(), saved.end(), models);
            *entities = saved_entities;
            *count = saved_count;
        }
    } restore_engine_state{models, saved_models, entities, *entities, entity_count, *entity_count};
    model_t placeholder{};
    std::strcpy(placeholder.name, "unrelated-model");
    std::fill_n(models, 512, nullptr);
    std::fill(models + 1, models + 77, &placeholder);
    models[77] = &model;
    *entities = brush_entities.data();
    *entity_count = int(brush_entities.size());
    int resolved_index = 0;
    assert(engine_table->CL_LoadModel("*11", &resolved_index) == &model && resolved_index == 77);
    assert(engine_table->hudGetModelByIndex(77) == &model);
    assert(!engine_table->hudGetModelByIndex(78));
    assert(!engine_table->CL_LoadModel("*12", &resolved_index) && resolved_index == -1);
    assert(engine_table->GetEntityByIndex(7) == &brush_entities[7]);
    assert(!engine_table->GetEntityByIndex(-1) &&
           !engine_table->GetEntityByIndex(int(brush_entities.size())));
    model_t empty = model;
    dclipnode_t empty_nodes[2]{{0, {-1, -1}}, {0, {-1, -1}}};
    for (auto &hull : empty.hulls) {
        hull.clipnodes = empty_nodes;
        hull.firstclipnode = 0;
        hull.lastclipnode = 1;
    }
    const auto initialize = reinterpret_cast<void (*)(playermove_t *)>(base_address + 0x1e1490);
    setup();
    initialize(pm.get());
    pm->numphysent = pm->numvisent = 3;
    pm->physents[0].model = &empty;
    pm->physents[0].solid = 4;
    for (int i = 1; i <= 2; ++i) {
        auto &pe = pm->physents[i];
        pe = {};
        pe.info = i == 1 ? 7 : 23; // Entity slots deliberately differ from PM indices.
        pe.model = &model;
        pe.solid = 4;
        pe.origin[0] = i == 1 ? 0.0f : 256.0f;
        auto &entity = brush_entities[pe.info];
        entity = {};
        entity.index = entity.curstate.number = pe.info;
        entity.model = &model;
        entity.curstate.solid = 4;
        entity.curstate.modelindex = 77;
        entity.curstate.messagenum = 100;
        // Render interpolation is deliberately unrelated to PM transforms.
        entity.origin[0] = 9000;
    }
    std::copy_n(pm->physents, 3, pm->visents);
    std::swap(pm->visents[1], pm->visents[2]);
    cl_enginefunc_t engine{};
    engine.CL_LoadModel = engine_table->CL_LoadModel;
    engine.GetEntityByIndex = engine_table->GetEntityByIndex;
    engine.hudGetModelByIndex = engine_table->hudGetModelByIndex;
    engine.pfnAngleVectors = engine_table->pfnAngleVectors;
    client_map::engine_initialized(&engine);
    auto &receiver = client_map::brush_identities();
    brush::Frame frame;
    frame.epoch = 9; frame.revision = 2; frame.sequence = 100; frame.total = 1;
    frame.entries[0] = {7, 55, 11};
    const auto publish = [&] {
        receiver.reset(9);
        std::array<std::uint8_t, 184> encoded;
        const auto size = brush::encode(frame, 0, encoded);
        assert(receiver.accept(std::span(encoded).first(size)));
        brush_entities[7].curstate.messagenum = int(frame.sequence);
    };
    const auto state = edits::Snapshot{9, 2, {{2, {7, 55, 11}, {{-8, -52, -44}, {8, -4, 44}}}}};
    prepare(state, &empty);
    publish();
    client_map::movement_initialized(pm.get());
    float a[3]{-80, -28, 0}, b[3]{80, -28, 0};
    const auto event = [&](int hull = 2, int flags = PM_NORMAL, int ignored = -1) {
        api->EV_SetTraceHull(hull);
        api->EV_PlayerTrace(a, b, flags, ignored, &result);
    };
    pmtrace_t original{};
    native->EV_SetTraceHull(2);
    native->EV_PlayerTrace(a, b, PM_NORMAL, -1, &original);
    assert(original.ent == 1 && original.fraction < 1);
    for (int hull : {0, 1, 2}) {
        event(hull);
        assert(result.fraction == 1 && result.ent == -1);
    }
    event(3);
    assert(result.ent == 1 && result.fraction < 1);
    b[0] = 320;
    event();
    assert(result.ent == 2 && api->EV_IndexFromTrace(&result) == 23);
    event(2, PM_NORMAL, 2);
    assert(result.fraction == 1);
    event(2, PM_WORLD_ONLY);
    assert(result.fraction == 1);
    pm->physents[2].rendermode = 1;
    event(2, PM_GLASS_IGNORE);
    assert(result.fraction == 1);
    pm->physents[2].rendermode = 0;
    // Equal-fraction hits preserve the first PM array entry, even when that is
    // the untouched instance and the edited entity has a numerically lower slot.
    pm->physents[2].origin[0] = 0;
    a[1] = b[1] = -8;
    event(0);
    assert(result.ent == 1);
    const float tie_fraction = result.fraction;
    std::swap(pm->physents[1], pm->physents[2]);
    event(0);
    assert(result.ent == 1 && result.fraction == tie_fraction);
    std::swap(pm->physents[1], pm->physents[2]);
    pm->physents[2].origin[0] = 256;
    b[0] = 80;
    a[1] = b[1] = -8;
    event(0);
    assert(result.ent == 1);
    event(2);
    assert(result.fraction == 1);
    a[1] = b[1] = -28;
    std::copy_n(a, 3, queried_start); std::copy_n(b, 3, queried_end);
    pm->usehull = 2;
    client_map::move(pm.get(), 0, [](playermove_t *movement, int) {
        predicate_count = 0; predicate_skip = -1;
        auto hit = movement->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, ordered_brush_predicate);
        assert(hit.fraction == 1 && predicate_count == 3);
        assert(predicate_order[0] == 0 && predicate_order[1] == 7 && predicate_order[2] == 23);
        predicate_count = 0;
        hit = movement->PM_PlayerTraceEx(queried_start, queried_end, PM_WORLD_ONLY, ordered_brush_predicate);
        assert(hit.fraction == 1 && predicate_count == 1);
        hit = *movement->PM_TraceLine(queried_start, queried_end, 1, 2, -1);
        assert(hit.fraction == 1); // Edited slot7 is now visent index2.
        hit = *movement->PM_TraceLineEx(queried_start, queried_end, 0, 2, nullptr);
        assert(hit.fraction == 1);
        float inside[3]{0, -28, 0}, rim[3]{0, -55, 0};
        std::copy_n(inside, 3, movement->origin);
        pmtrace_t origin;
        assert(movement->PM_TestPlayerPosition(inside, &origin) == -1 && origin.fraction == 1);
        predicate_count = 0;
        assert(movement->PM_TestPlayerPositionEx(rim, &origin, ordered_brush_predicate) == 1);
        assert(predicate_count == 2 && origin.fraction == 1);
        predicate_count = 0; predicate_skip = 7;
        assert(movement->PM_TestPlayerPositionEx(rim, &origin, ordered_brush_predicate) == -1);
        assert(predicate_count == 3 && origin.fraction == 1);
        predicate_skip = -1;
        int raw = 0;
        assert(movement->PM_PointContents(rim, &raw) == -1 && raw == -1);
        assert(movement->PM_TruePointContents(rim) == -1); // SOLID_BSP is not a water volume.
    });
    // Missing/old identities, deletion, changed model/solidity and slot reuse fail closed.
    const auto same_native = [&] {
        native->EV_SetTraceHull(2);
        native->EV_PlayerTrace(a, b, PM_NORMAL, -1, &original);
        event();
        assert(std::memcmp(&original, &result, sizeof(result)) == 0);
    };
    receiver.invalidate(); same_native(); publish();
    receiver.reset(10); same_native(); publish();
    ++brush_entities[7].curstate.messagenum; same_native(); publish();
    frame.entries[0].serial++; publish(); same_native(); frame.entries[0].serial--; publish();
    frame.entries[0].model++; publish(); same_native(); frame.entries[0].model--; publish();
    frame.revision = 1; publish(); same_native(); frame.revision = 2; publish();
    brush_entities[7].model = nullptr; same_native(); brush_entities[7].model = &model;
    brush_entities[7].curstate.solid = 0; same_native(); brush_entities[7].curstate.solid = 4;
    brush_entities[7].curstate.modelindex = 78; same_native(); brush_entities[7].curstate.modelindex = 77;
    pm->physents[1].solid = 0; same_native(); pm->physents[1].solid = 4;
    // A newer incomplete sideband must not authorize a newer entity snapshot.
    auto fragmented = frame;
    fragmented.sequence++;
    fragmented.total = 17;
    for (unsigned i = 0; i < fragmented.total; ++i) fragmented.entries[i] = {i + 1, 55, 11};
    std::array<std::uint8_t, 184> fragment;
    auto count = brush::encode(fragmented, 0, fragment);
    assert(!receiver.accept(std::span(fragment).first(count)));
    brush_entities[7].curstate.messagenum = int(fragmented.sequence);
    same_native();
    count = brush::encode(fragmented, 16, fragment);
    assert(receiver.accept(std::span(fragment).first(count)));
    event(); assert(result.fraction == 1);
    publish();
    // Generation and identity changes inside a later predicate invalidate the collected edit.
    for (int change : {1, 2, 3, 4}) {
        publish(); prepare(state, &empty);
        predicate_change = change; predicate_count = 0;
        client_map::move(pm.get(), 0, [](playermove_t *movement, int) {
            const auto hit = movement->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, ordered_brush_predicate);
            assert(hit.ent == 1 && hit.fraction < 1);
        });
    }
    // The engine temporarily sets usehull inside PM_TraceLineEx. A callback
    // exception must restore it before both fallback and the caller resume.
    for (int flags : {0, 1}) {
        brush_line_flags = flags;
        for (int change : {1, 2, 3, 4}) {
            publish(); prepare(state, &empty);
            pm->usehull = 0; predicate_change = change; predicate_count = 0;
            client_map::move(pm.get(), 0, [](playermove_t *movement, int) {
                const auto hit = movement->PM_TraceLineEx(queried_start, queried_end,
                    brush_line_flags, 2, ordered_brush_predicate);
                if (movement->usehull != 0) throw std::runtime_error("Native line leaked trace hull");
                assert(hit && hit->ent == (brush_line_flags ? 2 : 1) && hit->fraction < 1);
            });
        }
        publish(); prepare(state, &empty);
        pm->usehull = 0; predicate_throws = 2; predicate_count = 0;
        client_map::move(pm.get(), 0, [](playermove_t *movement, int) {
            const auto hit = movement->PM_TraceLineEx(queried_start, queried_end,
                brush_line_flags, 2, ordered_brush_predicate);
            assert(hit && hit->allsolid && hit->fraction == 0 && movement->usehull == 0);
            assert(predicate_throws == 0);
        });
    }
    pm->usehull = 2;
    publish(); prepare(state, &empty);
    predicate_change = 5; predicate_count = 0;
    client_map::move(pm.get(), 0, [](playermove_t *movement, int) {
        const auto hit = movement->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, ordered_brush_predicate);
        assert(hit.fraction == 1 && predicate_count == 3);
    });
    // Failed preparation must leave the previously committed instance intact.
    auto invalid = state;
    invalid.revision = 3;
    invalid.cuts.push_back({3, {7, 56, 11}, {{-8, -52, -44}, {8, -4, 44}}});
    try { prepare(invalid, &empty); assert(false); } catch (const ProtocolError &) {}
    event(); assert(result.fraction == 1);
    invalid = state; invalid.revision = 3; invalid.cuts[0].target.model = unsigned(model.numsubmodels);
    try { prepare(invalid, &empty); assert(false); } catch (const ProtocolError &) {}
    event(); assert(result.fraction == 1);
    // Exact CL_LoadModel only searches the contiguous server precache, and
    // missing models must fail preparation without altering the committed cut.
    models[77] = nullptr;
    invalid = state; invalid.revision = 3;
    try { prepare(invalid, &empty); assert(false); } catch (const ProtocolError &) {}
    same_native();
    models[77] = &model;
    event(); assert(result.fraction == 1);
    // Translation and arbitrary pitch/yaw/roll use the actual native AngleVectors.
    const std::array<std::array<float, 3>, 4> rotations{{{0, 0, 0}, {0, 90, 0}, {0, 37, 0}, {17, 37, 11}}};
    for (const auto &angles : rotations) {
        auto &pe = pm->physents[1];
        std::copy(angles.begin(), angles.end(), pe.angles);
        pe.origin[0] = 128; pe.origin[1] = 256; pe.origin[2] = 64;
        float forward[3], right[3], up[3];
        engine.pfnAngleVectors(pe.angles, forward, right, up);
        const auto world_point = [&](float x, float y, float z, float *out) {
            for (int i = 0; i < 3; ++i)
                out[i] = pe.origin[i] + (forward[i] * x - right[i] * y) + up[i] * z;
        };
        world_point(-80, -28, 0, a); world_point(80, -28, 0, b);
        native->EV_SetTraceHull(2); native->EV_PlayerTrace(a, b, PM_NORMAL, -1, &original);
        assert(original.ent == 1 && original.fraction < 1);
        for (int hull : {0, 1, 2}) { event(hull); assert(result.fraction == 1); }
        world_point(-80, -8, 0, a); world_point(80, -8, 0, b);
        event(0);
        assert(result.ent == 1 && result.fraction < 1);
        // Contact normal must rotate, local plane distance must not translate.
        for (int i = 0; i < 3; ++i) assert(std::abs(result.plane.normal[i] + forward[i]) < .0001f);
        assert(std::abs(result.plane.dist - 20.0f) < .0001f);
    }
    pm->physents[1].angles[0] = pm->physents[1].angles[1] = pm->physents[1].angles[2] = 0;
    pm->physents[1].origin[0] = pm->physents[1].origin[1] = pm->physents[1].origin[2] = 0;
    a[0] = -80; a[1] = -28; a[2] = 0; b[0] = 80; b[1] = -28; b[2] = 0;
    event(); // Warm reusable hull scratch before measuring.
    const auto before = allocations.load();
    for (int i = 0; i < 4096; ++i) { event(); assert(result.fraction == 1); }
    assert(before == allocations.load());
    auto both = state;
    both.revision = 3;
    both.cuts.push_back({3, {23, 66, 11}, state.cuts[0].box});
    frame.revision = 3; frame.total = 2; frame.entries[1] = {23, 66, 11}; publish();
    prepare(both, &empty);
    b[0] = 320;
    event(); assert(result.fraction == 1);
    // Removing one instance's edit does not discard another instance's cut.
    both.revision = 4; both.cuts.erase(both.cuts.begin());
    frame.revision = 4; publish(); prepare(both, &empty);
    event(); assert(result.ent == 1);
    event(2, PM_NORMAL, 1); assert(result.fraction == 1);
    prepare({9, 5, {}}, nullptr);
    same_native();
    // A high entity slot in a complete 1024-entry sideband still occupies a
    // small PM-array index. Incomplete identity fragments keep native collision.
    static_assert(brush::max_entities == 1024);
    brush_entities[700] = brush_entities[7];
    brush_entities[700].index = brush_entities[700].curstate.number = 700;
    brush_entities[700].curstate.messagenum = 101;
    pm->physents[1].info = 700;
    prepare({9, 6, {{6, {700, 55, 11}, state.cuts[0].box}}}, &empty);
    frame.revision = 6; frame.sequence = 101; frame.total = 1024;
    for (unsigned i = 0; i < frame.total; ++i) frame.entries[i] = {i + 1, 55, 11};
    receiver.reset(9);
    b[0] = 80;
    for (unsigned first = 0; first < frame.total; first += brush::chunk_entries) {
        const auto size = brush::encode(frame, first, fragment);
        const bool complete = first + brush::chunk_entries == frame.total;
        assert(receiver.accept(std::span(fragment).first(size)) == complete);
        event();
        assert(complete ? result.fraction == 1 : (result.ent == 1 && result.fraction < 1));
    }
    frame.sequence++;
    frame.entries[699].serial++;
    for (unsigned first = 0; first < frame.total; first += brush::chunk_entries) {
        const auto size = brush::encode(frame, first, fragment);
        receiver.accept(std::span(fragment).first(size));
    }
    brush_entities[700].curstate.messagenum = int(frame.sequence);
    same_native();
    client_map::reset(); receiver.reset();
    std::cout << "{\"actualEngineBrushTrace\":true,\"actualPublicModelEntityLookups\":true,"
                 "\"actualAssaultDoor\":11,\"distinctInstances\":true,"
                 "\"fourHulls\":true,\"rotationTranslation\":true,\"predicateOrder\":true,"
                 "\"identityLifecycle\":true,\"equalHitOrder\":true,\"fragmentCompletion\":true,"
                 "\"nestedQueries\":true,\"visentsAndEvents\":true,\"identity1024HighSlot\":true,"
                 "\"exceptionHullRestored\":true,\"warmQueries\":4096,"
                 "\"warmAllocations\":0,\"passed\":true}\n";
}
