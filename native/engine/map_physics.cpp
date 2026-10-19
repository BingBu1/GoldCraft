#include "precompiled.h"
#include "map_physics.h"
#include "map_visibility.h"
#include "goldcraft/edited_hull.hpp"
#include "goldcraft/collision_source.hpp"
#include "goldcraft/host_map_api.hpp"
#include "goldcraft/host_map_sample_api.hpp"
#include "goldcraft/host_delivery_api.hpp"
#include "goldcraft/map_edit_delivery.hpp"
#include "goldcraft/mining_cell.hpp"
#include "goldcraft/brush_identity.hpp"
#include <array>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>

namespace {
using namespace goldcraft;
using namespace goldcraft::carving;
struct SourceHull : CollisionSource::Hull {
    SourceHull(model_t *model, const hull_t &hull) {
        root = hull.firstclipnode;
        if (model->numplanes < 0 || model->numplanes > 262144 || hull.lastclipnode >= 262144 ||
            !hull.planes || !hull.clipnodes)
            throw std::invalid_argument("Invalid engine BSP hull");
        planes.reserve(model->numplanes);
        if (hull.lastclipnode >= 0) nodes.reserve(hull.lastclipnode + 1);
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
};
struct SourceModel : CollisionSource {
    SourceModel(model_t* model, const std::vector<Box>& input) {
        hulls = {SourceHull(model,model->hulls[0]),SourceHull(model,model->hulls[1]),
                 SourceHull(model,model->hulls[2]),SourceHull(model,model->hulls[3])};
        cuts = input;
        for (int h = 1; h < 4; ++h)
            for (int axis = 0; axis < 3; ++axis) {
                bodies[h].min[axis] = model->hulls[h].clip_mins[axis];
                bodies[h].max[axis] = model->hulls[h].clip_maxs[axis];
            }
    }
};
struct Target {
    int serial;
    model_t *model;
    std::uint32_t inline_model;
    std::array<std::shared_ptr<const EditedHull>, 4> hulls;
    std::shared_ptr<const SourceModel> source;
};
model_t *TargetModel(std::uint32_t slot, std::uint32_t serial, std::uint32_t inline_model) {
    if (slot >= static_cast<unsigned>(g_psv.num_edicts))
        throw std::invalid_argument("Map physics target slot");
    const auto *entity = &g_psv.edicts[slot];
    auto *model = Mod_Handle(entity->v.modelindex);
    if (entity->free || (entity->v.flags & FL_KILLME) || !model || model->type != mod_brush ||
        (slot && (static_cast<unsigned>(entity->serialnumber) != serial || !inline_model)) ||
        (!slot && (serial || inline_model || model != g_psv.worldmodel || entity->v.solid != SOLID_BSP)))
        throw std::invalid_argument("Map physics target identity");
    if (slot) {
        const char *name = STRING(entity->v.model);
        char *end = nullptr;
        if (!name || name[0] != '*' || !name[1] ||
            std::strtoul(name + 1, &end, 10) != inline_model || !end || *end)
            throw std::invalid_argument("Map physics inline model");
    }
    return model;
}
class MapPhysics final : public IHostMapPhysics, public IHostMapSample {
  public:
    bool Replace(std::uint64_t epoch, std::uint64_t revision, const HostMapCut *cuts,
                 std::uint32_t count) override {
        try {
            if (!epoch || epoch != stats.epoch || revision <= stats.revision || count > 65536 ||
                (!cuts && count))
                throw std::invalid_argument("Map physics session/revision/count");
            struct CutGroup {
                std::uint32_t serial{}, model{};
                std::vector<Box> boxes;
            };
            std::map<std::uint32_t, CutGroup> groups;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto &cut = cuts[i];
                if (cut.slot >= static_cast<unsigned>(g_psv.num_edicts))
                    throw std::invalid_argument("Map physics target slot");
                auto [entry, inserted] = groups.try_emplace(cut.slot);
                auto &group = entry->second;
                if (inserted) {
                    group.serial = cut.serial;
                    group.model = cut.model;
                } else if (group.serial != cut.serial || group.model != cut.model)
                    throw std::invalid_argument("Mixed map physics identity");
                Box box{};
                for (int axis = 0; axis < 3; ++axis) {
                    box.min[axis] = cut.min[axis];
                    box.max[axis] = cut.max[axis];
                }
                group.boxes.push_back(box);
            }
            // Replace runs synchronously on the engine thread. Grouping and
            // collision preparation do not invoke game/plugin callbacks, so
            // validate each requested target once, never across replacements.
            std::map<int, Target> next;
            for (const auto &[slot, group] : groups) {
                auto *model = TargetModel(slot, group.serial, group.model);
                auto &target = next[static_cast<int>(slot)];
                target.serial = g_psv.edicts[slot].serialnumber;
                target.model = model;
                target.inline_model = group.model;
            }
            std::size_t total_work = 0, source_bytes = 0;
            for (auto &[slot, target] : next) {
                std::shared_ptr<const SourceModel> source = std::make_shared<SourceModel>(target.model,groups.at(slot).boxes);
                const auto previous = targets.find(slot);
                if (previous != targets.end()) {
                    const auto& old = previous->second;
                    // TargetModel was checked for this group in this replacement.
                    // A reused pointer or matching cut list alone is insufficient.
                    if (old.serial == target.serial && old.model == target.model &&
                        old.inline_model == target.inline_model && old.source && source->same(*old.source)) {
                        target.hulls = old.hulls;
                        source = old.source;
                    }
                }
                std::optional<CompactedCuts> compact;
                for (int i = 0; i < 4; ++i) {
                    if (!target.hulls[i]) {
                        if (!compact) compact.emplace(source->cuts);
                        target.hulls[i] = std::make_shared<EditedHull>(source->hulls[0].view(),
                            source->hulls[i].view(), source->bodies[i], *compact);
                    }
                    total_work += target.hulls[i]->operations();
                    if (total_work > 32'000'000)
                        throw std::length_error("Map physics transaction budget");
                }
                constexpr std::size_t cache_limit = 16 * 1024 * 1024;
                const auto bytes = source->retained_bytes();
                if (bytes <= cache_limit - source_bytes) {
                    target.source = std::move(source);
                    source_bytes += bytes;
                }
            }
            auto visible = engine_map::prepare_visibility(g_psv.worldmodel,
                groups.contains(0) ? std::span<const Box>(groups.at(0).boxes) : std::span<const Box>{});
            targets.swap(next);
            has_brushes = targets.size() > (targets.contains(0) ? 1u : 0u);
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
        has_brushes = false;
        identity_message = 0;
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
    bool Sample(std::uint64_t epoch, std::uint64_t revision, std::uint32_t slot,
                std::uint32_t serial, std::uint32_t inline_model, const float *start,
                const float *end, HostMapCell &cell) override {
        try {
            if (!epoch || epoch != stats.epoch || revision != stats.revision || !start || !end)
                throw std::invalid_argument("Map sample session/revision/ray");
            auto *model = TargetModel(slot, serial, inline_model);
            auto *entity = &g_psv.edicts[slot];
            if (entity->v.solid != SOLID_BSP)
                throw std::invalid_argument("Map sample inactive brush");
            vec3_t offset, local_start, local_end;
            // This is SV_SingleClipMoveToEntity's native point-hull transform.
            // The GameDLL sees rotated normals but an unchanged local distance;
            // tracing here preserves the exact local plane at grid boundaries.
            auto *hull = SV_HullForBsp(entity, vec3_origin, vec3_origin, offset);
            VectorSubtract(start, offset, local_start);
            VectorSubtract(end, offset, local_end);
            if (!VectorIsZero(entity->v.angles)) {
                vec3_t forward, right, up, temp;
                AngleVectors(entity->v.angles, forward, right, up);
                VectorCopy(local_start, temp);
                local_start[0] = _DotProduct(temp, forward);
                local_start[1] = -_DotProduct(temp, right);
                local_start[2] = _DotProduct(temp, up);
                VectorCopy(local_end, temp);
                local_end[0] = _DotProduct(temp, forward);
                local_end[1] = -_DotProduct(temp, right);
                local_end[2] = _DotProduct(temp, up);
            }
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(local_start[axis]) || !std::isfinite(local_end[axis]))
                    throw std::invalid_argument("Map sample nonfinite ray");
            trace_t native{};
            native.fraction = 1.0f;
            native.allsolid = TRUE;
            if (!trace(static_cast<int>(slot), model, hull, local_start, local_end, &native))
                SV_RecursiveHullCheck(hull, hull->firstclipnode, 0.0f, 1.0f,
                                      local_start, local_end, &native);
            if (native.startsolid || native.allsolid || native.fraction >= 1.0f)
                throw std::invalid_argument("Map sample has no surface");
            const auto selected = mining::surface_cell(
                {local_start[0], local_start[1], local_start[2]},
                {local_end[0], local_end[1], local_end[2]},
                {native.plane.normal[0], native.plane.normal[1], native.plane.normal[2]},
                native.plane.dist);
            cell = {selected.x, selected.y, selected.z};
            return true;
        } catch (const std::exception &failure) {
            error = failure.what();
            ++stats.failures;
            return false;
        }
    }
    void identities(client_t *client, packet_entities_t *pack, sizebuf_t *message) {
        if (!has_brushes || !client || client->fakeclient || !client->fully_connected ||
            !pack || pack->num_entities < 0 || pack->num_entities > int(brush::max_entities) ||
            (pack->num_entities && !pack->entities) || !message || !message->data ||
            message->cursize < 0 || message->cursize > message->maxsize ||
            (message->flags & SIZEBUF_OVERFLOWED) ||
            Q_strcmp(Info_ValueForKey(client->userinfo, brush::capability), brush::capability_value))
            return;
        if (!identity_message) {
            for (auto *registered = sv_gpUserMsgs; registered; registered = registered->next)
                if (registered->iSize == -1 && !Q_strcmp(registered->szName, brush::message_name)) {
                    identity_message = registered->iMsg;
                    break;
                }
            if (!identity_message) return;
        }
        brush::Frame frame;
        frame.epoch = stats.epoch; frame.revision = stats.revision;
        frame.sequence = std::uint32_t(client->netchan.outgoing_sequence) & brush::sequence_mask;
        for (int i = 0; i < pack->num_entities; ++i) {
            const auto &state = pack->entities[i];
            const int slot = state.number;
            const auto target = targets.find(slot);
            if (slot <= 0 || slot >= g_psv.num_edicts || target == targets.end()) continue;
            auto *entity = &g_psv.edicts[slot];
            auto *model = target->second.model;
            // AddToFullPack plugins may replace the outgoing model/solidity.
            // Only bind an unchanged native BSP instance to its server serial.
            if (state.modelindex != entity->v.modelindex || state.solid != SOLID_BSP ||
                !find(slot, model, &model->hulls[0])) continue;
            const brush::Identity entry{unsigned(slot), unsigned(entity->serialnumber),
                                         target->second.inline_model};
            if (!brush::valid(entry) || (frame.total && entry.slot <= frame.entries[frame.total - 1].slot))
                return;
            frame.entries[frame.total++] = entry;
        }
        // All fragments fit or none are appended. A new entity messagenum can
        // never reuse metadata from an older datagram after loss/overflow.
        if (brush::packet_bytes(frame.total) > std::size_t(message->maxsize - message->cursize))
            return;
        std::array<std::uint8_t, brush::header_bytes + brush::entry_bytes * brush::chunk_entries> data;
        for (std::size_t first = 0;; first += brush::chunk_entries) {
            const auto count = brush::encode(frame, first, data);
            MSG_WriteByte(message, identity_message);
            MSG_WriteByte(message, int(count));
            MSG_WriteBuf(message, int(count), data.data());
            if (first + brush::chunk_entries >= frame.total) break;
        }
    }
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
        if (slot) {
            const char *name = STRING(entity->v.model);
            char *end = nullptr;
            if (!name || name[0] != '*' || !name[1] ||
                std::strtoul(name + 1, &end, 10) != target.inline_model || !end || *end)
                return nullptr;
        }
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
    int identity_message = 0;
    bool has_brushes = false;
};
MapPhysics physics;
class HostDelivery final : public IHostDelivery {
    std::uint32_t ReliableRoom(std::uint32_t slot) const override {
        if (!slot || slot > static_cast<unsigned>(g_psvs.maxclients) || !g_psvs.clients) return 0;
        const auto& client = g_psvs.clients[slot - 1];
        const auto& channel = client.netchan;
        const bool usable = client.edict && !client.fakeclient && client.active &&
            client.spawned && client.hasusrmsgs && channel.message.data &&
            !(channel.message.flags & SIZEBUF_OVERFLOWED);
        // Unacknowledged reliable_buf is separate from message. At most one
        // bounded next batch may queue while it retransmits; cursize then
        // stops additional edits until Netchan_Transmit actually drains it.
        const bool fragments = channel.fragbufs[FRAG_NORMAL_STREAM] ||
            channel.waitlist[FRAG_NORMAL_STREAM];
        return edits::reliable_room(channel.message.maxsize, channel.message.cursize, usable, fragments);
    }
};
HostDelivery host_delivery;
} // namespace
using goldcraft::IHostMapPhysics;
using goldcraft::IHostMapSample;
using goldcraft::IHostDelivery;
// The singleton macro casts directly to IBaseInterface and does not support
// two base interfaces. Typed factories preserve each interface's correct this.
static IBaseInterface *CreateMapPhysics() { return static_cast<IHostMapPhysics *>(&physics); }
static IBaseInterface *CreateMapSample() { return static_cast<IHostMapSample *>(&physics); }
EXPOSE_INTERFACE_FN(CreateMapPhysics, IHostMapPhysics, goldcraft::host_map_api_version);
EXPOSE_INTERFACE_FN(CreateMapSample, IHostMapSample, goldcraft::host_map_sample_api_version);
static IBaseInterface *CreateHostDelivery() { return &host_delivery; }
EXPOSE_INTERFACE_FN(CreateHostDelivery, IHostDelivery, goldcraft::host_delivery_api_version);
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
void GoldCraft_WriteBrushIdentities(client_t *client, packet_entities_t *pack, sizebuf_t *message) {
    physics.identities(client, pack, message);
}
