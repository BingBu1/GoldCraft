// HLSDK model/PM headers require MetaHook base types first.
#include <metahook.h>
#include <com_model.h>
#include <pm_defs.h>
#include <event_api.h>
#include <cl_entity.h>

#include "map_collision.hpp"
#include "goldcraft/edited_hull.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace goldcraft::client_map {
using namespace carving;
struct BrushTarget {
    edits::Target identity;
    model_t *model = nullptr;
    std::array<std::unique_ptr<EditedHull>, 4> hulls;
    std::array<std::array<float, 3>, 4> clip_mins;
};
struct Prepared {
    model_t *model = nullptr;
    edits::Snapshot state;
    std::array<std::unique_ptr<EditedHull>, 4> hulls;
    std::array<Point, 4> clip_mins;
    std::vector<BrushTarget> targets;
    std::vector<int> slots;
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
brush::Receiver identities;
std::uint64_t traces = 0, positions = 0, points = 0, frames = 0, failures = 0;
std::uint64_t event_traces = 0;
playermove_t *event_movement = nullptr;
event_api_t native_events{}, client_events{};
decltype(cl_enginefunc_t::CL_LoadModel) load_model = nullptr;
decltype(cl_enginefunc_t::GetEntityByIndex) entity_by_index = nullptr;
decltype(cl_enginefunc_t::pfnAngleVectors) angle_vectors = nullptr;
decltype(cl_enginefunc_t::hudGetModelByIndex) model_by_index = nullptr;
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
    if (active != current->geometry || !current->geometry->hulls[0] ||
        count <= 0 || count > MAX_PHYSENTS ||
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
// Native invokes predicates in array order, before glass/solidity tests. Collect
// edits at that exact point; pre-scanning would change callback side effects.
struct BrushQuery {
    physent_t *entities;
    int count, usehull, flags, ignored;
    const float *start, *end;
    int (*predicate)(physent_t *);
    bool position_query = false;
    std::uint64_t publication = identities.publication();
    std::array<unsigned short, MAX_PHYSENTS> consumed;
    std::size_t consumed_count = 0;
    pmtrace_t best{};
    BrushQuery *previous = nullptr;

    BrushQuery(physent_t *array, int size, int hull, int mask, int skip,
               const float *a, const float *b, int (*callback)(physent_t *), bool position = false)
        : entities(array), count(size), usehull(hull), flags(mask), ignored(skip), start(a),
          end(b), predicate(callback), position_query(position) {
        best.fraction = 1;
        best.ent = -1;
    }
    void unchanged() const {
        if (active != current->geometry || identities.publication() != publication)
            throw std::runtime_error("Client brush query generation changed");
    }
    const BrushTarget *target(const physent_t &pe) const {
        const auto &geometry = *current->geometry;
        if (pe.info <= 0 || std::size_t(pe.info) >= geometry.slots.size() ||
            pe.solid != 4 || !entity_by_index || !model_by_index)
            return nullptr;
        const int index = geometry.slots[pe.info];
        if (index < 0) return nullptr;
        const auto &entry = geometry.targets[index];
        if (pe.model != entry.model) return nullptr;
        auto *entity = entity_by_index(pe.info);
        if (!entity || entity->curstate.number != pe.info || entity->model != entry.model ||
            entity->curstate.solid != 4 || entity->curstate.modelindex <= 0 ||
            model_by_index(entity->curstate.modelindex) != entry.model ||
            !identities.matches({entry.identity.slot, entry.identity.serial, entry.identity.model},
                                std::uint32_t(entity->curstate.messagenum),
                                geometry.state.epoch, geometry.state.revision))
            return nullptr;
        for (int axis = 0; axis < 3; ++axis)
            if (!std::isfinite(pe.origin[axis]) || !std::isfinite(pe.angles[axis]))
                return nullptr;
        return &entry;
    }
    struct Transform {
        std::array<float, 3> offset, forward{}, right{}, up{};
        bool rotated;
        Transform(const BrushTarget &target, const physent_t &pe, int hull) {
            for (int i = 0; i < 3; ++i)
                offset[i] = pe.origin[i] +
                    (target.clip_mins[hull_index[hull]][i] - current->pm->player_mins[hull][i]);
            rotated = pe.angles[0] != 0 || pe.angles[1] != 0 || pe.angles[2] != 0;
            if (rotated) {
                if (!angle_vectors) throw std::runtime_error("Client brush angle callback missing");
                angle_vectors(pe.angles, forward.data(), right.data(), up.data());
            }
        }
        Point local(const float *p) const {
            if (!p) throw std::invalid_argument("Client brush point missing");
            const std::array<float, 3> relative{p[0] - offset[0], p[1] - offset[1], p[2] - offset[2]};
            if (!rotated) return {relative[0], relative[1], relative[2]};
            const auto dot = [&](const auto &axis) {
                return (axis[0] * relative[0] + axis[1] * relative[1]) + axis[2] * relative[2];
            };
            return {dot(forward), -dot(right), dot(up)};
        }
        void normal(float *n) const {
            if (!rotated) return;
            const std::array<float, 3> local{n[0], n[1], n[2]};
            for (int i = 0; i < 3; ++i)
                n[i] = (forward[i] * local[0] - right[i] * local[1]) + up[i] * local[2];
        }
    };
    static bool earlier(const pmtrace_t &a, const pmtrace_t &b) {
        return a.fraction < 1 &&
            (a.fraction < b.fraction || (a.fraction == b.fraction && a.ent < b.ent));
    }
    int visit(physent_t *pe) {
        const auto offset = reinterpret_cast<std::uintptr_t>(pe) -
                            reinterpret_cast<std::uintptr_t>(entities);
        if (offset % sizeof(physent_t) || offset / sizeof(physent_t) >= std::size_t(count))
            throw std::runtime_error("Client brush native filter array");
        const int index = int(offset / sizeof(physent_t));
        if (index == ignored || (predicate && predicate(pe))) return 1;
        unchanged();
        if ((flags & PM_GLASS_IGNORE) && pe->rendermode) return 0;
        if (count <= 0 || count > MAX_PHYSENTS || usehull < 0 || usehull >= 4) return 0;
        pmtrace_t cut{};
        if (index == 0 && affects(world(entities, count), usehull, start, end)) {
            if (position_query) {
                ++positions;
                return current->geometry->hulls[hull_index[usehull]]->contents(
                    client_map::local(start, *pe, usehull)) != -2;
            }
            cut = overlay(pe, usehull, start, end);
        } else {
            const auto *entry = target(*pe);
            unchanged();
            if (!entry) return 0;
            Transform transform(*entry, *pe, usehull);
            unchanged();
            const auto a = transform.local(start), b = transform.local(end);
            const auto &hull = *entry->hulls[hull_index[usehull]];
            if (!hull.affects(a, b)) return 0;
            if (consumed_count == consumed.size())
                throw std::runtime_error("Client brush native filter repeated");
            consumed[consumed_count++] = static_cast<unsigned short>(index);
            if (position_query) {
                ++positions;
                return hull.contents(a) != -2;
            }
            const auto value = hull.trace(a, b);
            cut.ent = index;
            cut.fraction = value.start_solid ? 0 : static_cast<float>(value.fraction);
            cut.startsolid = value.start_solid;
            cut.allsolid = value.all_solid;
            cut.inopen = value.in_open;
            cut.inwater = value.in_water;
            // Native rotates the normal, but retains the model-local plane distance.
            cut.plane.dist = static_cast<float>(value.plane.distance);
            for (int i = 0; i < 3; ++i) {
                cut.plane.normal[i] = static_cast<float>(value.plane.normal[i]);
                cut.endpos[i] = start[i] + cut.fraction * (end[i] - start[i]);
            }
            transform.normal(cut.plane.normal);
            ++traces;
        }
        if (earlier(cut, best)) best = cut;
        return 1;
    }
    void validate() const {
        unchanged();
        for (std::size_t i = 0; i < consumed_count; ++i)
            if (!target(entities[consumed[i]]))
                throw std::runtime_error("Client brush identity changed during query");
        unchanged();
    }
    pmtrace_t finish(const pmtrace_t &native) const {
        validate();
        return earlier(best, native) ? best : native;
    }
};
thread_local BrushQuery *brush_query = nullptr;
struct BrushScope {
    explicit BrushScope(BrushQuery &query) { query.previous = std::exchange(brush_query, &query); }
    ~BrushScope() { brush_query = brush_query->previous; }
};
int brush_ignore(physent_t *entity) { return brush_query->visit(entity); }
pmtrace_t brush_trace(float *start, float *end, int flags, int ignored,
                     int (*predicate)(physent_t *)) {
    BrushQuery query(current->pm->physents, current->pm->numphysent, current->pm->usehull,
                     flags, ignored, start, end, predicate);
    BrushScope scope(query);
    return query.finish(current->native.trace_ex(start, end, flags, brush_ignore));
}
pmtrace_t trace(float *start, float *end, int flags, int ignored) {
    const auto native = [&] { return current->native.trace(start, end, flags, ignored); };
    return guarded(
        [&] {
            if (!current->geometry->targets.empty())
                return brush_trace(start, end, flags, ignored, nullptr);
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
            if (predicate || !current->geometry->targets.empty())
                return brush_trace(start, end, flags, -1, predicate);
            auto *pe = world(current->pm->physents, current->pm->numphysent);
            if (!affects(pe, current->pm->usehull, start, end) ||
                ((flags & PM_GLASS_IGNORE) && pe->rendermode))
                return native();
            auto cut = overlay(pe, current->pm->usehull, start, end);
            FilterScope scope({pe, nullptr, predicate});
            return closest(cut, current->native.trace_ex(start, end, flags, ignore));
        },
        native, [&] { return blocked(start); });
}
int test_position(float *p, pmtrace_t *result, int (*predicate)(physent_t *)) {
    if (predicate || !current->geometry->targets.empty()) {
        // Native reports an unfiltered trace at PM origin before visiting predicates.
        if (result) *result = trace(current->pm->origin, current->pm->origin, PM_NORMAL, -1);
        BrushQuery query(current->pm->physents, current->pm->numphysent, current->pm->usehull,
                         PM_NORMAL, -1, p, p, predicate, true);
        BrushScope scope(query);
        const int hit = current->native.position_ex(p, nullptr, brush_ignore);
        query.validate();
        return hit;
    }
    auto *pe = world(current->pm->physents, current->pm->numphysent);
    if (!affects(pe, current->pm->usehull, p, p)) {
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
            if (!current->geometry->targets.empty()) return test_position(p, result, nullptr);
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
struct LineHullScope {
    playermove_t &pm;
    int saved;
    explicit LineHullScope(playermove_t &movement) : pm(movement), saved(movement.usehull) {}
    ~LineHullScope() { pm.usehull = saved; }
};
pmtrace_t *native_line_ex(float *start, float *end, int flags, int usehull,
                        int (*predicate)(physent_t *)) {
    // Native restores usehull only on normal return. Restore before fallback
    // too when a filtering callback unwinds through the engine.
    LineHullScope scope(*current->pm);
    return current->native.line_ex(start, end, flags, usehull, predicate);
}
pmtrace_t *native_line(float *start, float *end, int flags, int usehull, int ignored) {
    LineHullScope scope(*current->pm);
    return current->native.line(start, end, flags, usehull, ignored);
}
pmtrace_t *test_line(float *start, float *end, int flags, int usehull, int ignored,
                     int (*predicate)(physent_t *)) {
    auto *entities = flags ? current->pm->visents : current->pm->physents;
    const int count = flags ? current->pm->numvisent : current->pm->numphysent;
    if (predicate || !current->geometry->targets.empty()) {
        BrushQuery query(entities, count, usehull, PM_NORMAL, ignored, start, end, predicate);
        BrushScope scope(query);
        const auto *native = native_line_ex(start, end, flags, usehull, brush_ignore);
        if (!native) throw std::runtime_error("Client native brush line result");
        current->line_result = query.finish(*native);
        return &current->line_result;
    }
    auto *pe = world(entities, count);
    auto cut = overlay(pe, usehull, start, end);
    FilterScope scope(
        {pe, ignored >= 0 && ignored < count ? &entities[ignored] : nullptr, predicate});
    const auto *base = native_line_ex(start, end, flags, usehull, ignore);
    if (!base)
        throw std::runtime_error("Client native line result");
    current->line_result = closest(cut, *base);
    return &current->line_result;
}
pmtrace_t *line_ex(float *start, float *end, int flags, int usehull,
                   int (*predicate)(physent_t *)) {
    return guarded(
        [&] {
            if (predicate || !current->geometry->targets.empty())
                return test_line(start, end, flags, usehull, -1, predicate);
            auto *entities = flags ? current->pm->visents : current->pm->physents;
            const int count = flags ? current->pm->numvisent : current->pm->numphysent;
            auto *pe = world(entities, count);
            if (!affects(pe, usehull, start, end))
                return native_line_ex(start, end, flags, usehull, predicate);
            return test_line(start, end, flags, usehull, -1, predicate);
        },
        [&] { return native_line_ex(start, end, flags, usehull, predicate); },
        [&] {
            current->line_result = blocked(start);
            return &current->line_result;
        });
}
pmtrace_t *line(float *start, float *end, int flags, int usehull, int ignored) {
    return guarded(
        [&] {
            if ((flags == 0 || flags == 1) && !current->geometry->targets.empty())
                return test_line(start, end, flags, usehull, ignored, nullptr);
            auto *entities = flags ? current->pm->visents : current->pm->physents;
            const int count = flags ? current->pm->numvisent : current->pm->numphysent;
            auto *pe = world(entities, count);
            if ((flags != 0 && flags != 1) || ignored == 0 || !affects(pe, usehull, start, end))
                return native_line(start, end, flags, usehull, ignored);
            return test_line(start, end, flags, usehull, ignored, nullptr);
        },
        [&] { return native_line(start, end, flags, usehull, ignored); },
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
void event_trace(float *start, float *end, int flags, int ignored, pmtrace_t *result) {
    const auto native = [&] {
        pmtrace_t out{};
        native_events.EV_PlayerTrace(start, end, flags, ignored, &out);
        return out;
    };
    // This table belongs to the client; the engine may temporarily select a
    // different PM context. Check the public accessor before reading our PM.
    if (!result || !active || (!active->hulls[0] && active->targets.empty()) || !event_movement) {
        native_events.EV_PlayerTrace(start, end, flags, ignored, result);
        return;
    }
    *result = guarded(
        [&] {
            auto *pm = event_movement;
            if (native_events.EV_GetPhysent(0) != &pm->physents[0])
                return native();
            Callbacks callbacks(*pm);
            for (auto *enclosing = current; enclosing; enclosing = enclosing->previous)
                if (enclosing->pm == pm)
                    unwrap(callbacks, enclosing->native);
            if (!callbacks.trace_ex)
                return native();
            Context context{pm, callbacks, active, current};
            struct Restore {
                Context *previous;
                ~Restore() { current = previous; }
            } restore{current};
            current = &context;
            if (!context.geometry->targets.empty()) {
                const auto out = brush_trace(start, end, flags, ignored, nullptr);
                if (event_movement != pm || native_events.EV_GetPhysent(0) != &pm->physents[0])
                    throw std::runtime_error("Client event brush PM changed during query");
                ++event_traces;
                return out;
            }
            auto *pe = world(pm->physents, pm->numphysent);
            if (ignored == 0 || !affects(pe, pm->usehull, start, end) ||
                ((flags & PM_GLASS_IGNORE) && pe->rendermode))
                return native();
            const auto cut = overlay(pe, pm->usehull, start, end);
            FilterScope scope(
                {pe, ignored >= 0 && ignored < pm->numphysent ? &pm->physents[ignored] : nullptr,
                 nullptr});
            const auto other = callbacks.trace_ex(start, end, flags, ignore);
            if (event_movement != pm || native_events.EV_GetPhysent(0) != pe)
                throw std::runtime_error("Client event PM changed during query");
            const auto out = closest(cut, other);
            ++event_traces;
            return out;
        },
        native, [&] { return blocked(start); });
}
} // namespace
void engine_initialized(const cl_enginefuncs_s *engine) noexcept {
    load_model = engine ? engine->CL_LoadModel : nullptr;
    entity_by_index = engine ? engine->GetEntityByIndex : nullptr;
    angle_vectors = engine ? engine->pfnAngleVectors : nullptr;
    model_by_index = engine ? engine->hudGetModelByIndex : nullptr;
}
event_api_t *event_api(event_api_t *source) {
    event_movement = nullptr;
    // Another plugin may copy the proxy table before a second Initialize.
    // Never save our own function as the native fallback (it would recurse).
    if (source && source->EV_PlayerTrace == event_trace)
        return source;
    if (!source || source->version != EVENT_API_VERSION || !source->EV_GetPhysent ||
        !source->EV_PlayerTrace)
        return source;
    native_events = client_events = *source;
    client_events.EV_PlayerTrace = event_trace;
    return &client_events;
}
void movement_initialized(playermove_t *movement) noexcept { event_movement = movement; }
void shutdown_events() noexcept {
    event_movement = nullptr;
    engine_initialized(nullptr);
}
brush::Receiver &brush_identities() noexcept { return identities; }
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
    std::map<std::uint32_t, std::vector<Box>> brush_cuts;
    for (const auto &cut : state.cuts) {
        Box box{};
        for (int i = 0; i < 3; ++i) {
            box.min[i] = cut.box.min[i];
            box.max[i] = cut.box.max[i];
        }
        if (!cut.target.slot) {
            cuts.push_back(box);
            continue;
        }
        if (!brush::valid({cut.target.slot, cut.target.serial, cut.target.model}) ||
            !loaded || loaded->numsubmodels <= 0 || cut.target.model >= unsigned(loaded->numsubmodels) ||
            !load_model || !entity_by_index || !model_by_index || !angle_vectors)
            throw ProtocolError("Client brush model callbacks/identity unavailable");
        if (next->slots.size() <= cut.target.slot) next->slots.resize(cut.target.slot + 1, -1);
        auto &index = next->slots[cut.target.slot];
        if (index < 0) {
            BrushTarget entry;
            entry.identity = cut.target;
            std::array<char, 16> name{};
            name[0] = '*';
            const auto encoded = std::to_chars(name.data() + 1, name.data() + name.size() - 1,
                                               cut.target.model);
            if (encoded.ec != std::errc{}) throw ProtocolError("Client brush model name");
            int model_index = 0;
            entry.model = load_model(name.data(), &model_index);
            if (!entry.model || entry.model->type != mod_brush || model_index <= 0 ||
                model_by_index(model_index) != entry.model ||
                !std::equal(name.data(), encoded.ptr + 1, entry.model->name))
                throw ProtocolError("Client brush inline model unavailable");
            index = static_cast<int>(next->targets.size());
            next->targets.push_back(std::move(entry));
        } else if (next->targets[index].identity != cut.target)
            throw ProtocolError("Client brush slot has mixed identities");
        brush_cuts[cut.target.slot].push_back(box);
    }
    std::size_t work = 0;
    if (!cuts.empty()) {
        if (!loaded)
            throw ProtocolError("Client world model not loaded");
        const SourceHull original(loaded, loaded->hulls[0]);
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
    for (auto &entry : next->targets) {
        const SourceHull original(entry.model, entry.model->hulls[0]);
        for (int i = 0; i < 4; ++i) {
            const auto &hull = entry.model->hulls[i];
            const SourceHull body_hull(entry.model, hull);
            Box body{};
            for (int axis = 0; axis < 3; ++axis) {
                entry.clip_mins[i][axis] = hull.clip_mins[axis];
                if (i) {
                    body.min[axis] = hull.clip_mins[axis];
                    body.max[axis] = hull.clip_maxs[axis];
                }
            }
            entry.hulls[i] = std::make_unique<EditedHull>(original.view(), body_hull.view(), body,
                                                        brush_cuts.at(entry.identity.slot));
            work += entry.hulls[i]->operations();
            if (work > 32'000'000) throw std::length_error("Client map physics budget");
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
    if (!server && active && (active->hulls[0] || !active->targets.empty()) &&
        native.complete() && pm->PM_HullPointContents) {
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
        << ",\"mapCollisionEventTraces\":" << event_traces
        << ",\"mapCollisionBrushTargets\":" << (active ? active->targets.size() : 0)
        << ",\"mapCollisionDeferredTargets\":0";
}
} // namespace goldcraft::client_map
