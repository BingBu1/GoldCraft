// Compiled only into the independent dedicated-server fixture, never production.
// The native GameDLL packs real edicts and ReHLDS writes its actual delta stream.
#include "precompiled.h"
#include "packet_entities_test.h"
#include "packet_entities.h"
#include "map_physics.h"
#include "goldcraft/host_map_api.hpp"
#include "goldcraft/brush_identity.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int requested_capacity = 1024;
constexpr int candidate_count = requested_capacity + 1;
constexpr int physics_identity_count = 257;
using SetupVisibility = decltype(DLL_FUNCTIONS::pfnSetupVisibility);
using AddToFullPack = decltype(DLL_FUNCTIONS::pfnAddToFullPack);
AddToFullPack original_pack;
int first_entity, selected_count, native_calls, native_added;
PFN_PROCESSOUTGOINGNET original_outgoing;
netchan_t *captured_channel;
std::vector<byte> captured_packet;

void CaptureOutgoing(netchan_t *channel, sizebuf_t *message) {
    if (original_outgoing) original_outgoing(channel, message);
    if (channel == captured_channel)
        captured_packet.assign(message->data, message->data + message->cursize);
}

void AllVisible(edict_t *, edict_t *, unsigned char **pvs, unsigned char **pas) {
    *pvs = *pas = nullptr;
}
int SelectedPack(entity_state_t *state, int number, edict_t *entity, edict_t *host,
                 int flags, int player, unsigned char *pvs) {
    if (number < first_entity || number >= first_entity + selected_count)
        return FALSE;
    ++native_calls;
    const int added = original_pack(state, number, entity, host, flags, player, pvs);
    native_added += added != 0;
    return added;
}

