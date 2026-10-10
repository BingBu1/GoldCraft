// Public-PM adapter tests. Fake callbacks are dispatch spies, not an engine
// simulation. Actual map assertions use immutable, hash-identified BSP data.
// HLSDK model/PM headers require MetaHook base types first.
#include <metahook.h>
#include <com_model.h>
#include <pm_defs.h>

#include "goldcraft/wire.hpp"
#include "map_collision.hpp"
#include "map_edit_transaction.hpp"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>

using namespace goldcraft;
namespace {
auto pm = std::make_unique<playermove_t>();
model_t model{};
std::vector<mplane_t> planes;
std::array<std::vector<dclipnode_t>, 4> nodes;
int original_calls = 0, other_hits = 0, filtered_world = 0, predicates = 0, callback_runs = 0,
    query_throws = 0;
bool include_other = false, throw_original = false;
float queried_start[3]{}, queried_end[3]{};
pmtrace_t result{};
constexpr std::array<int, 4> indices{1, 3, 0, 2};
int contents(hull_t *hull, int node, float *p) {
    while (node >= 0) {
        auto &b = hull->clipnodes[node];
        auto &plane = hull->planes[b.planenum];
        const float distance = plane.type < 3 ? p[plane.type] - plane.dist
                                              : p[0] * plane.normal[0] + p[1] * plane.normal[1] +
                                                    p[2] * plane.normal[2] - plane.dist;
        node = b.children[distance >= 0 ? 0 : 1];
    }
    return node;
}
pmtrace_t clear(float *end) {
    pmtrace_t tr{};
    tr.ent = -1;
    tr.fraction = 1;
    std::copy_n(end, 3, tr.endpos);
    return tr;
}
pmtrace_t dispatch(physent_t *entities, int count, float *end, int flags, int index,
                   int (*ignore)(physent_t *)) {
    if (query_throws) {
        --query_throws;
        throw std::runtime_error("Native query fixture");
    }
    ++original_calls;
    auto tr = clear(end);
    bool skip = index == 0 || (ignore && ignore(&entities[0])) ||
                ((flags & PM_GLASS_IGNORE) && entities[0].rendermode);
    if (skip)
        ++filtered_world;
    // Deliberately recognizable sentinel for an unfiltered native world query.
    if (count > 0 && !skip) {
        tr.ent = 0;
        tr.fraction = .25f;
        tr.hitgroup = 77;
    }
    if (count > 1 && index != 1 && include_other && !(flags & PM_WORLD_ONLY) &&
        (!ignore || !ignore(&entities[1]))) {
        ++other_hits;
        if (tr.fraction > .6f) {
            tr.ent = 1;
            tr.fraction = .6f;
            tr.hitgroup = 7;
        }
    }
    return tr;
}
pmtrace_t base_ex(float *, float *end, int flags, int (*ignore)(physent_t *)) {
    return dispatch(pm->physents, pm->numphysent, end, flags, -1, ignore);
}
pmtrace_t base(float *a, float *b, int flags, int index) {
    return dispatch(pm->physents, pm->numphysent, b, flags, index, nullptr);
}
int base_position_ex(float *, pmtrace_t *tr, int (*ignore)(physent_t *)) {
    if (tr)
        *tr = base(pm->origin, pm->origin, PM_NORMAL, -1);
    if (!ignore || !ignore(&pm->physents[0]))
        return 0;
    return include_other && (!ignore || !ignore(&pm->physents[1])) ? 1 : -1;
}
int base_position(float *p, pmtrace_t *tr) { return base_position_ex(p, tr, nullptr); }
int base_point(float *p, int *truecontents) {
    int c = contents(&model.hulls[0], model.hulls[0].firstclipnode, p);
    if (truecontents)
        *truecontents = c;
    return c;
}
int base_true(float *p) { return base_point(p, nullptr); }
pmtrace_t *base_line_ex(float *, float *b, int flags, int hull, int (*ignore)(physent_t *)) {
    static pmtrace_t out;
    const int previous = pm->usehull;
    pm->usehull = hull;
    out = dispatch(flags ? pm->visents : pm->physents, flags ? pm->numvisent : pm->numphysent, b,
                   PM_NORMAL, -1, ignore);
    pm->usehull = previous;
    return &out;
}
pmtrace_t *base_line(float *a, float *b, int flags, int hull, int index) {
    static pmtrace_t out;
    const int previous = pm->usehull;
    pm->usehull = hull;
    if (flags == 0 || flags == 1)
        out = dispatch(flags ? pm->visents : pm->physents, flags ? pm->numvisent : pm->numphysent,
                       b, PM_NORMAL, index, nullptr);
    pm->usehull = previous;
    return &out;
}
int ignore_world(physent_t *p) {
    ++predicates;
    return p->info == 0;
}
int ignore_other(physent_t *p) {
    ++predicates;
    return p->info == 1;
}
void invoke(playermove_t *move, int) {
    ++callback_runs;
    if (throw_original)
        throw std::runtime_error("Original callback failure");
    result = move->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1);
}
void setup() {
    *pm = {};
    pm->numphysent = 2;
    pm->numvisent = 2;
    pm->physents[0].model = &model;
    pm->physents[0].solid = 4;
    pm->physents[1].info = 1;
    pm->physents[1].solid = 2;
    pm->visents[0] = pm->physents[0];
    pm->visents[1] = pm->physents[1];
    pm->PM_PlayerTrace = base;
    pm->PM_PlayerTraceEx = base_ex;
    pm->PM_TestPlayerPosition = base_position;
    pm->PM_TestPlayerPositionEx = base_position_ex;
    pm->PM_PointContents = base_point;
    pm->PM_TruePointContents = base_true;
    pm->PM_HullPointContents = contents;
    pm->PM_TraceLine = base_line;
    pm->PM_TraceLineEx = base_line_ex;
    for (int h = 0; h < 4; h++)
        for (int axis = 0; axis < 3; axis++) {
            pm->player_mins[h][axis] = model.hulls[indices[h]].clip_mins[axis];
            pm->player_maxs[h][axis] = model.hulls[indices[h]].clip_maxs[axis];
        }
    include_other = false;
}
void restored() {
    assert(pm->PM_PlayerTrace == base && pm->PM_PlayerTraceEx == base_ex);
    assert(pm->PM_TestPlayerPosition == base_position &&
           pm->PM_TestPlayerPositionEx == base_position_ex);
    assert(pm->PM_PointContents == base_point && pm->PM_TruePointContents == base_true);
    assert(pm->PM_TraceLine == base_line && pm->PM_TraceLineEx == base_line_ex);
}
edits::Snapshot snapshot(std::array<float, 3> low, std::array<float, 3> high) {
    return {9, 2, {{2, {}, {low, high}}}};
}
void prepare(const edits::Snapshot &state, model_t *loaded) {
    edits::Replica verified;
    verified.reset(state.epoch);
    verified.accept(state);
    client_map::commit(client_map::prepare(verified, loaded));
}
void run(float *a, float *b, int hull = 0, int server = 0) {
    std::copy_n(a, 3, queried_start);
    std::copy_n(b, 3, queried_end);
    pm->usehull = hull;
    client_map::move(pm.get(), server, invoke);
    restored();
}
void synthetic() {
    model = {};
    model.type = mod_brush;
    planes.resize(8);
    model.numplanes = 8;
    for (int h = 0; h < 4; h++) {
        int half = h == 0   ? 0
                   : h == 2 ? 32
                            : 16,
            height = h == 0   ? 0
                     : h == 1 ? 36
                     : h == 3 ? 18
                              : 32;
        planes[h * 2].normal[0] = 1;
        planes[h * 2].dist = float(-half);
        planes[h * 2 + 1].normal[0] = 1;
        planes[h * 2 + 1].dist = float(16 + half);
        nodes[h] = {{h * 2, {1, -1}}, {h * 2 + 1, {-1, -2}}};
        model.hulls[h] = {nodes[h].data(), planes.data(), 0, 1};
        model.hulls[h].clip_mins[0] = model.hulls[h].clip_mins[1] = float(-half);
        model.hulls[h].clip_mins[2] = float(-height);
        model.hulls[h].clip_maxs[0] = model.hulls[h].clip_maxs[1] = float(half);
        model.hulls[h].clip_maxs[2] = float(height);
    }
    setup();
    float a[3]{-64, 0, 0}, b[3]{64, 0, 0};
    client_map::reset();
    run(a, b);
    assert(result.hitgroup == 77);
    auto state = snapshot({-1, -64, -64}, {17, 64, 64});
    prepare(state, &model);
    for (int hull = 0; hull < 4; hull++) {
        run(a, b, hull);
        assert(result.fraction == 1);
    }
    run(a, b, 0, 1);
    assert(result.hitgroup == 77);
    include_other = true;
    run(a, b);
    assert(result.ent == 1 && result.hitgroup == 7 && result.fraction == .6f);
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        auto hit = m->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, ignore_other);
        assert(hit.fraction == 1);
        hit = m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, 0);
        assert(hit.ent == 1);
        hit = m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, 1);
        assert(hit.fraction == 1);
        hit = m->PM_PlayerTrace(queried_start, queried_end, PM_WORLD_ONLY, -1);
        assert(hit.fraction == 1);
        hit = m->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, ignore_world);
        assert(hit.ent == 1);
        hit = *m->PM_TraceLine(queried_start, queried_end, 0, 2, -1);
        assert(hit.ent == 1 && hit.hitgroup == 7);
        hit = *m->PM_TraceLineEx(queried_start, queried_end, 1, 2, ignore_other);
        assert(hit.fraction == 1);
        float p[3]{8, 0, 0};
        int actual = 0;
        assert(m->PM_PointContents(p, &actual) == -1 && actual == -1 &&
               m->PM_TruePointContents(p) == -1);
        assert(m->PM_TestPlayerPositionEx(p, nullptr, ignore_other) == -1);
        assert(m->PM_TestPlayerPosition(p, nullptr) == 1);
    });
    restored();
    include_other = false;
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        float p[3]{8, 0, 0};
        pmtrace_t t{};
        assert(m->PM_TestPlayerPosition(p, &t) == -1);
    });
    restored();
    auto bad = state;
    bad.revision = 3;
    bad.cuts[0].box.min[0] = NAN;
    try {
        prepare(bad, &model);
        assert(false);
    } catch (const std::exception &) {
    }
    run(a, b);
    assert(result.fraction == 1);
    float far_a[3]{-64, 200, 0}, far_b[3]{64, 200, 0};
    run(far_a, far_b);
    assert(result.hitgroup == 77);
    throw_original = true;
    try {
        run(a, b);
        assert(false);
    } catch (const std::runtime_error &) {
    }
    throw_original = false;
    restored();
    // Nested callbacks must restore the enclosing table and filter context.
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        auto outer = m->PM_PlayerTrace;
        client_map::move(m, 0, invoke);
        assert(result.fraction == 1 && m->PM_PlayerTrace == outer);
        client_map::move(m, 1, invoke);
        assert(result.hitgroup == 77 && m->PM_PlayerTrace == outer);
        result = m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1);
        assert(result.fraction == 1);
    });
    restored();
    prepare({9, 3, {}}, nullptr);
    run(a, b);
    assert(result.hitgroup == 77);
    client_map::reset();
    run(a, b);
    assert(result.hitgroup == 77);
    assert(filtered_world > 0 && other_hits > 0 && predicates > 0);
    std::cout << "Public PM callback dispatch, hull selection, unrelated entities, filters, "
                 "position/contents/line, transaction rollback and scope restoration passed\n";
}
void render_transaction() {
    struct Renderer final : IMetaRendererWorldEdit {
        std::uint64_t next = 0, ticket = 0, revision = 0, visible = 0;
        unsigned begins = 0, cancels = 0, resets = 0;
        MetaWorldEditStatus status = MetaWorldEditStatus::Preparing;
        bool commit_ok = false, begin_ok = true;
        uint64_t Begin(model_s *loaded, uint64_t epoch, uint64_t value,
                       const MetaWorldEditBox *boxes, uint32_t count) noexcept override {
            assert(loaded == &model && epoch && (!count || boxes));
            ++begins;
            revision = value;
            return ticket = begin_ok ? ++next : 0;
        }
        MetaWorldEditStatus Poll(uint64_t value) noexcept override {
            assert(value == ticket && value);
            return status;
        }
        bool Commit(uint64_t value) noexcept override {
            assert(value == ticket && value);
            if (!commit_ok)
                return false;
            visible = revision;
            ticket = 0;
            return true;
        }
        void Cancel(uint64_t value) noexcept override {
            assert(value == ticket);
            ticket = 0;
            ++cancels;
        }
        void Reset() noexcept override {
            visible = ticket = 0;
            ++resets;
        }
    } renderer;
    setup();
    client_map::EditTransaction transaction;
    transaction.reset(9);
    const auto cut = snapshot({-1, -64, -64}, {17, 64, 64});
    assert(transaction.accept(cut) == edits::Applied::changed);
    float a[3]{-64, 0, 0}, b[3]{64, 0, 0};
    transaction.pump(nullptr, &model);
    run(a, b);
    assert(result.hitgroup == 77 && transaction.pending());
    transaction.pump(&renderer, &model);
    run(a, b);
    assert(result.hitgroup == 77 && transaction.applied().revision == 0);
    renderer.status = MetaWorldEditStatus::Ready;
    transaction.pump(&renderer, &model);
    run(a, b);
    assert(result.hitgroup == 77 && renderer.visible == 0);
    renderer.commit_ok = true;
    transaction.pump(&renderer, &model);
    run(a, b);
    assert(result.fraction == 1 && renderer.visible == 2 && transaction.applied().revision == 2);
    assert(!transaction.pending());
    renderer.status = MetaWorldEditStatus::Preparing;
    assert(transaction.accept(edits::Delta{9, 2, 3, edits::Operation::clear, {}, {}}) ==
           edits::Applied::changed);
    transaction.pump(&renderer, &model);
    run(a, b);
    assert(result.fraction == 1 && renderer.visible == 2);
    renderer.status = MetaWorldEditStatus::Failed;
    try {
        transaction.pump(&renderer, &model);
        assert(false);
    } catch (const ProtocolError &) {
    }
    run(a, b);
    assert(result.fraction == 1 && !transaction.ready() && transaction.applied().revision == 2);
    assert(transaction.accept(edits::Snapshot{9, 3, {}}) == edits::Applied::changed);
    renderer.status = MetaWorldEditStatus::Ready;
    transaction.pump(&renderer, &model);
    run(a, b);
    assert(result.hitgroup == 77 && renderer.visible == 3 && transaction.applied().revision == 3);
    renderer.status = MetaWorldEditStatus::Preparing;
    assert(transaction.accept(edits::Delta{9, 3, 4, edits::Operation::add, {}, cut.cuts[0].box}) ==
           edits::Applied::changed);
    transaction.pump(&renderer, &model);
    const auto superseded = renderer.ticket;
    assert(transaction.accept(
               edits::Delta{9, 4, 5, edits::Operation::add, {}, {{-1, 100, -64}, {17, 200, 64}}}) ==
           edits::Applied::changed);
    assert(renderer.ticket == 0 && renderer.cancels == 2);
    transaction.pump(&renderer, &model);
    assert(renderer.ticket != superseded && transaction.state().cuts.size() == 2);
    run(a, b);
    assert(result.hitgroup == 77);
    renderer.status = MetaWorldEditStatus::Ready;
    transaction.pump(&renderer, &model);
    run(a, b);
    assert(result.fraction == 1 && renderer.visible == 5 && transaction.applied().revision == 5);
    assert(transaction.accept(cut) == edits::Applied::ignored);
    assert(transaction.accept(edits::Delta{9, 6, 7, edits::Operation::clear, {}, {}}) ==
           edits::Applied::need_snapshot);
    run(a, b);
    assert(result.fraction == 1 && transaction.applied().revision == 5);
    transaction.reset(10);
    run(a, b);
    assert(result.hitgroup == 77 && renderer.visible == 0 && renderer.resets == 1);
    auto dynamic = cut;
    dynamic.epoch = 10;
    dynamic.cuts[0].target = {3, 1, 2};
    transaction.accept(dynamic);
    const auto begins = renderer.begins;
    try {
        transaction.pump(&renderer, &model);
        assert(false);
    } catch (const ProtocolError &) {
    }
    assert(renderer.begins == begins && !transaction.ready());
    transaction.reset(0);
    std::cout << "{\"renderCollisionTransaction\":true,\"waitKeepsOldCollision\":true,"
                 "\"failedCommitKeepsOld\":true,\"coalescesDeltas\":true,\"failureResync\":true,"
                 "\"restore\":true,\"dynamicRejectsAtomically\":true,\"passed\":true}\n";
}
int nested_predicate(physent_t *entity) {
    if (entity->info != 1)
        return 0;
    client_map::move(pm.get(), 0, invoke);
    assert(result.ent == 1 && result.fraction == .6f);
    return 1;
}
int restoring_predicate(physent_t *entity) {
    if (entity->info != 1)
        return 0;
    client_map::reset();
    return 1;
}
std::string status() {
    std::ostringstream out;
    client_map::write_status(out);
    return out.str();
}
void lifecycle() {
    setup();
    client_map::reset();
    auto state = snapshot({-1, -64, -64}, {17, 64, 64});
    prepare(state, &model);
    float a[3]{-64, 0, 0}, b[3]{64, 0, 0};
    run(a, b);
    // Invalid geometry cannot replace a previously prepared valid revision.
    auto newer = state;
    newer.revision = 3;
    model.type = mod_studio;
    try {
        prepare(newer, &model);
        assert(false);
    } catch (const std::exception &) {
    }
    model.type = mod_brush;
    run(a, b);
    assert(result.fraction == 1);
    try {
        prepare(newer, nullptr);
        assert(false);
    } catch (const std::exception &) {
    }
    model_t different = model;
    try {
        prepare(newer, &different);
        assert(false);
    } catch (const std::exception &) {
    }
    auto conflict = state;
    conflict.cuts[0].box.max[1] = 63;
    try {
        prepare(conflict, &model);
        assert(false);
    } catch (const std::exception &) {
    }
    auto foreign = state;
    foreign.epoch = 10;
    try {
        prepare(foreign, &model);
        assert(false);
    } catch (const std::exception &) {
    }
    edits::Replica unready;
    unready.reset(9);
    try {
        client_map::prepare(unready, &model);
        assert(false);
    } catch (const std::exception &) {
    }
    prepare(state, &model);
    run(a, b);
    assert(result.fraction == 1);
    assert(status().find("\"mapCollisionFailures\":0") != std::string::npos);
    // Each required callback must be present before installing the whole adapter.
    for (int missing = 0; missing < 9; ++missing) {
        setup();
        switch (missing) {
        case 0:
            pm->PM_PlayerTrace = nullptr;
            break;
        case 1:
            pm->PM_PlayerTraceEx = nullptr;
            break;
        case 2:
            pm->PM_TestPlayerPosition = nullptr;
            break;
        case 3:
            pm->PM_TestPlayerPositionEx = nullptr;
            break;
        case 4:
            pm->PM_PointContents = nullptr;
            break;
        case 5:
            pm->PM_TruePointContents = nullptr;
            break;
        case 6:
            pm->PM_TraceLine = nullptr;
            break;
        case 7:
            pm->PM_TraceLineEx = nullptr;
            break;
        case 8:
            pm->PM_HullPointContents = nullptr;
            break;
        }
        client_map::move(pm.get(), 0, [](playermove_t *m, int) {
            assert(m->PM_PlayerTrace == base || m->PM_PlayerTrace == nullptr);
            assert(m->PM_TraceLine == base_line || m->PM_TraceLine == nullptr);
        });
    }
    setup();
    for (int count : {0, MAX_PHYSENTS + 1}) {
        pm->numphysent = count;
        run(a, b);
        assert(result.hitgroup == 77 || count == 0);
    }
    setup();
    pm->physents[0].info = 7;
    run(a, b);
    assert(result.hitgroup == 77);
    setup();
    pm->physents[0].model = &different;
    run(a, b);
    assert(result.hitgroup == 77);
    setup();
    pm->physents[0].angles[1] = 90;
    run(a, b);
    assert(result.hitgroup == 77);
    setup();
    include_other = true;
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        auto hit = m->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, nested_predicate);
        assert(hit.fraction == 1);
        hit = m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1);
        assert(hit.ent == 1 && hit.fraction == .6f);
        float inside[3]{8, 0, 0};
        pmtrace_t out{};
        assert(m->PM_TestPlayerPositionEx(inside, &out, ignore_other) == -1);
        assert(out.ent == 1 &&
               out.fraction == .6f); // Output does not inherit the position predicate.
        assert(m->PM_TestPlayerPositionEx(inside, &out, ignore_world) == 1);
        assert(out.ent == 1 && out.fraction == .6f);
    });
    restored();
    setup();
    // World restoration/reset during a callback retires its immutable cache safely.
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        float outside[3]{64, 200, 0};
        pmtrace_t out{};
        assert(m->PM_TestPlayerPosition(outside, &out) == 0);
        assert(out.fraction == 1 && out.ent == -1);
        assert(m->PM_TestPlayerPositionEx(outside, &out, nullptr) == 0);
        assert(out.fraction == 1 && out.ent == -1);
    });
    restored();
    include_other = true;
    pm->physents[0].rendermode = 1;
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        const auto hit = m->PM_PlayerTrace(queried_start, queried_end, PM_GLASS_IGNORE, -1);
        assert(hit.ent == 1 && hit.fraction == .6f);
    });
    restored();
    setup();
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        client_map::reset();
        client_map::move(m, 0, invoke);
        assert(result.hitgroup == 77);
        assert(m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1).hitgroup == 77);
    });
    restored();
    prepare(state, &model);
    model_t fluid = model;
    pm->physents[1].model = &fluid;
    pm->physents[1].solid = 0;
    pm->physents[1].skin = -9;
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        float inside[3]{8, 0, 0};
        int raw = 0;
        assert(m->PM_PointContents(inside, &raw) == -9 && raw == -1);
        assert(m->PM_TruePointContents(inside) == -1);
    });
    restored();
    setup();
    newer.cuts.push_back({3, {1, 55, 1}, {{0, 0, 0}, {1, 1, 1}}});
    prepare(newer, &model);
    assert(status().find("\"mapCollisionDeferredTargets\":1") != std::string::npos);
    run(a, b);
    assert(result.fraction == 1);
    prepare({9, 4, {}}, nullptr);
    try {
        prepare(state, &model);
        assert(false);
    } catch (const std::exception &) {
    }
    run(a, b);
    assert(result.hitgroup == 77);
    client_map::reset();
    prepare(state, &model);
    // Invalid query coordinates and exceptions inside filtered native queries
    // return the original unfiltered result and leave the enclosing filter intact.
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        float bad[3]{NAN, 0, 0};
        pmtrace_t out{};
        int raw = 0;
        assert(m->PM_PlayerTrace(bad, queried_end, PM_NORMAL, -1).hitgroup == 77);
        assert(m->PM_PlayerTraceEx(bad, queried_end, PM_NORMAL, nullptr).hitgroup == 77);
        assert(m->PM_TestPlayerPosition(bad, &out) == 0);
        assert(m->PM_TestPlayerPositionEx(bad, &out, nullptr) == 0);
        m->PM_PointContents(bad, &raw);
        m->PM_TruePointContents(bad);
        assert(m->PM_TraceLine(bad, queried_end, 0, 2, -1)->hitgroup == 77);
        assert(m->PM_TraceLineEx(bad, queried_end, 1, 2, nullptr)->hitgroup == 77);
        query_throws = 1;
        assert(m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1).hitgroup == 77);
        assert(m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1).fraction == 1);
        query_throws = 2;
        const auto blocked = m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1);
        assert(blocked.startsolid && blocked.allsolid && blocked.fraction == 0);
        assert(m->PM_PlayerTrace(queried_start, queried_end, PM_NORMAL, -1).fraction == 1);
    });
    restored();
    assert(status().find("\"mapCollisionFailures\":11") != std::string::npos);
    include_other = true;
    client_map::move(pm.get(), 0, [](playermove_t *m, int) {
        const auto hit =
            m->PM_PlayerTraceEx(queried_start, queried_end, PM_NORMAL, restoring_predicate);
        assert(hit.ent == 0 && hit.hitgroup == 77);
    });
    restored();
    assert(status().find("\"mapCollisionFailures\":12") != std::string::npos);
    client_map::reset();
    std::cout << "Validated revision/session/model rejection, atomic geometry failure, nine "
                 "missing APIs, world identity, nested filtering, contents, restoration and twelve "
                 "query failures recovered\n";
}
struct Bsp {
    std::vector<std::uint8_t> bytes;
    std::array<std::size_t, 15> offset{}, length{};
    explicit Bsp(const char *path) {
        std::ifstream file(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(file), {});
        assert(bytes.size() >= 124 && u32(0) == 30 && crc32(bytes) == 0xf6725c06);
        for (int i = 0; i < 15; i++) {
            offset[i] = u32(4 + i * 8);
            length[i] = u32(8 + i * 8);
            assert(offset[i] + length[i] <= bytes.size());
        }
    }
    unsigned short u16(std::size_t p) const {
        assert(p + 2 <= bytes.size());
        return bytes[p] | (bytes[p + 1] << 8);
    }
    unsigned u32(std::size_t p) const { return u16(p) | (unsigned(u16(p + 2)) << 16); }
    float f32(std::size_t p) const { return std::bit_cast<float>(u32(p)); }
    void load() {
        model = {};
        model.type = mod_brush;
        planes.clear();
        for (auto &n : nodes)
            n.clear();
        for (std::size_t at = offset[1]; at < offset[1] + length[1]; at += 20) {
            mplane_t p{};
            for (int i = 0; i < 3; i++)
                p.normal[i] = f32(at + i * 4);
            p.dist = f32(at + 12);
            p.type = byte(u32(at + 16));
            planes.push_back(p);
        }
        for (std::size_t at = offset[5]; at < offset[5] + length[5]; at += 24) {
            dclipnode_t n{};
            n.planenum = int(u32(at));
            for (int s = 0; s < 2; s++) {
                const auto child = std::bit_cast<short>(u16(at + 4 + s * 2));
                n.children[s] =
                    child >= 0 ? child : short(int(u32(offset[10] + std::size_t(-1 - child) * 28)));
            }
            nodes[0].push_back(n);
        }
        for (std::size_t at = offset[9]; at < offset[9] + length[9]; at += 8)
            nodes[1].push_back(
                {int(u32(at)),
                 {std::bit_cast<short>(u16(at + 4)), std::bit_cast<short>(u16(at + 6))}});
        nodes[2] = nodes[1];
        nodes[3] = nodes[1];
        model.numplanes = int(planes.size());
        for (int h = 0; h < 4; h++) {
            model.hulls[h] = {nodes[h].data(), planes.data(), int(u32(offset[14] + 36 + h * 4)),
                              int(nodes[h].size() - 1)};
            const int half = h == 0   ? 0
                             : h == 2 ? 32
                                      : 16,
                      height = h == 0   ? 0
                               : h == 1 ? 36
                               : h == 3 ? 18
                                        : 32;
            model.hulls[h].clip_mins[0] = model.hulls[h].clip_mins[1] = float(-half);
            model.hulls[h].clip_mins[2] = float(-height);
            model.hulls[h].clip_maxs[0] = model.hulls[h].clip_maxs[1] = float(half);
            model.hulls[h].clip_maxs[2] = float(height);
        }
    }
};
void assault(const char *path) {
    Bsp bsp(path);
    bsp.load();
    setup();
    auto state = snapshot({64, 2864, 358.4f}, {96, 2960, 454.4f});
    state.cuts.push_back({3, {}, {{96, 2864, 358.4f}, {128, 2960, 454.4f}}});
    state.revision = 3;
    prepare(state, &model);
    float a[3]{96, 2784, 406.4f}, b[3]{96, 2976, 406.4f};
    for (int h : {0, 1, 2}) {
        run(a, b, h);
        assert(result.fraction == 1);
    }
    a[0] = b[0] = 120;
    run(a, b);
    assert(result.fraction < 1 && result.ent == 0);
    a[0] = b[0] = 96;
    for (auto &cut : state.cuts) {
        cut.box.min[2] = 384.4f;
        cut.box.max[2] = 428.4f;
    }
    state.revision = 4;
    prepare(state, &model);
    run(a, b, 1);
    assert(result.fraction == 1);
    run(a, b);
    assert(result.fraction < 1);
    state.cuts = {
        {5,
         {},
         {{-2069.544912f, 1641.130686f, 585.474396f}, {-1877.544912f, 1737.130686f, 681.474396f}}}};
    state.revision = 5;
    prepare(state, &model);
    float c[3]{-2021.544912f, 1689.130686f, 633.474396f},
        d[3]{-1925.544912f, 1689.130686f, 633.474396f};
    run(c, d, 2);
    assert(result.fraction == 1);
    run(c, d);
    assert(result.fraction < 1 && std::abs(result.endpos[0] + 1936.03125f) < .001f);
    prepare({9, 6, {}}, nullptr);
    run(a, b);
    assert(result.hitgroup == 77);
    std::cout << "Actual cs_assault PM adapter: standing/duck/point, rim, low-hole and clip-only "
                 "contact passed\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc == 2)
            assault(argv[1]);
        else {
            synthetic();
            render_transaction();
            lifecycle();
        }
        std::ostringstream out;
        client_map::write_status(out);
        std::cout << "{" << out.str() << "}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
