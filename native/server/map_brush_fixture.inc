// Compiled only into the independent headless GameDLL. All entity mutations
// below are restricted to fixture-owned clones; normal game entities are read-only.
struct BrushFixtureIdentity { int slot = 0, serial = 0; };
std::array<BrushFixtureIdentity, 16> fixture_brushes;
struct BrushFixturePhysics final : IHostMapPhysics {
    IHostMapPhysics *inner = nullptr;
    bool fail = false;
    unsigned attempts = 0;
    bool Replace(std::uint64_t epoch, std::uint64_t revision, const HostMapCut *cuts,
                 std::uint32_t count) override {
        ++attempts;
        return !fail && inner->Replace(epoch, revision, cuts, count);
    }
    void Reset(std::uint64_t epoch, std::uint64_t revision) override { inner->Reset(epoch, revision); }
    const char *LastError() const override { return fail ? "Injected brush cleanup failure" : inner->LastError(); }
    HostMapStats Stats() const override { return inner->Stats(); }
} fixture_physics;
void BrushFixtureReset() {
    fixture_brushes = {};
    fixture_physics = {};
}
edict_t *BrushFixtureEntity(int slot) {
    if (slot <= gpGlobals->maxClients || slot >= gpGlobals->maxEntities) return nullptr;
    auto *entity = INDEXENT(slot);
    for (const auto identity : fixture_brushes)
        if (identity.slot == slot && entity && !entity->free && entity->serialnumber == identity.serial)
            return entity;
    return nullptr;
}
void BrushFixtureState(edict_t *entity) {
    CarvePrint("[GC brush] slot=%d serial=%d model=%s index=%d solid=%d origin=%.6f,%.6f,%.6f angles=%.6f,%.6f,%.6f\n",
        ENTINDEX(entity), entity->serialnumber, STRING(entity->v.model), entity->v.modelindex,
        entity->v.solid, entity->v.origin.x, entity->v.origin.y, entity->v.origin.z,
        entity->v.angles.x, entity->v.angles.y, entity->v.angles.z);
}
bool BrushFixtureModel(int model, char (&name)[24]) {
    if (model <= 0 || model > 65535) return false;
    std::snprintf(name, sizeof(name), "*%d", model);
    for (int slot = gpGlobals->maxClients + 1; slot < gpGlobals->maxEntities; ++slot) {
        const auto *entity = INDEXENT(slot);
        if (entity && !entity->free && entity->v.modelindex && !std::strcmp(STRING(entity->v.model), name))
            return true;
    }
    return false;
}
void BrushFixture() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || CMD_ARGC() < 2) return;
    const char *action = CMD_ARGV(1);
    if (!std::strcmp(action, "api")) {
        CarvePrint("[GC brush] physics=%d sample=%d dynamic=%d failures=%u\n",
            map_physics != nullptr, map_sample != nullptr, dynamic_geometry_ready, fixture_physics.attempts);
        return;
    }
    if (!std::strcmp(action, "fail") && CMD_ARGC() == 3 && map_physics) {
        if (map_physics != &fixture_physics) fixture_physics.inner = map_physics;
        fixture_physics.fail = std::atoi(CMD_ARGV(2)) != 0;
        map_physics = fixture_physics.fail ? &fixture_physics : fixture_physics.inner;
        CarvePrint("[GC brush] failure=%d attempts=%u\n", fixture_physics.fail, fixture_physics.attempts);
        return;
    }
    if (!std::strcmp(action, "create") && CMD_ARGC() == 9) {
        char model[24];
        if (!BrushFixtureModel(std::atoi(CMD_ARGV(2)), model)) return;
        auto empty = std::find_if(fixture_brushes.begin(), fixture_brushes.end(),
                                  [](auto identity) { return !identity.slot; });
        if (empty == fixture_brushes.end()) return;
        Vector origin{}, angles{};
        for (int i = 0; i < 3; ++i) {
            origin[i] = std::strtof(CMD_ARGV(3 + i), nullptr);
            angles[i] = std::strtof(CMD_ARGV(6 + i), nullptr);
        }
        if (!std::isfinite(origin.Length()) || !std::isfinite(angles.Length())) return;
        auto *entity = CREATE_NAMED_ENTITY(MAKE_STRING("func_wall"));
        if (!entity) return;
        entity->v.movetype = MOVETYPE_PUSH;
        entity->v.solid = SOLID_BSP;
        entity->v.angles = angles;
        SET_MODEL(entity, model);
        SET_ORIGIN(entity, origin);
        *empty = {ENTINDEX(entity), entity->serialnumber};
        BrushFixtureState(entity);
        return;
    }
    if (CMD_ARGC() < 3) return;
    const int slot = std::atoi(CMD_ARGV(2));
    auto *entity = BrushFixtureEntity(slot);
    if (!entity) return;
    if (!std::strcmp(action, "pose") && CMD_ARGC() == 9) {
        Vector origin{}, angles{};
        for (int i = 0; i < 3; ++i) {
            origin[i] = std::strtof(CMD_ARGV(3 + i), nullptr);
            angles[i] = std::strtof(CMD_ARGV(6 + i), nullptr);
        }
        if (!std::isfinite(origin.Length()) || !std::isfinite(angles.Length())) return;
        entity->v.angles = angles;
        SET_ORIGIN(entity, origin);
    } else if (!std::strcmp(action, "solid") && CMD_ARGC() == 4) {
        const int value = std::atoi(CMD_ARGV(3));
        if (value != SOLID_BSP && value != SOLID_NOT) return;
        entity->v.solid = value;
        SET_ORIGIN(entity, entity->v.origin);
    } else if (!std::strcmp(action, "model") && CMD_ARGC() == 4) {
        char model[24];
        if (!BrushFixtureModel(std::atoi(CMD_ARGV(3)), model)) return;
        SET_MODEL(entity, model);
        SET_ORIGIN(entity, entity->v.origin);
    } else if (!std::strcmp(action, "remove") && CMD_ARGC() == 3) {
        REMOVE_ENTITY(entity); // Actual engine ED_Free owns serial advancement.
        CarvePrint("[GC brush] removed=%d free=%d serial=%d\n", slot, entity->free, entity->serialnumber);
        for (auto &identity : fixture_brushes) if (identity.slot == slot) identity = {};
        return;
    } else if (!std::strcmp(action, "sample") && CMD_ARGC() == 9 && map_sample) {
        Vector start{}, end{};
        for (int i = 0; i < 3; ++i) {
            start[i] = std::strtof(CMD_ARGV(3 + i), nullptr);
            end[i] = std::strtof(CMD_ARGV(6 + i), nullptr);
        }
        const auto model = std::strtoul(STRING(entity->v.model) + 1, nullptr, 10);
        HostMapCell cell{-999, -999, -999};
        const bool applied = map_sample->Sample(map_edits.state().epoch, map_edits.state().revision,
            slot, entity->serialnumber, model, start, end, cell);
        CarvePrint("[GC brush] sampled=%d cell=%d,%d,%d\n", applied, cell.x, cell.y, cell.z);
        return;
    }
    BrushFixtureState(entity);
}