void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Save(const std::filesystem::path &path, const void *bytes, std::size_t size) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    Require(bool(output), "cannot open packet evidence");
    output.write(static_cast<const char *>(bytes), static_cast<std::streamsize>(size));
    Require(bool(output), "cannot write packet evidence");
}
struct Message {
    std::vector<byte> bytes;
    sizebuf_t value{};
    explicit Message(std::size_t capacity = NET_MAX_PAYLOAD) : bytes(capacity) {
        value.buffername = "GoldCraft packet fixture";
        value.data = bytes.data();
        value.maxsize = int(bytes.size());
        value.flags = SIZEBUF_ALLOW_OVERFLOW;
    }
    void reset() { value.cursize = value.flags = 0; value.flags = SIZEBUF_ALLOW_OVERFLOW; }
};
struct ClientState {
    client_t &client;
    client_frame_t *frames;
    std::vector<client_frame_t> fixture_frames;
    std::array<char, sizeof(client_t::userinfo)> userinfo;
    event_state_t events;
    usercmd_t lastcmd;
    entvars_t vars;
    client_t *previous_host;
    int outgoing, delta, group;
    netadr_t address;
    qboolean proxy, fake;
    explicit ClientState(client_t &value)
        : client(value), frames(value.frames), fixture_frames(SV_UPDATE_BACKUP),
          events(value.events), lastcmd(value.lastcmd), vars(value.edict->v), previous_host(host_client),
          outgoing(value.netchan.outgoing_sequence), delta(value.delta_sequence),
          group(g_groupop), address(value.netchan.remote_address), proxy(value.proxy), fake(value.fakeclient) {
        std::memcpy(userinfo.data(), value.userinfo, userinfo.size());
        client.frames = fixture_frames.data();
        client.events = {};
        client.proxy = FALSE;
        client.fakeclient = FALSE;
        client.netchan.remote_address.type = NA_IP;
        client.lastcmd.buttons &= ~IN_SCORE;
        host_client = &client;
        g_groupop = GROUP_OP_AND;
    }
    ~ClientState() {
        for (auto &frame : fixture_frames)
            SV_ClearPacketEntities(&frame);
        client.frames = frames;
        client.netchan.outgoing_sequence = outgoing;
        client.netchan.remote_address = address;
        client.delta_sequence = delta;
        client.events = events;
        client.lastcmd = lastcmd;
        client.proxy = proxy;
        client.fakeclient = fake;
        client.edict->v = vars;
        std::memcpy(client.userinfo, userinfo.data(), userinfo.size());
        host_client = previous_host;
        g_groupop = group;
    }
};
struct EntityState {
    int old_count = g_psv.num_edicts;
    SetupVisibility visibility = gEntityInterface.pfnSetupVisibility;
    AddToFullPack pack = gEntityInterface.pfnAddToFullPack;
    std::vector<edict_t> saved;
    std::vector<entity_state_t> baselines;
    explicit EntityState(const edict_t &model, bool brush = false)
        : saved(candidate_count), baselines(candidate_count) {
        Require(old_count + candidate_count <= g_psv.max_edicts &&
                    old_count + candidate_count <= 2048,
                "launch the independent fixture with -num_edicts 2048");
        first_entity = old_count;
        std::memcpy(saved.data(), g_psv.edicts + old_count, sizeof(edict_t) * saved.size());
        std::memcpy(baselines.data(), g_psv.baselines + old_count,
                    sizeof(entity_state_t) * baselines.size());
        g_psv.num_edicts += candidate_count;
        for (int i = 0; i < candidate_count; ++i) {
            auto &entity = g_psv.edicts[old_count + i];
            entity = {};
            entity.free = FALSE;
            entity.v.pContainingEntity = &entity;
            entity.v.model = model.v.model;
            entity.v.modelindex = model.v.modelindex;
            entity.v.classname = model.v.classname;
            entity.serialnumber = 17001 + i;
            entity.v.movetype = brush ? MOVETYPE_PUSH : MOVETYPE_NONE;
            entity.v.solid = brush ? SOLID_BSP : SOLID_NOT;
            // Quantized native values exercise every entity, not only its ID.
            for (int axis = 0; axis < 3; ++axis) {
                entity.v.origin[axis] = float((i * (37 + axis * 11)) % 16000 - 8000) / 8;
                entity.v.angles[axis] = float((i * (13 + axis * 7)) % 360);
                entity.v.mins[axis] = float(-1 - i % 15);
                entity.v.maxs[axis] = float(1 + i % 15);
                entity.v.size[axis] = entity.v.maxs[axis] - entity.v.mins[axis];
            }
            entity.v.frame = float(i % 200);
            entity.v.sequence = i % 16;
            entity.v.skin = i % 8;
            entity.v.body = i % 16;
            entity.v.scale = 1 + float(i % 32) / 256;
            entity.v.framerate = float(1 + i % 15) / 16;
            entity.v.rendermode = kRenderTransAlpha;
            entity.v.renderamt = 100 + i % 100;
            entity.v.rendercolor[0] = float(i % 256);
            entity.v.rendercolor[1] = float((i * 3) % 256);
            entity.v.rendercolor[2] = float((i * 7) % 256);
            for (int j = 0; j < 4; ++j) entity.v.controller[j] = byte((i * (j + 1)) % 256);
            for (int j = 0; j < 2; ++j) entity.v.blending[j] = byte((i * (j + 9)) % 256);
            // Runtime-created edicts have zero spawn baselines on both ends.
            g_psv.baselines[old_count + i] = {};
            SV_LinkEdict(&entity, FALSE);
        }
        original_pack = pack;
        gEntityInterface.pfnSetupVisibility = AllVisible;
        gEntityInterface.pfnAddToFullPack = SelectedPack;
    }
    ~EntityState() {
        gEntityInterface.pfnSetupVisibility = visibility;
        gEntityInterface.pfnAddToFullPack = pack;
        for (int i = 0; i < candidate_count; ++i)
            SV_UnlinkEdict(g_psv.edicts + old_count + i);
        std::memcpy(g_psv.edicts + old_count, saved.data(), sizeof(edict_t) * saved.size());
        std::memcpy(g_psv.baselines + old_count, baselines.data(),
                    sizeof(entity_state_t) * baselines.size());
        g_psv.num_edicts = old_count;
        original_pack = nullptr;
    }
};

struct PhysicsState {
    goldcraft::IHostMapPhysics *api;
    goldcraft::HostMapStats saved;
    explicit PhysicsState(model_t *model, std::uint32_t inline_model)
        : api(static_cast<goldcraft::IHostMapPhysics *>(
              CreateInterface(goldcraft::host_map_api_version, nullptr))) {
        Require(api != nullptr, "missing production map physics API");
        saved = api->Stats();
        Require(saved.targets == 0, "packet fixture must start with unedited topology");
        // The independent fixture owns this empty engine transaction and removes
        // it before restoring any synthetic edict. No ledger/MC request is sent.
        const auto epoch = saved.epoch ? saved.epoch : std::uint64_t{71};
        if (!saved.epoch) api->Reset(epoch, 0);
        std::vector<goldcraft::HostMapCut> cuts(physics_identity_count);
        for (int i = 0; i < physics_identity_count; ++i) {
            auto &cut = cuts[i];
            cut.slot = first_entity + i;
            cut.serial = unsigned(g_psv.edicts[cut.slot].serialnumber);
            cut.model = inline_model;
            for (int axis = 0; axis < 3; ++axis) {
                const auto center = (model->mins[axis] + model->maxs[axis]) * .5f;
                cut.min[axis] = center - .25f;
                cut.max[axis] = center + .25f;
            }
        }
        if (!api->Replace(epoch, saved.revision + 1, cuts.data(), unsigned(cuts.size()))) {
            const std::string error = api->LastError();
            api->Reset(saved.epoch, saved.revision);
            throw std::runtime_error(error);
        }
    }
    ~PhysicsState() { api->Reset(saved.epoch, saved.revision); }
};

