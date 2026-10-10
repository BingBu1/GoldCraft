#include "precompiled.h"
#include "map_physics.h"
#include "map_visibility.h"
#include "goldcraft/edited_hull.hpp"
#include "goldcraft/host_map_api.hpp"
#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>

namespace {
using namespace goldcraft;
using namespace goldcraft::carving;
struct SourceHull {
    std::vector<Plane> planes;
    std::vector<HullNode> nodes;
    int root;
    SourceHull(model_t *model, const hull_t &hull) : root(hull.firstclipnode) {
        if (model->numplanes < 0 || model->numplanes > 262144 || hull.lastclipnode >= 262144 ||
            !hull.planes || !hull.clipnodes)
            throw std::invalid_argument("Invalid engine BSP hull");
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
            const auto &node = hull.clipnodes[i];
            nodes.push_back(
                {static_cast<std::uint32_t>(node.planenum), {node.children[0], node.children[1]}});
        }
    }
    HullView view() const { return {planes, nodes, root}; }
};
struct Target {
    int serial;
    model_t *model;
    std::uint32_t inline_model;
    std::array<std::unique_ptr<EditedHull>, 4> hulls;
};
class MapPhysics final : public IHostMapPhysics {
  public:
    bool Replace(std::uint64_t epoch, std::uint64_t revision, const HostMapCut *cuts,
                 std::uint32_t count) override {
        try {
            if (!epoch || epoch != stats.epoch || revision <= stats.revision || count > 65536 ||
                (!cuts && count))
                throw std::invalid_argument("Map physics session/revision/count");
            std::map<int, std::vector<Box>> groups;
            std::map<int, Target> next;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto &cut = cuts[i];
                if (cut.slot >= static_cast<unsigned>(g_psv.num_edicts))
                    throw std::invalid_argument("Map physics target slot");
                auto *entity = &g_psv.edicts[cut.slot];
                auto *model = Mod_Handle(entity->v.modelindex);
                if (entity->free || (entity->v.flags & FL_KILLME) || model == nullptr ||
                    model->type != mod_brush || entity->v.solid != SOLID_BSP ||
                    (cut.slot && (static_cast<unsigned>(entity->serialnumber) != cut.serial ||
                                  cut.model == 0)) ||
                    (!cut.slot && (cut.serial || cut.model || model != g_psv.worldmodel)))
                    throw std::invalid_argument("Map physics target identity");
                if (cut.slot) {
                    const char *name = STRING(entity->v.model);
                    char *end = nullptr;
                    if (!name || name[0] != '*' || std::strtoul(name + 1, &end, 10) != cut.model ||
                        !end || *end)
                        throw std::invalid_argument("Map physics inline model");
                }
                auto [entry, inserted] = next.try_emplace(static_cast<int>(cut.slot));
                if (inserted) {
                    entry->second.serial = entity->serialnumber;
                    entry->second.model = model;
                    entry->second.inline_model = cut.model;
                } else if (entry->second.inline_model != cut.model)
                    throw std::invalid_argument("Mixed map physics identity");
                Box box{};
                for (int axis = 0; axis < 3; ++axis) {
                    box.min[axis] = cut.min[axis];
                    box.max[axis] = cut.max[axis];
                }
                groups[static_cast<int>(cut.slot)].push_back(box);
            }
            std::size_t total_work = 0;
            for (auto &[slot, target] : next) {
                const SourceHull point(target.model, target.model->hulls[0]);
                for (int i = 0; i < 4; ++i) {
                    const auto &native = target.model->hulls[i];
                    const SourceHull body_hull(target.model, native);
                    Box body{};
                    if (i)
                        for (int axis = 0; axis < 3; ++axis) {
                            body.min[axis] = native.clip_mins[axis];
                            body.max[axis] = native.clip_maxs[axis];
                        }
                    target.hulls[i] = std::make_unique<EditedHull>(point.view(), body_hull.view(),
                                                                   body, groups.at(slot));
                    total_work += target.hulls[i]->operations();
                    if (total_work > 32'000'000)
                        throw std::length_error("Map physics transaction budget");
                }
            }
            auto visible = engine_map::prepare_visibility(g_psv.worldmodel,
                groups.contains(0) ? std::span<const Box>(groups.at(0)) : std::span<const Box>{});
            targets.swap(next);
            engine_map::commit_visibility(std::move(visible));
            stats.revision = revision;
            stats.targets = static_cast<std::uint32_t>(targets.size());
            error.clear();
            return true;
        } catch (const std::exception &failure) {
            error = failure.what();
            ++stats.failures;
            return false;
        }
    }
    void Reset(std::uint64_t epoch, std::uint64_t revision) override {
        engine_map::commit_visibility({});
        targets.clear();
        // Restoration is a revision barrier in the same map session.
        if (stats.epoch != epoch)
            stats = {epoch, revision, 0, 0, 0, 0};
        else {
            stats.revision = revision;
            stats.targets = 0;
        }
        error.clear();
    }
    const char *LastError() const override { return error.c_str(); }
    HostMapStats Stats() const override { return stats; }
    const EditedHull *find(int slot, model_t *model, hull_t *hull) const {
        const auto entry = targets.find(slot);
        if (entry == targets.end() || slot < 0 || slot >= g_psv.num_edicts)
            return nullptr;
        const auto *entity = &g_psv.edicts[slot];
        const auto &target = entry->second;
        if (entity->free || (entity->v.flags & FL_KILLME) || entity->v.solid != SOLID_BSP ||
            entity->serialnumber != target.serial || model != target.model ||
            Mod_Handle(entity->v.modelindex) != model)
            return nullptr;
        for (int i = 0; i < 4; ++i)
            if (hull == &model->hulls[i])
                return target.hulls[i].get();
        return nullptr;
    }
    template <class Trace>
    bool trace(int slot, model_t *model, hull_t *hull, const float *start, const float *end,
               Trace *result) {
        const auto *edit = find(slot, model, hull);
        if (!edit)
            return false;
        try {
            const Point a{start[0], start[1], start[2]}, b{end[0], end[1], end[2]};
            if (!edit->affects(a, b))
                return false;
            const auto value = edit->trace(a, b);
            result->fraction = static_cast<float>(value.fraction);
            result->startsolid = value.start_solid;
            result->allsolid = value.all_solid;
            result->inopen = value.in_open;
            result->inwater = value.in_water;
            for (int axis = 0; axis < 3; ++axis)
                result->plane.normal[axis] = static_cast<float>(value.plane.normal[axis]);
            result->plane.dist = static_cast<float>(value.plane.distance);
            // PM_TraceModel consumes the local endpoint directly; the other
            // callers subsequently transform/recompute it in world space.
            if (result->fraction != 1.0f)
                for (int axis = 0; axis < 3; ++axis)
                    result->endpos[axis] =
                        start[axis] + result->fraction * (end[axis] - start[axis]);
            ++stats.traces;
            return true;
        } catch (const std::exception &failure) {
            error = failure.what();
            ++stats.failures;
            return false;
        }
    }
    bool contents(int slot, model_t *model, hull_t *hull, const float *point, int &result) {
        const auto *edit = find(slot, model, hull);
        if (!edit)
            return false;
        try {
            const Point p{point[0], point[1], point[2]};
            if (!edit->affects(p, p))
                return false;
            result = edit->contents(p);
            ++stats.points;
            return true;
        } catch (const std::exception &failure) {
            error = failure.what();
            ++stats.failures;
            return false;
        }
    }

  private:
    std::map<int, Target> targets;
    HostMapStats stats{};
    std::string error;
};
MapPhysics physics;
} // namespace
using goldcraft::IHostMapPhysics;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(MapPhysics, IHostMapPhysics, goldcraft::host_map_api_version,
                                  physics);
bool GoldCraft_MapTrace(int slot, model_t *model, hull_t *hull, const float *start,
                        const float *end, trace_t *trace) {
    return physics.trace(slot, model, hull, start, end, trace);
}
bool GoldCraft_MapTrace(int slot, model_t *model, hull_t *hull, const float *start,
                        const float *end, pmtrace_t *trace) {
    return physics.trace(slot, model, hull, start, end, trace);
}
bool GoldCraft_MapContents(int slot, model_t *model, hull_t *hull, const float *point,
                           int &result) {
    return physics.contents(slot, model, hull, point, result);
}
void GoldCraft_MapPhysicsReset() { physics.Reset(0, 0); }
