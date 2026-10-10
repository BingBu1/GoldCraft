// HLSDK model/PM headers require MetaHook base types first.
#include <metahook.h>
#include <com_model.h>
#include <pm_defs.h>

#include "map_collision.hpp"
#include "goldcraft/edited_hull.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace goldcraft::client_map {
using namespace carving;
struct Prepared {
    model_t *model = nullptr;
    edits::Snapshot state;
    std::array<std::unique_ptr<EditedHull>, 4> hulls;
    std::array<Point, 4> clip_mins;
    std::size_t deferred_targets = 0;
};
namespace {
static_assert(sizeof(model_t) == 392 && sizeof(hull_t) == 40 && sizeof(physent_t) == 224);
constexpr std::array<int, 4> hull_index{1, 3, 0, 2};
struct SourceHull {
    std::vector<Plane> planes;
    std::vector<HullNode> nodes;
    int root;
    SourceHull(model_t *model, const hull_t &hull) : root(hull.firstclipnode) {
        if (model->type != mod_brush || model->numplanes < 1 || model->numplanes > 262144 ||
            hull.lastclipnode < 0 || hull.lastclipnode >= 262144 || !hull.planes || !hull.clipnodes)
            throw std::invalid_argument("Client map hull bounds");
        planes.reserve(model->numplanes);
        nodes.reserve(hull.lastclipnode + 1);
        for (int i = 0; i < model->numplanes; ++i) {
            const auto &p = hull.planes[i];
            Plane plane{{}, p.dist};
            if (p.type < 3)
                plane.normal[p.type] = 1;
            else
                for (int axis = 0; axis < 3; ++axis)
                    plane.normal[axis] = p.normal[axis];
            planes.push_back(plane);
        }
        for (int i = 0; i <= hull.lastclipnode; ++i) {
            const auto &n = hull.clipnodes[i];
            nodes.push_back({static_cast<unsigned>(n.planenum), {n.children[0], n.children[1]}});
        }
    }
    HullView view() const { return {planes, nodes, root}; }
};
std::shared_ptr<const Prepared> active;
std::uint64_t traces = 0, positions = 0, points = 0, frames = 0, failures = 0;
struct Callbacks {
    decltype(playermove_t::PM_PlayerTrace) trace;
    decltype(playermove_t::PM_PlayerTraceEx) trace_ex;
    decltype(playermove_t::PM_TestPlayerPosition) position;
    decltype(playermove_t::PM_TestPlayerPositionEx) position_ex;
    decltype(playermove_t::PM_PointContents) point;
    decltype(playermove_t::PM_TruePointContents) true_point;
    decltype(playermove_t::PM_TraceLine) line;
    decltype(playermove_t::PM_TraceLineEx) line_ex;
    explicit Callbacks(const playermove_t &pm)
        : trace(pm.PM_PlayerTrace), trace_ex(pm.PM_PlayerTraceEx),
          position(pm.PM_TestPlayerPosition), position_ex(pm.PM_TestPlayerPositionEx),
          point(pm.PM_PointContents), true_point(pm.PM_TruePointContents), line(pm.PM_TraceLine),
          line_ex(pm.PM_TraceLineEx) {}
    void apply(playermove_t &pm) const {
        pm.PM_PlayerTrace = trace;
        pm.PM_PlayerTraceEx = trace_ex;
        pm.PM_TestPlayerPosition = position;
        pm.PM_TestPlayerPositionEx = position_ex;
        pm.PM_PointContents = point;
        pm.PM_TruePointContents = true_point;
        pm.PM_TraceLine = line;
        pm.PM_TraceLineEx = line_ex;
    }
    bool complete() const {
        return trace && trace_ex && position && position_ex && point && true_point && line &&
               line_ex;
    }
};
struct Context {
    playermove_t *pm;
    Callbacks native;
    std::shared_ptr<const Prepared> geometry;
    Context *previous;
    pmtrace_t line_result{};
};
thread_local Context *current = nullptr;
struct Filter {
    physent_t *world;
    physent_t *ignored;
    int (*predicate)(physent_t *);
};
thread_local Filter *filter = nullptr;
int ignore(physent_t *pe) {
    return pe == filter->world || pe == filter->ignored ||
           (filter->predicate && filter->predicate(pe));
}
struct FilterScope {
    Filter value;
    Filter *previous;
    explicit FilterScope(Filter next) : value(next), previous(std::exchange(filter, &value)) {}
    ~FilterScope() { filter = previous; }
};
// Overlay failures use the saved native query after unwinding its world filter.
template <class Query, class Fallback, class Failed>
auto guarded(Query query, Fallback fallback, Failed failed) {
    try {
        return query();
    } catch (...) {
        ++failures;
        try {
            return fallback();
        } catch (...) {
            ++failures;
            return failed();
        }
    }
}
pmtrace_t blocked(const float *start) {
    pmtrace_t out{};
    out.ent = 0;
    out.startsolid = out.allsolid = 1;
    if (start)
        for (int i = 0; i < 3; ++i)
            out.endpos[i] = start[i];
    return out;
}
Point point(const float *p) {
    if (!p)
        throw std::invalid_argument("Client map query point");
    return {p[0], p[1], p[2]};
}
physent_t *world(physent_t *entities, int count) {
    if (active != current->geometry || count <= 0 || count > MAX_PHYSENTS ||
        entities[0].info != 0 || entities[0].model != current->geometry->model ||
        entities[0].solid != 4)
        return nullptr;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(entities[0].origin[i]) || entities[0].angles[i] != 0)
            return nullptr;
    return entities;
}
Point local(const float *p, const physent_t &pe, int usehull) {
    auto out = point(p);
    const auto &h = current->geometry->clip_mins[hull_index.at(usehull)];
    for (int i = 0; i < 3; ++i)
        out[i] -= pe.origin[i] + h[i] - current->pm->player_mins[usehull][i];
    return out;
}
bool affects(physent_t *pe, int usehull, const float *start, const float *end) {
    return pe && usehull >= 0 && usehull < 4 &&
           current->geometry->hulls[hull_index[usehull]]->affects(local(start, *pe, usehull),
                                                                  local(end, *pe, usehull));
}
pmtrace_t overlay(physent_t *pe, int usehull, const float *start, const float *end) {
    const auto value = current->geometry->hulls[hull_index.at(usehull)]->trace(
        local(start, *pe, usehull), local(end, *pe, usehull));
    pmtrace_t out{};
    out.ent = 0;
    out.fraction = value.start_solid ? 0 : static_cast<float>(value.fraction);
    out.startsolid = value.start_solid;
    out.allsolid = value.all_solid;
    out.inopen = value.in_open;
    out.inwater = value.in_water;
    out.plane.dist = static_cast<float>(value.plane.distance);
    for (int i = 0; i < 3; ++i) {
        out.plane.normal[i] = static_cast<float>(value.plane.normal[i]);
        out.endpos[i] = start[i] + out.fraction * (end[i] - start[i]);
    }
    ++traces;
    return out;
}
pmtrace_t closest(const pmtrace_t &world_trace, const pmtrace_t &native_trace) {
    if (active != current->geometry)
        throw std::runtime_error("Client map changed during query");
    // Native PM visits the world first and replaces only on a strictly nearer hit.
    return world_trace.fraction < 1 && world_trace.fraction <= native_trace.fraction ? world_trace
                                                                                     : native_trace;
}
pmtrace_t trace(float *start, float *end, int flags, int ignored) {
    const auto native = [&] { return current->native.trace(start, end, flags, ignored); };
    return guarded(
        [&] {
            auto *pe = world(current->pm->physents, current->pm->numphysent);
            if (ignored == 0 || !affects(pe, current->pm->usehull, start, end) ||
                ((flags & PM_GLASS_IGNORE) && pe->rendermode))
                return native();
            auto cut = overlay(pe, current->pm->usehull, start, end);
            FilterScope scope({pe,
                               ignored >= 0 && ignored < current->pm->numphysent
                                   ? &current->pm->physents[ignored]
                                   : nullptr,
                               nullptr});
            return closest(cut, current->native.trace_ex(start, end, flags, ignore));
        },
        native, [&] { return blocked(start); });
}
pmtrace_t trace_ex(float *start, float *end, int flags, int (*predicate)(physent_t *)) {
    const auto native = [&] { return current->native.trace_ex(start, end, flags, predicate); };
    return guarded(
        [&] {
            auto *pe = world(current->pm->physents, current->pm->numphysent);
            if (!affects(pe, current->pm->usehull, start, end) ||
                ((flags & PM_GLASS_IGNORE) && pe->rendermode) || (predicate && predicate(pe)))
                return native();
            auto cut = overlay(pe, current->pm->usehull, start, end);
            FilterScope scope({pe, nullptr, predicate});
            return closest(cut, current->native.trace_ex(start, end, flags, ignore));
        },
        native, [&] { return blocked(start); });
}
int test_position(float *p, pmtrace_t *result, int (*predicate)(physent_t *)) {
    auto *pe = world(current->pm->physents, current->pm->numphysent);
    if (!affects(pe, current->pm->usehull, p, p) || (predicate && predicate(pe))) {
        const int hit = current->native.position_ex(p, result, predicate);
        if (result && affects(pe, current->pm->usehull, current->pm->origin, current->pm->origin))
            *result = trace(current->pm->origin, current->pm->origin, PM_NORMAL, -1);
        return hit;
    }
    const int content = current->geometry->hulls[hull_index.at(current->pm->usehull)]->contents(
        local(p, *pe, current->pm->usehull));
    // GoldSrc reports this unfiltered origin trace even for an Ex position query.
    if (result)
        *result = trace(current->pm->origin, current->pm->origin, PM_NORMAL, -1);
    ++positions;
    if (content == -2)
        return 0;
    FilterScope scope({pe, nullptr, predicate});
    const int hit = current->native.position_ex(p, nullptr, ignore);
    if (active != current->geometry)
        throw std::runtime_error("Client map changed during position query");
    return hit;
}
int position_ex(float *p, pmtrace_t *result, int (*predicate)(physent_t *)) {
    return guarded([&] { return test_position(p, result, predicate); },
                   [&] { return current->native.position_ex(p, result, predicate); },
                   [&] {
                       if (result)
                           *result = blocked(p);
                       return 0;
                   });
}
int position(float *p, pmtrace_t *result) {
    return guarded(
        [&] {
            auto *pe = world(current->pm->physents, current->pm->numphysent);
            if (!affects(pe, current->pm->usehull, p, p)) {
                const int hit = current->native.position(p, result);
                if (result &&
                    affects(pe, current->pm->usehull, current->pm->origin, current->pm->origin))
                    *result = trace(current->pm->origin, current->pm->origin, PM_NORMAL, -1);
                return hit;
            }
            return test_position(p, result, nullptr);
        },
        [&] { return current->native.position(p, result); },
        [&] {
            if (result)
                *result = blocked(p);
            return 0;
        });
}
int point_contents(float *p, int *truecontents) {
    const auto native = [&] { return current->native.point(p, truecontents); };
    return guarded(
        [&] {
            auto *pe = world(current->pm->physents, current->pm->numphysent);
            if (!pe || !current->geometry->hulls[0]->affects(point(p), point(p)))
                return native();
            const int content = current->geometry->hulls[0]->contents(point(p));
            if (content != -1)
                return native();
            if (truecontents)
                *truecontents = content;
            for (int i = 1; i < current->pm->numphysent; ++i) {
                auto &ent = current->pm->physents[i];
                if (ent.model && ent.solid == 0) {
                    float local_point[3];
                    for (int a = 0; a < 3; ++a)
                        local_point[a] = p[a] - ent.origin[a];
                    auto &hull = ent.model->hulls[0];
                    if (current->pm->PM_HullPointContents(&hull, hull.firstclipnode, local_point) !=
                        -1) {
                        ++points;
                        return ent.skin;
                    }
                }
            }
            ++points;
            return content;
        },
        native,
        [&] {
            if (truecontents)
                *truecontents = -2;
            return -2;
        });
}
int true_contents(float *p) {
    const auto native = [&] { return current->native.true_point(p); };
    return guarded(
        [&] {
            auto *pe = world(current->pm->physents, current->pm->numphysent);
            if (!pe || !current->geometry->hulls[0]->affects(point(p), point(p)))
                return native();
            ++points;
            return current->geometry->hulls[0]->contents(point(p));
        },
        native, [] { return -2; });
}
pmtrace_t *test_line(float *start, float *end, int flags, int usehull, int ignored,
                     int (*predicate)(physent_t *)) {
    auto *entities = flags ? current->pm->visents : current->pm->physents;
    const int count = flags ? current->pm->numvisent : current->pm->numphysent;
    auto *pe = world(entities, count);
    auto cut = overlay(pe, usehull, start, end);
    FilterScope scope(
        {pe, ignored >= 0 && ignored < count ? &entities[ignored] : nullptr, predicate});
    const auto *base = current->native.line_ex(start, end, flags, usehull, ignore);
    if (!base)
        throw std::runtime_error("Client native line result");
    current->line_result = closest(cut, *base);
    return &current->line_result;
}
pmtrace_t *line_ex(float *start, float *end, int flags, int usehull,
                   int (*predicate)(physent_t *)) {
    return guarded(
        [&] {
            auto *entities = flags ? current->pm->visents : current->pm->physents;
            const int count = flags ? current->pm->numvisent : current->pm->numphysent;
            auto *pe = world(entities, count);
            if (!affects(pe, usehull, start, end) || (predicate && predicate(pe)))
                return current->native.line_ex(start, end, flags, usehull, predicate);
            return test_line(start, end, flags, usehull, -1, predicate);
        },
        [&] { return current->native.line_ex(start, end, flags, usehull, predicate); },
        [&] {
            current->line_result = blocked(start);
            return &current->line_result;
        });
}
pmtrace_t *line(float *start, float *end, int flags, int usehull, int ignored) {
    return guarded(
        [&] {
            auto *entities = flags ? current->pm->visents : current->pm->physents;
            const int count = flags ? current->pm->numvisent : current->pm->numphysent;
            auto *pe = world(entities, count);
            if ((flags != 0 && flags != 1) || ignored == 0 || !affects(pe, usehull, start, end))
                return current->native.line(start, end, flags, usehull, ignored);
            return test_line(start, end, flags, usehull, ignored, nullptr);
        },
        [&] { return current->native.line(start, end, flags, usehull, ignored); },
        [&] {
            current->line_result = blocked(start);
            return &current->line_result;
        });
}
void unwrap(Callbacks &saved, const Callbacks &native) {
    if (saved.trace == trace)
        saved.trace = native.trace;
    if (saved.trace_ex == trace_ex)
        saved.trace_ex = native.trace_ex;
    if (saved.position == position)
        saved.position = native.position;
    if (saved.position_ex == position_ex)
        saved.position_ex = native.position_ex;
    if (saved.point == point_contents)
        saved.point = native.point;
    if (saved.true_point == true_contents)
        saved.true_point = native.true_point;
    if (saved.line == line)
        saved.line = native.line;
    if (saved.line_ex == line_ex)
        saved.line_ex = native.line_ex;
}
} // namespace
void reset() { active.reset(); }
Candidate prepare(const edits::Replica &replica, model_t *loaded) {
    if (!replica.ready())
        throw ProtocolError("Client map physics needs a validated snapshot");
    const auto &state = replica.state();
    if (active) {
        if (state.epoch != active->state.epoch || state.revision < active->state.revision ||
            (loaded && active->model && loaded != active->model))
            throw ProtocolError("Client map physics session/revision/model");
        if (state.revision == active->state.revision) {
            if (state != active->state)
                throw ProtocolError("Client map physics revision conflict");
            return active;
        }
    }
    auto next = std::make_shared<Prepared>();
    next->model = loaded;
    next->state = state;
    std::vector<Box> cuts;
    for (const auto &cut : state.cuts) {
        if (cut.target.slot) {
            ++next->deferred_targets;
            continue;
        }
        Box box{};
        for (int i = 0; i < 3; ++i) {
            box.min[i] = cut.box.min[i];
            box.max[i] = cut.box.max[i];
        }
        cuts.push_back(box);
    }
    if (!cuts.empty()) {
        if (!loaded)
            throw ProtocolError("Client world model not loaded");
        const SourceHull original(loaded, loaded->hulls[0]);
        std::size_t work = 0;
        for (int i = 0; i < 4; ++i) {
            const SourceHull body_hull(loaded, loaded->hulls[i]);
            Box body{};
            for (int axis = 0; axis < 3; ++axis) {
                next->clip_mins[i][axis] = loaded->hulls[i].clip_mins[axis];
                if (i) {
                    body.min[axis] = loaded->hulls[i].clip_mins[axis];
                    body.max[axis] = loaded->hulls[i].clip_maxs[axis];
                }
            }
            next->hulls[i] =
                std::make_unique<EditedHull>(original.view(), body_hull.view(), body, cuts);
            work += next->hulls[i]->operations();
            if (work > 32'000'000)
                throw std::length_error("Client map physics budget");
        }
    }
    return next;
}
void commit(Candidate candidate) noexcept { active = std::move(candidate); }
void move(playermove_t *pm, int server, void (*original)(playermove_t *, int)) {
    if (!original)
        return;
    if (!pm) {
        original(pm, server);
        return;
    }
    const Callbacks observed(*pm);
    auto native = observed;
    for (auto *enclosing = current; enclosing; enclosing = enclosing->previous)
        if (enclosing->pm == pm)
            unwrap(native, enclosing->native);
    Context context{pm, native, active, current};
    struct Restore {
        playermove_t &pm;
        const Callbacks &saved;
        Context *previous;
        ~Restore() {
            saved.apply(pm);
            current = previous;
        }
    } restore{*pm, observed, current};
    // Nested server movement temporarily uses the native table; client movement
    // unwraps its enclosing table instead of recursively saving our own wrappers.
    native.apply(*pm);
    current = &context;
    if (!server && active && active->hulls[0] && native.complete() && pm->PM_HullPointContents) {
        pm->PM_PlayerTrace = trace;
        pm->PM_PlayerTraceEx = trace_ex;
        pm->PM_TestPlayerPosition = position;
        pm->PM_TestPlayerPositionEx = position_ex;
        pm->PM_PointContents = point_contents;
        pm->PM_TruePointContents = true_contents;
        pm->PM_TraceLine = line;
        pm->PM_TraceLineEx = line_ex;
        ++frames;
    }
    original(pm, server);
}
void write_status(std::ostream &out) {
    out << "\"mapCollisionEpoch\":" << (active ? active->state.epoch : 0)
        << ",\"mapCollisionRevision\":" << (active ? active->state.revision : 0)
        << ",\"mapCollisionFrames\":" << frames << ",\"mapCollisionTraces\":" << traces
        << ",\"mapCollisionPositions\":" << positions << ",\"mapCollisionPoints\":" << points
        << ",\"mapCollisionFailures\":" << failures
        << ",\"mapCollisionDeferredTargets\":" << (active ? active->deferred_targets : 0);
}
} // namespace goldcraft::client_map