void RichChanges(client_t &client, int generation) {
    for (int i = 0; i < requested_capacity; ++i) {
        auto &v = g_psv.edicts[first_entity + i].v;
        const int seed = i * 109 + generation * 719;
        for (int axis = 0; axis < 3; ++axis) {
            v.origin[axis] = float((seed * (13 + axis * 4)) % 15000 - 7500) / 8;
            v.angles[axis] = float((seed * (11 + axis * 8)) % 359 + 1);
            v.mins[axis] = -float((seed * (7 + axis * 6)) % 12000 + 1) / 8;
            v.maxs[axis] = float((seed * (9 + axis * 4)) % 12000 + 1) / 8;
            v.startpos[axis] = float((seed * (17 + axis * 4)) % 8000 + 1);
            v.endpos[axis] = -float((seed * (19 + axis * 6)) % 8000 + 1);
        }
        v.animtime = float(g_psv.time) + float(seed % 100 + 1) / 100;
        v.starttime = float(g_psv.time) + float(seed % 100 + 1) / 10;
        v.impacttime = 0; // Actual Entity_Encode keeps origin/angles with one time zero.
        v.frame = float(seed % 254 + 1);
        v.sequence = seed % 254 + 1;
        v.skin = seed % 250 + 1;
        v.body = seed % 254 + 1;
        v.colormap = seed % 65534 + 1;
        v.effects = EF_BRIGHTFIELD | EF_MUZZLEFLASH | EF_BRIGHTLIGHT | EF_DIMLIGHT;
        v.scale = float(seed % 65000 + 1) / 256;
        v.framerate = float(seed % 120 + 1) / 16;
        v.rendermode = int(seed % 5 + 1);
        v.renderfx = seed % 20 + 1;
        v.renderamt = seed % 254 + 1;
        v.rendercolor[0] = float(seed % 254 + 1);
        v.rendercolor[1] = float((seed * 3) % 254 + 1);
        v.rendercolor[2] = float((seed * 7) % 254 + 1);
        for (int axis = 0; axis < 4; ++axis)
            v.controller[axis] = byte((seed * (axis + 3)) % 254 + 1);
        for (int axis = 0; axis < 2; ++axis)
            v.blending[axis] = byte((seed * (axis + 9)) % 254 + 1);
        v.aiment = &g_psv.edicts[1 + (i + generation) % g_psvs.maxclients];
        v.owner = client.edict;
        v.iuser4 = 1 + (i + generation) % 3;
    }
}

void Describe(const std::filesystem::path &folder) {
    std::ostringstream json;
    json << "{\"stateBytes\":" << sizeof(entity_state_t) << ",\"time\":" << g_psv.time
         << ",\"maxClients\":" << g_psvs.maxclients << ",\"instanceBaselines\":"
         << g_psv.instance_baselines->number << ",\"firstEntity\":" << first_entity
         << ",\"schemas\":[";
    bool first = true;
    for (auto *info = g_sv_delta; info; info = info->next) {
        if (!first) json << ',';
        first = false;
        json << "{\"name\":\"" << info->name << "\",\"fields\":[";
        for (int i = 0; i < info->delta->fieldCount; ++i) {
            if (i) json << ',';
            const auto &field = info->delta->pdd[i];
            json << "{\"fieldType\":" << uint32(field.fieldType) << ",\"fieldName\":\""
                 << field.fieldName << "\",\"fieldOffset\":" << field.fieldOffset
                 << ",\"fieldSize\":" << field.fieldSize << ",\"significant_bits\":"
                 << field.significant_bits << ",\"premultiply\":" << field.premultiply
                 << ",\"postmultiply\":" << field.postmultiply << '}';
        }
        json << "]}";
    }
    json << "]}";
    const auto text = json.str();
    Save(folder / "schema.json", text.data(), text.size());
    Message descriptions;
    SV_WriteDeltaDescriptionsToClient(&descriptions.value);
    Require(!(descriptions.value.flags & SIZEBUF_OVERFLOWED), "delta descriptions overflow");
    Save(folder / "descriptions.bin", descriptions.value.data, descriptions.value.cursize);
    Save(folder / "baselines.bin", g_psv.baselines, sizeof(entity_state_t) * g_psv.num_edicts);
    Save(folder / "instance-baselines.bin", g_psv.instance_baselines->baseline,
         sizeof(entity_state_t) * g_psv.instance_baselines->number);
}

