// Compiled only into the isolated engine fixture, never the production DLL.
#include "precompiled.h"
#include "map_visibility.h"
#include "map_physics.h"
#include "goldcraft/brush_identity.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
struct Buffer {
    sizebuf_t &target, saved;
    std::vector<unsigned char> storage;
    explicit Buffer(sizebuf_t &value) : target(value), saved(value), storage(16384) {
        target.data = storage.data();
        target.maxsize = static_cast<int>(storage.size());
        target.cursize = 0;
        target.flags = 0;
    }
    ~Buffer() { target = saved; }
    std::string hex() const {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        for (int i = 0; i < target.cursize && i < 256; ++i) {
            result += digits[target.data[i] >> 4];
            result += digits[target.data[i] & 15];
        }
        return result;
    }
};
struct Player {
    client_t &client;
    entvars_t vars;
    event_state_t events;
    qboolean fake, connected, spawned, fully, proxy;
    explicit Player(client_t &value)
        : client(value), vars(value.edict->v), events(value.events), fake(value.fakeclient),
          connected(value.connected), spawned(value.spawned), fully(value.fully_connected),
          proxy(value.proxy) {
        // Only an existing fake client may enter this synchronous test. No
        // packets are transmitted; every recipient buffer is swapped/restored.
        client.fakeclient = FALSE;
        client.connected = client.spawned = client.fully_connected = TRUE;
        client.proxy = FALSE;
        client.events = {};
    }
    void position(const float *point) {
        for (int axis = 0; axis < 3; ++axis) {
            client.edict->v.origin[axis] = point[axis];
            client.edict->v.mins[axis] = -1;
            client.edict->v.maxs[axis] = 1;
            client.edict->v.size[axis] = 2;
        }
        client.edict->v.groupinfo = 0;
        SV_LinkEdict(client.edict, FALSE);
    }
    ~Player() {
        client.fakeclient = fake;
        client.connected = connected;
        client.spawned = spawned;
        client.fully_connected = fully;
        client.proxy = proxy;
        client.events = events;
        client.edict->v = vars;
        SV_LinkEdict(client.edict, FALSE);
    }
};
struct Context {
    client_t *saved_host = host_client;
    int saved_group = g_groupop;
    explicit Context(client_t *sender) {
        host_client = sender;
        g_groupop = GROUP_OP_AND;
    }
    ~Context() {
        host_client = saved_host;
        g_groupop = saved_group;
    }
};
void BrushIdentity() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || Cmd_Argc() != 5) return;
    const int player_slot = std::atoi(Cmd_Argv(1)), requested = std::atoi(Cmd_Argv(2));
    const int mode = std::atoi(Cmd_Argv(3)), repeats = std::atoi(Cmd_Argv(4));
    if (player_slot < 1 || player_slot > g_psvs.maxclients || mode < 0 || mode > 8 ||
        repeats < 1 || repeats > 65536 || requested < 0 || requested >= g_psv.num_edicts) return;
    auto &client = g_psvs.clients[player_slot - 1];
    if (!client.active || !client.fakeclient || !client.edict || client.edict->free) return;
    int slot = requested;
    if (!slot) {
        for (int i = g_psvs.maxclients + 1; i < g_psv.num_edicts; ++i) {
            const auto &entity = g_psv.edicts[i];
            if (!entity.free && entity.v.solid == SOLID_BSP && STRING(entity.v.model)[0] == '*' &&
                !Q_strcmp(STRING(entity.v.classname), "func_door")) { slot = i; break; }
        }
    }
    if (slot <= g_psvs.maxclients) return;
    auto &target = g_psv.edicts[slot];
    auto *model = Mod_Handle(target.v.modelindex);
    if (target.free || !model || model->type != mod_brush || STRING(target.v.model)[0] != '*') return;
    const unsigned inline_model = std::strtoul(STRING(target.v.model) + 1, nullptr, 10);
    Player restore_player(client);
    Context context(&client);
    struct Restore {
        client_t &client;
        edict_t &target;
        int sequence, serial;
        std::array<char, sizeof(client_t::userinfo)> userinfo;
        Restore(client_t &c, edict_t &e) : client(c), target(e),
            sequence(c.netchan.outgoing_sequence), serial(e.serialnumber) {
            std::memcpy(userinfo.data(), c.userinfo, userinfo.size());
        }
        ~Restore() {
            client.netchan.outgoing_sequence = sequence;
            target.serialnumber = serial;
            std::memcpy(client.userinfo, userinfo.data(), userinfo.size());
        }
    } restore(client, target);
    client.netchan.outgoing_sequence = 731;
    Info_SetValueForKey(client.userinfo, goldcraft::brush::capability,
                       mode == 1 ? "" : mode == 2 ? "2" : "1", sizeof(client.userinfo));
    if (mode == 7) client.fully_connected = FALSE;
    if (mode == 8) client.fakeclient = TRUE;
    // Use the real GameDLL packing contract, then emulate two explicit plugin
    // overrides. No packets or identity changes escape this synchronous call.
    entity_state_t state{};
    const auto packed = gEntityInterface.pfnAddToFullPack(&state, slot, &target, client.edict, 1, FALSE, nullptr);
    if (mode == 3) ++target.serialnumber;
    if (mode == 4) state.modelindex = g_psv.edicts[0].v.modelindex;
    if (mode == 5) state.solid = SOLID_NOT;
    packet_entities_t pack{};
    pack.num_entities = packed ? 1 : 0;
    pack.entities = &state;
    std::array<byte, 4096> bytes{};
    sizebuf_t message{};
    message.data = bytes.data(); message.maxsize = int(bytes.size()); message.flags = 0;
    if (mode == 6) message.maxsize = int(goldcraft::brush::packet_bytes(1)) - 1;
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < repeats; ++i) {
        message.cursize = 3;
        bytes[0] = 0x71; bytes[1] = 0x72; bytes[2] = 0x73;
        GoldCraft_WriteBrushIdentities(&client, &pack, &message);
    }
    const auto us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count() / repeats;
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    for (int i = 3; i < message.cursize; ++i) {
        encoded += digits[bytes[i] >> 4]; encoded += digits[bytes[i] & 15];
    }
    Con_Printf("GC_BRUSH {\"slot\":%d,\"serial\":%u,\"model\":%u,\"packed\":%d,\"solid\":%d,"
               "\"low\":[%g,%g,%g],\"high\":[%g,%g,%g],\"wire\":\"%s\",\"prefix\":%d,\"us\":%.6f}\n",
               slot, unsigned(target.serialnumber), inline_model, int(packed), state.solid,
               model->mins[0],model->mins[1],model->mins[2],model->maxs[0],model->maxs[1],model->maxs[2],
               encoded.c_str(), bytes[0] == 0x71 && bytes[1] == 0x72 && bytes[2] == 0x73, us);
}
void Delivery() {
    if (!std::getenv("GOLDCRAFT_HEADLESS_BINDINGS") || Cmd_Argc() != 12)
        return;
    const int from = std::atoi(Cmd_Argv(1)) - 1, to = std::atoi(Cmd_Argv(2)) - 1;
    const int mode = std::atoi(Cmd_Argv(9)), options = std::atoi(Cmd_Argv(10));
    const int repeats = std::atoi(Cmd_Argv(11));
    if (from < 0 || to < 0 || from == to || from >= g_psvs.maxclients || to >= g_psvs.maxclients ||
        mode < 0 || mode > 10 || options < 0 || options > 15 || repeats < 1 || repeats > 65536 ||
        ((options & 8) && mode > 4))
        return;
    for (int i = 0; i < g_psvs.maxclients; ++i) {
        const auto &c = g_psvs.clients[i];
        if (c.active && (i != from && i != to))
            return;
        if ((i == from || i == to) && (!c.active || !c.fakeclient || !c.edict || c.edict->free))
            return;
    }
    vec3_t source, receiver;
    for (int axis = 0; axis < 3; ++axis) {
        source[axis] = std::strtof(Cmd_Argv(3 + axis), nullptr);
        receiver[axis] = std::strtof(Cmd_Argv(6 + axis), nullptr);
        if (!std::isfinite(source[axis]) || !std::isfinite(receiver[axis]) ||
            std::abs(source[axis]) > 8192 || std::abs(receiver[axis]) > 8192)
            return;
    }
    auto &sender = g_psvs.clients[from], &listener = g_psvs.clients[to];
    Player source_state(sender), receiver_state(listener);
    source_state.position(source);
    receiver_state.position(receiver);
    if (options & 1) {
        sender.edict->v.groupinfo = 1;
        listener.edict->v.groupinfo = 2;
    }
    if (options & 2)
        listener.proxy = TRUE;
    Context context(&sender);
    Buffer outgoing(g_psv.multicast), sender_d(sender.datagram), sender_r(sender.netchan.message);
    Buffer target_d(listener.datagram), target_r(listener.netchan.message);
    if (options & 8)
        target_d.target.maxsize = target_r.target.maxsize = 0;

    auto *pvs = SV_FatPVS(source);
    auto *pas = SV_FatPAS(source);
    const int fat_visible = SV_CheckVisibility(listener.edict, pvs);
    const int fat_audible = SV_CheckVisibility(listener.edict, pas);
    const int leaf = SV_PointLeafnum(source);
    const int visible = SV_ValidClientMulticast(&listener, leaf, MSG_FL_PVS, source);
    const int audible = SV_ValidClientMulticast(&listener, leaf, MSG_FL_PAS, source);
    const auto started = std::chrono::steady_clock::now();
    int seen = 0;
    for (int i = 0; i < repeats; ++i)
        seen += SV_ValidClientMulticast(&listener, leaf, MSG_FL_PAS, source) != 0;
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started)
            .count() /
        repeats;

    constexpr const char *sample = "weapons/dryfire_pistol.wav";
    bool emitted = true;
    if (mode <= 3) {
        constexpr int destinations[] = {MSG_PVS, MSG_PAS, MSG_PVS_R, MSG_PAS_R};
        PF_MessageBegin_I(destinations[mode], svc_temp_entity, source, nullptr);
        PF_WriteByte_I(TE_SPARKS);
        for (float coordinate : source)
            PF_WriteCoord_I(coordinate);
        PF_MessageEnd_I();
    } else if (mode == 4 || mode == 8 || mode == 9) {
        SV_StartSound(options & 4 ? 1 : 0, sender.edict, mode == 9 ? CHAN_STATIC : CHAN_WEAPON,
                      sample, 255, 1, mode == 8 ? SND_FL_STOP : 0, 100);
    } else if (mode == 5 || mode == 6 || mode == 10) {
        emitted = SV_EmitSound2_api(
            sender.edict, mode == 6 ? nullptr : GetRehldsApiClient(&listener), CHAN_WEAPON, sample,
            1, 1, 0, 100, SND_EMIT2_USE_ORIGIN | (mode == 10 ? SND_EMIT2_NOPAS : 0), source);
    } else {
        unsigned short event = 1;
        while (event < MAX_EVENTS && !g_psv.event_precache[event].pszScript)
            ++event;
        emitted = event < MAX_EVENTS;
        if (emitted)
            EV_Playback(0, sender.edict, event, 0, source, vec3_origin, 0, 0, 0, 0, 0, 0);
    }
    int events = 0;
    for (const auto &event : listener.events.ei)
        events += event.index != 0;
    Con_Printf("GC_DELIVERY {\"visible\":%d,\"audible\":%d,\"fatVisible\":%d,\"fatAudible\":%d,"
               "\"datagram\":\"%s\",\"reliable\":\"%s\",\"senderBytes\":%d,\"events\":%d,"
               "\"emitted\":%d,\"repeats\":%d,\"seen\":%d,\"us\":%.6f}\n",
               visible, audible, fat_visible, fat_audible, target_d.hex().c_str(),
               target_r.hex().c_str(), sender.datagram.cursize + sender.netchan.message.cursize,
               events, emitted, repeats, seen, us);
}
} // namespace

void GoldCraft_MapDeliveryTestInit() {
    if (std::getenv("GOLDCRAFT_HEADLESS_BINDINGS")) {
        Cmd_AddCommand("gc_carve_delivery", Delivery);
        Cmd_AddCommand("gc_brush_identity", BrushIdentity);
    }
}
