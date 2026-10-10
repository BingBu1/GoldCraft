// Executes the production transaction, Receiver and collision preparation.
// The Renderer is an ABI dispatch spy: GPU/worker behavior is tested separately.
model_t transaction_brush{};
cl_entity_t transaction_entity{};
model_t *transaction_load(const char *name, int *index) {
    *index = std::strcmp(name, "*1") ? -1 : 77;
    return *index == 77 ? &transaction_brush : nullptr;
}
model_t *transaction_model(int index) { return index == 77 ? &transaction_brush : nullptr; }
cl_entity_t *transaction_entity_at(int index) { return index == 7 ? &transaction_entity : nullptr; }
void transaction_angles(const float *, float *, float *, float *) { assert(false); }

struct TargetedRenderer final : IMetaRendererWorldEdit2 {
    std::array<MetaWorldEditCut, 8> cuts{};
    std::array<MetaWorldEditTarget, brush::max_entities> identities{};
    std::uint64_t next{}, ticket{}, revision{}, visible{}, snapshot_epoch{}, snapshot_revision{};
    std::uint32_t snapshot_sequence{}, count{}, identity_count{};
    unsigned begins{}, legacy_begins{}, cancels{}, resets{}, publications{}, revocations{}, commits{};
    std::size_t commit_allocations{};
    MetaWorldEditStatus status = MetaWorldEditStatus::Preparing;
    bool begin_ok = true, commit_ok = false, snapshot_ok = true, snapshot_ready = false;
    client_map::EditTransaction *transaction = nullptr;
    std::uint64_t expected_old{};
    void (*before_commit)() = nullptr;
    uint64_t Begin(model_s *, uint64_t, uint64_t, const MetaWorldEditBox *, uint32_t) noexcept override {
        ++legacy_begins;
        assert(false); // API002 receives the entire targeted state, including world cuts.
        return 0;
    }
    uint64_t BeginTargeted(model_s *loaded, uint64_t epoch, uint64_t value,
                           const MetaWorldEditCut *values, uint32_t size) noexcept override {
        assert(loaded == &model && epoch && size <= cuts.size() && (!size || values));
        ++begins;
        count = size;
        std::copy_n(values, size, cuts.begin());
        revision = value;
        return ticket = begin_ok ? ++next : 0;
    }
    MetaWorldEditStatus Poll(uint64_t value) noexcept override {
        assert(value && value == ticket);
        return status;
    }
    bool Commit(uint64_t value) noexcept override {
        assert(value && value == ticket);
        assert(!transaction || transaction->applied().revision == expected_old);
        if (before_commit) before_commit();
        if (!commit_ok) return false;
        visible = revision;
        ticket = 0;
        ++commits;
        commit_allocations = allocations.load();
        return true;
    }
    void Cancel(uint64_t value) noexcept override {
        assert(value && value == ticket);
        ticket = 0;
        ++cancels;
    }
    void Reset() noexcept override {
        visible = ticket = snapshot_epoch = 0;
        snapshot_ready = false;
        ++resets;
    }
    bool UpdateBrushSnapshot(uint64_t epoch, uint64_t value, uint32_t sequence,
                              const MetaWorldEditTarget *entries, uint32_t size) noexcept override {
        if (!epoch) {
            assert(!value && !sequence && !entries && !size);
            snapshot_ready = false;
            ++revocations;
            return false; // Exactly the production API002 revocation return value.
        }
        ++publications;
        assert(size <= identities.size());
        assert(static_cast<const void *>(entries) !=
               static_cast<const void *>(client_map::brush_identities().frame()->entries.data()));
        std::copy_n(entries, size, identities.begin());
        identity_count = size;
        snapshot_epoch = epoch;
        snapshot_revision = value;
        snapshot_sequence = sequence;
        snapshot_ready = snapshot_ok;
        return snapshot_ok;
    }
};
void transaction_frame(brush::Frame &frame) {
    std::array<std::uint8_t, 184> bytes{};
    for (std::size_t first = 0; first < frame.total || !first; first += brush::chunk_entries) {
        const auto size = brush::encode(frame, first, bytes);
        const bool complete = client_map::brush_identities().accept(std::span(bytes).first(size));
        assert(complete == (first + brush::chunk_entries >= frame.total));
        if (!frame.total) break;
    }
}
void transaction_query() {
    // Cross the world cut too: the inherited native callback is a dispatch
    // sentinel, so an unrelated world query intentionally returns its .25 hit.
    float from[]{-64, 0, 0}, to[]{384, 0, 0};
    run(from, to, 2);
}
void transaction_clear() { transaction_query(); assert(result.fraction == 1); }
void transaction_native() { transaction_query(); assert(result.ent == 1 && result.fraction == .6f); }
template <class F> void transaction_rejects(F &&action) {
    try { action(); assert(false); } catch (const ProtocolError &) {}
}
void targeted_transaction() {
    setup();
    model.numsubmodels = 2;
    transaction_brush = model;
    std::strcpy(transaction_brush.name, "*1");
    transaction_entity = {};
    transaction_entity.model = &transaction_brush;
    transaction_entity.curstate.number = 7;
    transaction_entity.curstate.modelindex = 77;
    transaction_entity.curstate.solid = 4;
    transaction_entity.curstate.messagenum = 40;
    auto &entity = pm->physents[1];
    entity.info = 7;
    entity.solid = 4;
    entity.model = &transaction_brush;
    entity.origin[0] = 320;
    pm->visents[1] = entity;
    include_other = true;
    cl_enginefunc_t engine{};
    engine.CL_LoadModel = transaction_load;
    engine.hudGetModelByIndex = transaction_model;
    engine.GetEntityByIndex = transaction_entity_at;
    engine.pfnAngleVectors = transaction_angles;
    client_map::engine_initialized(&engine);
    TargetedRenderer renderer;
    client_map::EditTransaction transaction;
    renderer.transaction = &transaction;
    transaction.reset(9);
    brush::Frame frame{9, 2, 40, 1};
    frame.entries[0] = {7, 55, 1};
    transaction_frame(frame);
    // Publication works without any ready or pending map-edit transaction.
    transaction.publish(&renderer, &model, &renderer);
    assert(renderer.publications == 1 && renderer.snapshot_ready && !transaction.ready());
    assert(renderer.identities[0].slot == 7 && renderer.identities[0].serial == 55 &&
           renderer.identities[0].inlineModel == 1);
    transaction.publish(&renderer, &model, &renderer);
    assert(renderer.publications == 1);
    auto mixed = snapshot({-1, -64, -64}, {17, 64, 64});
    mixed.cuts[0].id = 1;
    mixed.cuts.push_back({2, {7, 55, 1}, mixed.cuts[0].box});
    assert(transaction.accept(mixed) == edits::Applied::changed);
    transaction.pump(&renderer, &model, &renderer);
    assert(renderer.begins == 1 && !renderer.legacy_begins && renderer.count == 2);
    for (std::size_t i = 0; i < mixed.cuts.size(); ++i) {
        const auto &a = mixed.cuts[i]; const auto &b = renderer.cuts[i];
        assert(a.target.slot == b.target.slot && a.target.serial == b.target.serial &&
               a.target.model == b.target.inlineModel);
        assert(std::equal(a.box.min.begin(), a.box.min.end(), b.box.min) &&
               std::equal(a.box.max.begin(), a.box.max.end(), b.box.max));
    }
    transaction_query(); assert(result.hitgroup == 77 && transaction.applied().revision == 0);
    renderer.status = MetaWorldEditStatus::Ready;
    transaction.pump(&renderer, &model, &renderer);
    assert(!renderer.visible && !transaction.applied().revision);
    renderer.commit_ok = true;
    renderer.before_commit = [] { transaction_query(); assert(result.hitgroup == 77); };
    transaction.pump(&renderer, &model, &renderer);
    assert(allocations.load() == renderer.commit_allocations);
    assert(renderer.visible == 2 && transaction.applied().revision == 2 && !transaction.pending());
    transaction_clear();
    renderer.expected_old = 2;
    renderer.before_commit = transaction_clear;

    // A physics preparation failure cannot even start a renderer transaction.
    auto newer = mixed; newer.revision = 3;
    transaction_brush.type = mod_studio;
    transaction.accept(newer);
    transaction_rejects([&] { transaction.pump(&renderer, &model, &renderer); });
    transaction_brush.type = mod_brush;
    assert(renderer.begins == 1 && renderer.visible == 2 && !transaction.ready());
    transaction_clear();
    transaction.accept(newer);
    renderer.begin_ok = false;
    transaction_rejects([&] { transaction.pump(&renderer, &model, &renderer); });
    assert(renderer.visible == 2 && !transaction.ready());
    transaction_clear();
    renderer.begin_ok = true;
    transaction.accept(newer);
    renderer.status = MetaWorldEditStatus::Failed;
    transaction_rejects([&] { transaction.pump(&renderer, &model, &renderer); });
    assert(renderer.visible == 2 && !transaction.ready() && renderer.cancels == 1);
    transaction_clear();

    // Invalid/missing identity immediately restores native dynamic collision;
    // the static edit and prior committed replica remain available for recovery.
    client_map::brush_identities().invalidate();
    transaction.publish(&renderer, &model, &renderer);
    assert(!renderer.snapshot_ready && renderer.revocations == 1 && renderer.visible == 2);
    transaction_native();
    ++frame.sequence; transaction_entity.curstate.messagenum = int(frame.sequence);
    transaction_frame(frame); transaction.publish(&renderer, &model, &renderer);
    transaction_clear();
    const auto before_reject = renderer.revocations;
    renderer.snapshot_ok = false;
    ++frame.sequence; transaction_entity.curstate.messagenum = int(frame.sequence);
    transaction_frame(frame);
    transaction_rejects([&] { transaction.publish(&renderer, &model, &renderer); });
    assert(!client_map::brush_identities().frame() && !renderer.snapshot_ready &&
           renderer.revocations == before_reject + 1);
    transaction.publish(&renderer, &model, &renderer);
    assert(renderer.revocations == before_reject + 1); // No repeated error/revocation loop.
    transaction_native();
    renderer.snapshot_ok = true;
    ++frame.sequence; transaction_entity.curstate.messagenum = int(frame.sequence);
    transaction_frame(frame); transaction.publish(&renderer, &model, &renderer);
    transaction_clear();

    // Full-state replacement removes the dynamic target while retaining world cuts.
    auto world_only = snapshot({-1, -64, -64}, {17, 64, 64}); world_only.revision = 3;
    transaction.accept(world_only);
    renderer.status = MetaWorldEditStatus::Preparing;
    transaction.pump(&renderer, &model, &renderer);
    assert(renderer.count == 1 && !renderer.cuts[0].target.slot);
    transaction_clear();
    renderer.status = MetaWorldEditStatus::Ready;
    transaction.pump(&renderer, &model, &renderer);
    assert(allocations.load() == renderer.commit_allocations);
    assert(renderer.visible == 3 && transaction.applied().revision == 3);
    transaction_native();

    // Provider loss clears both consumers and authorization before recovery.
    transaction_rejects([&] { transaction.publish(nullptr, &model); });
    assert(!transaction.ready() && !transaction.applied().revision && !renderer.visible &&
           !renderer.snapshot_ready && !client_map::brush_identities().frame());
    transaction_query(); assert(result.hitgroup == 77);
    TargetedRenderer replacement;
    transaction.publish(&replacement, &model, &replacement);
    assert(replacement.revocations == 1 && !replacement.begins);
    transaction.reset(10);
    assert(replacement.resets == 1 && !client_map::brush_identities().frame());

    // Complete 1024-entry tables, including a high slot, publish without an edit
    // transaction and without allocations. Empty complete frames also publish.
    frame = {10, 1, 1, static_cast<std::uint16_t>(brush::max_entities)};
    for (std::uint32_t i = 0; i < frame.total; ++i) frame.entries[i] = {i + 1, i + 50, 1};
    frame.entries[frame.total - 1].slot = 65535;
    transaction_frame(frame); transaction.publish(&replacement, &model, &replacement);
    assert(replacement.identity_count == brush::max_entities &&
           replacement.identities.back().slot == 65535 && !transaction.ready());
    const auto warm_allocations = allocations.load();
    for (int repeat = 0; repeat < 4096; ++repeat) {
        ++frame.sequence;
        transaction_frame(frame);
        transaction.publish(&replacement, &model, &replacement);
    }
    assert(allocations.load() == warm_allocations);
    const auto publications = replacement.publications;
    ++frame.sequence; frame.total = 0;
    transaction_frame(frame); transaction.publish(&replacement, &model, &replacement);
    assert(replacement.publications == publications + 1 && !replacement.identity_count &&
           replacement.snapshot_ready);
    // Model/provider replacement is also rejected when no edit is pending.
    model_t other = model;
    transaction_rejects([&] { transaction.publish(&replacement, &other, &replacement); });
    assert(!replacement.snapshot_ready && !client_map::brush_identities().frame());
    transaction.reset(0);
    client_map::engine_initialized(nullptr);
    std::cout << "{\"targetedTransaction\":true,\"productionCollision\":true,"
                 "\"completeState\":true,\"prepareFailureRollback\":true,"
                 "\"rendererFirstCommit\":true,\"commitAllocations\":0,"
                 "\"independentIdentityPublication\":true,\"revokeAndProviderLoss\":true,"
                 "\"copied1024Identities\":true,\"warmPublications\":4096,"
                 "\"publicationAllocations\":0,\"passed\":true}\n";
}