void CheckEvents(const sizebuf_t &message, int missing_entity) {
    struct RestoreRead {
        sizebuf_t saved = net_message;
        int count = msg_readcount, bad = msg_badread;
        bool reading = false;
        ~RestoreRead() {
            if (reading) MSG_EndBitReading(&net_message);
            net_message = saved;
            msg_readcount = count;
            msg_badread = bad;
        }
    } restore;
    net_message = message;
    MSG_BeginReading();
    Require(MSG_ReadByte() == svc_event, "missing native event service");
    MSG_StartBitReading(&net_message);
    restore.reading = true;
    Require(MSG_ReadBits(5) == 2, "native event count differs");
    for (int i = 0; i < 2; ++i) {
        Require(MSG_ReadBits(10) == 1 && MSG_ReadBits(1) == 1, "native event header differs");
        Require(MSG_ReadBits(11) == unsigned(i ? 1024 : 1023), "native event packet index truncated");
        event_args_t base{}, args{};
        if (MSG_ReadBits(1))
            DELTA_ParseDelta(reinterpret_cast<byte *>(&base), reinterpret_cast<byte *>(&args), g_peventdelta);
        Require(!i || args.entindex == missing_entity, "native event sentinel lost entity ID");
        if (MSG_ReadBits(1)) MSG_ReadBits(16);
    }
    Require(!msg_badread, "native event stream is malformed");
}

void Packets() {
    const auto *evidence = std::getenv("GOLDCRAFT_PACKET_EVIDENCE");
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || !evidence || !*evidence ||
        Cmd_Argc() != 2)
        return;
    const int slot = std::atoi(Cmd_Argv(1));
    if (slot < 1 || slot > g_psvs.maxclients)
        return;
    auto &client = g_psvs.clients[slot - 1];
    if (!client.active || !client.fakeclient || !client.edict || client.edict->free)
        return;
    for (int i = 0; i < g_psvs.maxclients; ++i)
        if (i != slot - 1 && g_psvs.clients[i].active)
            return;
    const edict_t *model = nullptr;
    for (int i = g_psvs.maxclients + 1; i < g_psv.num_edicts; ++i) {
        auto &entity = g_psv.edicts[i];
        if (!entity.free && entity.v.modelindex > 0 && entity.v.model &&
            STRING(entity.v.model)[0] != '*') {
            model = &entity;
            break;
        }
    }
    if (!model) return;
    try {
        std::filesystem::path folder(evidence);
        std::filesystem::create_directories(folder);
        ClientState restore_client(client);
        EntityState restore_entities(*model);
        Describe(folder);
        Message message;
        int previous = -1;
        int sequence = 41;
        std::ostringstream cases;
        cases << "{\"cases\":[";
        bool first = true;
        auto frame = [&](const char *name, const char *capability, int candidates, bool delta,
                         int expected, bool proxy = false) {
            selected_count = candidates;
            native_calls = native_added = 0;
            client.events = {};
            client.proxy = proxy;
            Info_SetValueForKey(client.userinfo, "_gcpe", capability, sizeof(client.userinfo));
            client.netchan.outgoing_sequence = sequence;
            client.delta_sequence = delta ? previous : -1;
            message.reset();
            SV_WriteEntitiesToClient(&client, &message.value);
            auto &packet = client.frames[SV_UPDATE_MASK & sequence].entities;
            Require(!(message.value.flags & SIZEBUF_OVERFLOWED), "actual packet overflow");
            Require(packet.num_entities == expected, "actual packet count differs");
            Require(message.value.cursize >= 5 &&
                        (message.value.data[0] == svc_packetentities ||
                         message.value.data[0] == svc_deltapacketentities),
                    "missing actual packet service");
            Save(folder / (std::string(name) + ".bin"), message.value.data, message.value.cursize);
            Save(folder / (std::string(name) + ".states"), packet.entities,
                 sizeof(entity_state_t) * packet.num_entities);
            if (!first) cases << ',';
            first = false;
            cases << "{\"name\":\"" << name << "\",\"sequence\":" << sequence
                  << ",\"from\":" << client.delta_sequence << ",\"count\":"
                  << packet.num_entities << ",\"candidates\":" << candidates
                  << ",\"nativeCalls\":" << native_calls << ",\"nativeAdded\":"
                  << native_added << ",\"bytes\":" << message.value.cursize
                  << ",\"service\":" << unsigned(message.value.data[0]) << '}';
            previous = sequence++;
        };
        frame("legacy-256", "", candidate_count, false, 256);
        frame("malformed-512", "512", candidate_count, false, 256);
        frame("malformed-suffix", "1024x", candidate_count, false, 256);
        frame("wide-full", "1024", requested_capacity, false, requested_capacity);
        frame("wide-1025-boundary", "1024", candidate_count, false, requested_capacity);
        frame("wide-unchanged", "1024", requested_capacity, true, requested_capacity);
        g_psv.edicts[first_entity].v.effects |= EF_NODRAW;
        g_psv.edicts[first_entity + 512].v.origin[0] += 8;
        frame("wide-replace", "1024", candidate_count, true, requested_capacity);
        frame("wide-shrink", "1024", requested_capacity, true, requested_capacity - 1);
        g_psv.edicts[first_entity].v.effects &= ~EF_NODRAW;
        frame("wide-refill", "1024", requested_capacity, true, requested_capacity);
        frame("downgrade-full", "", candidate_count, true, 256);
        frame("rewide-delta", "1024", requested_capacity, true, requested_capacity);
        frame("proxy-legacy", "1024", candidate_count, false, 256, true);
        client.netchan.remote_address.type = NA_LOOPBACK;
        frame("loopback-legacy", "1024", candidate_count, false, 256);
        client.netchan.remote_address.type = NA_IPX;
        frame("ipx-legacy", "1024", candidate_count, false, 256);
        client.netchan.remote_address.type = NA_IP;
        frame("wide-events-base", "1024", requested_capacity, false, requested_capacity);

        auto &pack = client.frames[SV_UPDATE_MASK & previous].entities;
        client.events = {};
        client.events.ei[0].index = 1;
        client.events.ei[0].entity_index = pack.entities[requested_capacity - 1].number;
        client.events.ei[1].index = 1;
        client.events.ei[1].entity_index = first_entity + requested_capacity;
        message.reset();
        SV_EmitEvents(&client, &pack, &message.value);
        Require(!(message.value.flags & SIZEBUF_OVERFLOWED), "events overflow");
        const int event_bytes = message.value.cursize;
        Save(folder / "wide-events.bin", message.value.data, message.value.cursize);
        CheckEvents(message.value, first_entity + requested_capacity);

        Message rich(128 * 1024);
        const int rich_full_sequence = sequence++;
        client.netchan.outgoing_sequence = rich_full_sequence;
        client.delta_sequence = -1;
        client.events = {};
        selected_count = requested_capacity;
        RichChanges(client, 1);
        SV_WriteEntitiesToClient(&client, &rich.value);
        Require(!(rich.value.flags & SIZEBUF_OVERFLOWED), "large diagnostic full stream overflow");
        const int rich_full_bytes = rich.value.cursize;
        Save(folder / "budget-full.bin", rich.value.data, rich.value.cursize);
        Save(folder / "budget-full.states", client.frames[SV_UPDATE_MASK & rich_full_sequence].entities.entities,
             sizeof(entity_state_t) * requested_capacity);
        const int rich_delta_sequence = sequence++;
        client.netchan.outgoing_sequence = rich_delta_sequence;
        client.delta_sequence = rich_full_sequence;
        RichChanges(client, 2);
        rich.reset();
        SV_WriteEntitiesToClient(&client, &rich.value);
        Require(!(rich.value.flags & SIZEBUF_OVERFLOWED), "large diagnostic delta stream overflow");
        auto &rich_packet = client.frames[SV_UPDATE_MASK & rich_delta_sequence].entities;
        Require(rich_packet.num_entities == requested_capacity, "rich delta packet count differs");
        const int rich_delta_bytes = rich.value.cursize;
        Save(folder / "budget-delta.bin", rich.value.data, rich.value.cursize);
        Save(folder / "budget-delta.states", rich_packet.entities, sizeof(entity_state_t) * requested_capacity);
        Message bounded;
        bounded.value.maxsize = GoldCraft_PacketDatagramBytes;
        std::fill(bounded.bytes.begin() + bounded.value.maxsize, bounded.bytes.end(), byte{0xa7});
        SV_CreatePacketEntities_internal(sv_packet_delta, &client, &rich_packet, &bounded.value);
        const bool overflow = (bounded.value.flags & SIZEBUF_OVERFLOWED) != 0;
        Require(std::all_of(bounded.bytes.begin() + bounded.value.maxsize, bounded.bytes.end(),
                            [](byte value) { return value == byte{0xa7}; }),
                "bounded delta encoder wrote outside the production datagram budget");
        Require(overflow == (rich_delta_bytes > GoldCraft_PacketDatagramBytes),
                "bounded delta encoder overflow differs from complete diagnostic length");
        SZ_Clear(&bounded.value);
        Require(bounded.value.cursize == 0 && !(bounded.value.flags & SIZEBUF_OVERFLOWED),
                "native overflow clearing retained a partial snapshot");
        client.delta_sequence = -1;
        SV_CreatePacketEntities_internal(sv_packet_nodelta, &client, &pack, &bounded.value);
        Require(!(bounded.value.flags & SIZEBUF_OVERFLOWED) && bounded.value.cursize >= 5 &&
                    bounded.value.data[0] == svc_packetentities &&
                    (bounded.value.data[1] | (int(bounded.value.data[2]) << 8)) == requested_capacity,
                "normal full snapshot did not recover after actual encoder overflow");
        Save(folder / "budget-recovery.bin", bounded.value.data, bounded.value.cursize);
        std::ostringstream budget;
        budget << "{\"entities\":1024,\"fullBytes\":" << rich_full_bytes
               << ",\"deltaBytes\":" << rich_delta_bytes
               << ",\"datagramBudget\":" << GoldCraft_PacketDatagramBytes
               << ",\"identity1024Bytes\":" << goldcraft::brush::packet_bytes(requested_capacity)
               << ",\"deltaOverflow\":" << (overflow ? "true" : "false")
               << ",\"guardUnchanged\":true,\"partialCleared\":true"
               << ",\"normal1024Recovery\":true,\"recoveryBytes\":" << bounded.value.cursize << '}';
        const auto budget_text = budget.str();
        Save(folder / "budget.json", budget_text.data(), budget_text.size());

        std::vector<entity_state_t> over(candidate_count);
        packet_entities_t invalid{};
        invalid.num_entities = candidate_count;
        invalid.entities = over.data();
        message.reset();
        const int rejected = SV_CreatePacketEntities_internal(sv_packet_nodelta, &client, &invalid,
                                                              &message.value);
        Require(rejected == 0 && (message.value.flags & SIZEBUF_OVERFLOWED) &&
                    message.value.cursize == 0,
                "oversized API packet was not rejected before encoding");
        cases << "],\"invalid1025Rejected\":true,\"eventBytes\":" << event_bytes
              << ",\"event1023And1024Decoded\":true"
              << ",\"restoredSynchronously\":true}";
        const auto text = cases.str();
        Save(folder / "cases.json", text.data(), text.size());
        Con_Printf("GC_PACKETS {\"passed\":true,\"cases\":15,\"limit\":1024,\"stateBytes\":%d}\n",
                   int(sizeof(entity_state_t)));
    } catch (const std::exception &error) {
        Con_Printf("GC_PACKETS {\"passed\":false,\"error\":\"%s\"}\n", error.what());
    }
}

void Transmit() {
    const auto *evidence = std::getenv("GOLDCRAFT_PACKET_EVIDENCE");
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || !evidence || !*evidence ||
        Cmd_Argc() != 4)
        return;
    const int slot = std::atoi(Cmd_Argv(1)), port = std::atoi(Cmd_Argv(2));
    const int mode = std::atoi(Cmd_Argv(3));
    if (slot < 1 || slot > g_psvs.maxclients || port < 1 || port > 65535 || mode < 0 || mode > 7)
        return;
    auto &client = g_psvs.clients[slot - 1];
    if (!client.active || !client.fakeclient || !client.edict || client.edict->free)
        return;
    try {
        const std::filesystem::path folder(evidence);
        struct Restore {
            client_t &client;
            netchan_t channel;
            PFN_PROCESSOUTGOINGNET outgoing;
            std::array<char, sizeof(client_t::userinfo)> userinfo;
            explicit Restore(client_t &c) : client(c), channel(c.netchan),
                outgoing(g_modfuncs.m_pfnProcessOutgoingNet) {
                std::memcpy(userinfo.data(), c.userinfo, userinfo.size());
            }
            ~Restore() {
                client.netchan = channel;
                std::memcpy(client.userinfo, userinfo.data(), userinfo.size());
                g_modfuncs.m_pfnProcessOutgoingNet = outgoing;
                original_outgoing = nullptr;
                captured_channel = nullptr;
            }
        } restore(client);
        Info_SetValueForKey(client.userinfo, goldcraft::packet_entities::capability,
                           mode == 0 || mode == 4 ? "" : goldcraft::packet_entities::capability_value,
                           sizeof(client.userinfo));
        std::vector<byte> bytes;
        if (mode == 0 || mode == 2) {
            bytes.resize(mode == 0 ? MAX_DATAGRAM : GoldCraft_PacketDatagramBytes);
            for (std::size_t i = 0; i < bytes.size(); ++i)
                bytes[i] = byte((i * 97 + i / 251) % 256);
        } else {
            std::ifstream input(folder / "wide-full.bin", std::ios::binary);
            Require(bool(input), "run gc_packet_entities before transport");
            bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        }
        client.netchan = {};
        auto &channel = client.netchan;
        channel.sock = NS_SERVER;
        channel.player_slot = slot;
        channel.remote_address.type = NA_IP;
        channel.remote_address.ip[0] = 127;
        channel.remote_address.ip[3] = 1;
        channel.remote_address.port = htons(static_cast<unsigned short>(port));
        channel.rate = 1000000;
        channel.outgoing_sequence = 401 + mode;
        channel.incoming_sequence = 17;
        channel.message.data = channel.message_buf;
        channel.message.maxsize = sizeof(channel.message_buf);
        channel.message.buffername = "GoldCraft transport fixture";
        channel.message.flags = SIZEBUF_ALLOW_OVERFLOW;
        if (mode == 3) {
            // The actual resend branch retains a pending reliable message while
            // carrying this same complete current snapshot.
            channel.incoming_acknowledged = 103;
            channel.last_reliable_sequence = 101;
            channel.incoming_reliable_acknowledged = 0;
            channel.reliable_sequence = 1;
            channel.reliable_length = 128;
            for (int i = 0; i < channel.reliable_length; ++i) channel.reliable_buf[i] = byte(i + 19);
        } else if (mode == 6) {
            channel.message.cursize = 128;
            for (int i = 0; i < channel.message.cursize; ++i) channel.message.data[i] = byte(i + 19);
        }
        captured_packet.clear();
        captured_channel = &channel;
        original_outgoing = g_modfuncs.m_pfnProcessOutgoingNet;
        g_modfuncs.m_pfnProcessOutgoingNet = CaptureOutgoing;
        int actual_entities = 0, identity_bytes = 0, identity_total = 0;
        if (mode == 7) {
            const edict_t *brush = nullptr;
            std::uint64_t least_storage = ~std::uint64_t{};
            for (int i = g_psvs.maxclients + 1; i < g_psv.num_edicts; ++i) {
                const auto &entity = g_psv.edicts[i];
                if (!entity.free && entity.v.solid == SOLID_BSP && entity.v.model &&
                    STRING(entity.v.model)[0] == '*' && Mod_Handle(entity.v.modelindex)) {
                    const auto *model = Mod_Handle(entity.v.modelindex);
                    std::uint64_t storage = model->numplanes;
                    for (const auto &hull : model->hulls)
                        storage += hull.lastclipnode + 1;
                    if (storage < least_storage) {
                        least_storage = storage;
                        brush = &entity;
                    }
                }
            }
            Require(brush != nullptr, "missing actual native inline BSP");
            auto *model = Mod_Handle(brush->v.modelindex);
            const auto inline_model = std::strtoul(STRING(brush->v.model) + 1, nullptr, 10);
            ClientState restore_player(client);
            EntityState restore_entities(*brush, true);
            PhysicsState restore_physics(model, unsigned(inline_model));
            Info_SetValueForKey(client.userinfo, goldcraft::brush::capability,
                               goldcraft::brush::capability_value, sizeof(client.userinfo));
            selected_count = candidate_count;
            native_calls = native_added = 0;
            client.delta_sequence = -1;
            const int sequence = channel.outgoing_sequence;
            SV_SendClientDatagram(&client);
            auto &pack = client.frames[SV_UPDATE_MASK & sequence].entities;
            actual_entities = pack.num_entities;
            Require(actual_entities == requested_capacity && native_calls == requested_capacity,
                    "production datagram did not retain 1024 entities");
            Require(captured_packet.size() >= 8, "production datagram was not sent");
            auto decoded = captured_packet;
            COM_UnMunge2(decoded.data() + 8, int(decoded.size()) - 8, byte(sequence));
            bytes.assign(decoded.begin() + 8, decoded.end());
            Message entity_message, identities;
            SV_CreatePacketEntities_internal(sv_packet_nodelta, &client, &pack, &entity_message.value);
            channel.outgoing_sequence = sequence;
            GoldCraft_WriteBrushIdentities(&client, &pack, &identities.value);
            identity_bytes = identities.value.cursize;
            Require(identity_bytes == int(goldcraft::brush::packet_bytes(physics_identity_count)),
                    "production GCBrush sideband omitted identities above 256");
            Message short_identities;
            short_identities.value.maxsize = identity_bytes - 1;
            GoldCraft_WriteBrushIdentities(&client, &pack, &short_identities.value);
            Require(short_identities.value.cursize == 0 &&
                        !(short_identities.value.flags & SIZEBUF_OVERFLOWED),
                    "production GCBrush insufficient-space preflight wrote a partial frame");
            const int entity_offset = int(bytes.size()) - identity_bytes - entity_message.value.cursize;
            Require(entity_offset >= 5 && bytes[0] == svc_time &&
                        !std::memcmp(bytes.data() + entity_offset, entity_message.value.data,
                                     entity_message.value.cursize) &&
                        !std::memcmp(bytes.data() + bytes.size() - identity_bytes, identities.value.data,
                                     identity_bytes),
                    "production entity snapshot and GCBrush did not share one datagram");
            auto receiver = std::make_unique<goldcraft::brush::Receiver>();
            receiver->reset(restore_physics.api->Stats().epoch);
            std::size_t cursor = 0;
            while (cursor < std::size_t(identity_bytes)) {
                const auto size = identities.value.data[cursor + 1];
                receiver->accept(std::span(identities.value.data + cursor + 2, std::size_t(size)));
                cursor += size + 2;
            }
            Require(cursor == std::size_t(identity_bytes) && receiver->frame() &&
                        receiver->frame()->total == physics_identity_count &&
                        receiver->frame()->sequence == unsigned(sequence),
                    "production GCBrush frame did not decode with its snapshot sequence");
            identity_total = receiver->frame()->total;
            Save(folder / "production-entities.bin", entity_message.value.data, entity_message.value.cursize);
            Save(folder / "production-entities.states", pack.entities, sizeof(entity_state_t) * pack.num_entities);
            Save(folder / "production-brushes.bin", identities.value.data, identity_bytes);
            std::ostringstream metadata;
            metadata << "{\"name\":\"production-entities\",\"sequence\":" << sequence
                     << ",\"from\":-1,\"count\":" << actual_entities
                     << ",\"service\":40,\"offset\":" << entity_offset
                     << ",\"identityBytes\":" << identity_bytes
                     << ",\"identityTotal\":" << identity_total
                     << ",\"atomicInsufficientSpace\":true}";
            const auto text = metadata.str();
            Save(folder / "production.json", text.data(), text.size());
        } else {
            Netchan_Transmit(&channel, int(bytes.size()), bytes.data());
        }
        Require(captured_packet.size() >= 8, "native netchan did not transmit");
        const auto stem = "transport-" + std::to_string(mode);
        Save(folder / (stem + ".payload"), bytes.data(), bytes.size());
        Save(folder / (stem + ".netchan"), captured_packet.data(), captured_packet.size());
        std::uint32_t sent_sequence{};
        std::memcpy(&sent_sequence, captured_packet.data(), sizeof(sent_sequence));
        sent_sequence &= 0x3fffffff;
        Con_Printf("GC_TRANSPORT {\"mode\":%d,\"payload\":%d,\"packet\":%d,\"sequence\":%d,"
                   "\"enabled\":%d,\"reliable\":%d,\"entities\":%d,\"identityBytes\":%d,\"identityTotal\":%d}\n", mode, int(bytes.size()),
                   int(captured_packet.size()), int(sent_sequence),
                   int(GoldCraft_PacketChannelEnabled(&channel)), channel.reliable_length,
                   actual_entities, identity_bytes, identity_total);
    } catch (const std::exception &error) {
        Con_Printf("GC_TRANSPORT {\"passed\":false,\"error\":\"%s\"}\n", error.what());
    }
}
} // namespace

void GoldCraft_PacketEntitiesTestInit() {
    if (std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") && std::getenv("GOLDCRAFT_PACKET_EVIDENCE")) {
        Cmd_AddCommand("gc_packet_entities", Packets);
        Cmd_AddCommand("gc_packet_transmit", Transmit);
    }
}
