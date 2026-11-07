// SPDX-License-Identifier: GPL-3.0-or-later
// Actual ReHLDS SDK types and extracted production function/activation bodies.
// Network, clock and game-DLL edges are controlled; this is not a live server.
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "osconfig.h"
#include "archtypes.h"
#include "strtools.h"
#include "sys_shared.h"
#include "crc32c.h"
#include "static_map.h"
#include "FileSystem.h"
#include "decal.h"
#include "server.h"
#include "common.h"
#include "sys_dll.h"
#include "cmd.h"
#include "net_chan.h"
#include "com_custom.h"

static_assert(sizeof(void*) == 4);
// Only owner storage is a test double. Inline method bodies are extracted from
// matching CGameClient headers; no CGameClient vtable/layout claim is made.
server_t g_psv{};
struct GameClient {
    double m_localGameTimeBase{}, m_reconnectDeadline{};
#include "actual_game_client.inc"
};
server_static_t g_psvs{};
std::array<client_t, 32> clients;
std::array<GameClient, 32> gameClients;
std::array<GameClient*, 32> g_GameClients;
std::array<std::array<byte, 2048>, 32> fixtureMessages;
client_t* host_client{};
double realtime{}, systemTime{}, transmitSeconds{};
cmd_source_t cmd_source{};
UserMsg* sv_gpUserMsgs{};
DLL_FUNCTIONS gEntityInterface{};
bool gameAccepts{};
int drops{}, fragments{}, newResponses{}, gameConnectCalls{}, failures{}, checks{}, disconnects{}, customClears{};
std::string dropReason;
struct Transmission { int slot; double before, after, deadline; std::vector<byte> payload; };
std::vector<Transmission> transmissions;
struct RateLimiter { void ClientConnected(unsigned) {} } g_DlFileRateLimiter;

void SZ_Clear(sizebuf_t* message) { message->cursize = 0; }
void Netchan_Clear(netchan_t* channel) { SZ_Clear(&channel->message); }
void SV_SendServerinfo(sizebuf_t*, client_t*) { ++newResponses; }
void SV_SendUserReg(sizebuf_t*, UserMsg*) {}
char* NET_AdrToString(const netadr_t&) { static char address[] = "127.0.0.1"; return address; }
char* Info_Serverinfo() { static char empty[] = ""; return empty; }
void MSG_WriteByte(sizebuf_t* message, int value) { message->data[message->cursize++] = byte(value); }
void MSG_WriteString(sizebuf_t* message, const char* value) {
    const auto size = std::strlen(value) + 1;
    std::memcpy(message->data + message->cursize, value, size); message->cursize += int(size);
}
void SV_FullClientUpdate(client_t*, sizebuf_t*) {}
void Netchan_CreateFragments(qboolean, netchan_t*, sizebuf_t*) { ++fragments; }
void Netchan_FragSend(netchan_t*) {}
void SV_ClientPrintf(const char*, ...) {}
void SV_BroadcastPrintf(const char*, ...) {}
void Con_DPrintf(const char*, ...) {}
void COM_ClearCustomizationList(customization_t*, qboolean) { ++customClears; }
char* va(const char* format, ...) {
    static char text[256]; va_list args; va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args); va_end(args); return text;
}
void SV_DropClient(client_t* client, qboolean, const char* reason, ...) {
    ++drops; dropReason = reason;
    client->active = client->connected = client->spawned = FALSE;
}
double Sys_FloatTime() { return systemTime; }
void Netchan_Transmit(netchan_t* channel, int length, byte* data) {
    int slot = -1;
    for (int i = 0; i < 32; ++i) if (channel == &clients[i].netchan) slot = i;
    if (slot < 0 || length || data) std::abort();
    const double before = systemTime;
    systemTime += transmitSeconds;
    transmissions.push_back({slot, before, systemTime, gameClients[slot].GetReconnectDeadline(),
        {channel->message.data, channel->message.data + channel->message.cursize}});
}

void Log_Printf(const char*, ...) {}
#include "map_load_timing.h"
#include "actual_reconnect.inc"

void Check(bool value, const char* name) {
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
void Reset(int slot = 0) {
    std::memset(clients.data(), 0, sizeof(clients));
    gameClients = {}; fixtureMessages = {}; g_psvs = {}; g_psv = {};
    for (unsigned i = 0; i < 32; ++i) {
        g_GameClients[i] = &gameClients[i];
        clients[i].netchan.message.data = fixtureMessages[i].data();
        clients[i].netchan.message.maxsize = int(fixtureMessages[i].size());
    }
    g_psvs.maxclients = 32; g_psvs.clients = clients.data(); host_client = &clients[slot];
    host_client->connected = TRUE; host_client->netchan.last_received = 100;
    gameClients[slot].SetReconnectDeadline(1000); realtime = 100; systemTime = 1000;
    transmitSeconds = 0; g_psv.time = 42;
    cmd_source = src_client; gameAccepts = true;
    drops = fragments = newResponses = gameConnectCalls = disconnects = customClears = 0;
    dropReason.clear(); transmissions.clear();
    // Defaults come from actual source declarations, not fixture guesses.
    sv_timeout.value = std::strtof(sv_timeout.string, nullptr);
    sv_reconnect_timeout.value = std::strtof(sv_reconnect_timeout.string, nullptr);
    gEntityInterface = {};
    gEntityInterface.pfnClientDisconnect = [](edict_t*) { ++disconnects; };
    gEntityInterface.pfnClientConnect = [](edict_t*, const char*, const char*, char*) {
        ++gameConnectCalls; return gameAccepts ? TRUE : FALSE;
    };
}
void At(double seconds, bool traffic = true) {
    realtime = 100 + seconds; systemTime = 1000 + seconds;
    if (traffic) host_client->netchan.last_received = realtime;
}
void Baseline() {
    for (int slot : {0, 31}) {
        Reset(slot); gameClients[slot ? 0 : 31].SetReconnectDeadline(777);
        SV_New_f();
        Check(gameClients[slot].GetReconnectDeadline() == 0, "accepted new clears reconnect deadline before loading");
        Check(fragments == 1 && host_client->m_bSentNewResponse && gameConnectCalls == 1, "normal sign-on still sent");
        Check(gameClients[slot ? 0 : 31].GetReconnectDeadline() == 777, "other player slot untouched");
        Check(gameClients[slot].m_localGameTimeBase == 42, "actual inline owner records game time");
        At(35); SV_CheckTimeouts(); Check(drops == 0, "active download beyond 30 seconds is retained");
    }
    Reset(); Check(sv_timeout.value == 60 && sv_reconnect_timeout.value == 30, "actual timeout defaults stay 60 and 30");
    gameClients[0].SetReconnectDeadline(0); At(60, false); SV_CheckTimeouts();
    Check(!drops, "ordinary network timeout retains equality");
    At(61, false); SV_CheckTimeouts(); Check(drops == 1 && dropReason == "Timed out", "normal inactivity timeout still applies");
    Reset(); SV_InactivateClients(); Test_ActivateReconnect();
    At(30); SV_CheckTimeouts(); Check(!drops, "reconnect equality keeps strict greater-than condition");
    At(30.25); SV_CheckTimeouts();
    Check(drops == 1 && dropReason == "Failed to reconnect after level change", "warm netchan without new still drops after 30 seconds");
    Check(gameClients[0].GetReconnectDeadline() == 0, "hard expiry clears old request");
    Reset(); cmd_source = src_command; SV_New_f();
    Check(gameClients[0].GetReconnectDeadline() == 1000 && !fragments, "console new cannot satisfy reconnect");
    Reset(); host_client->hasusrmsgs = host_client->m_bSentNewResponse = TRUE; SV_New_f();
    Check(gameClients[0].GetReconnectDeadline() == 1000 && !fragments, "duplicate new remains rejected");
    Reset(); host_client->spawned = TRUE; SV_New_f();
    Check(gameClients[0].GetReconnectDeadline() == 1000 && !fragments, "inactive spawned state remains rejected");
    Reset(); gameAccepts = false; SV_New_f();
    Check(gameClients[0].GetReconnectDeadline() == 1000 && drops == 1 && !fragments, "game DLL refusal does not qualify as accepted new");
    Reset(); host_client->fully_connected = TRUE; At(31); SV_CheckTimeouts();
    Check(gameClients[0].GetReconnectDeadline() == 0 && !drops, "fully connected fallback still clears deadline");
    Reset(); host_client->fakeclient = TRUE; At(400, false); SV_CheckTimeouts();
    Check(!drops, "fake clients stay outside timeout checks");
    Reset(); sv_reconnect_timeout.value = 0; At(31); SV_CheckTimeouts();
    Check(!drops, "existing zero reconnect cvar remains effective");
    gameClients[0].SetReconnectDeadline(0); At(61, false); host_client->netchan.last_received = 100;
    SV_CheckTimeouts(); Check(drops == 1 && dropReason == "Timed out", "zero reconnect setting never disables ordinary timeout");
}
void LoadDelay() {
    for (int slot : {0, 31}) {
        Reset(slot); host_client->active = host_client->spawned = host_client->fully_connected = TRUE;
        SV_InactivateClients();
        Check(gameClients[slot].GetReconnectDeadline() == 0, "load: inactivation retires earlier request before loading");
        Check(!host_client->active && host_client->connected && !host_client->spawned && !host_client->fully_connected,
              "inactivation preserves actual connection flags");
        Check(transmissions.empty() && customClears == 1, "loading has not sent a reconnect request");
        systemTime += 32; transmitSeconds = .25; // realtime stays 100 through blocked load.
        Test_ActivateReconnect();
        Check(transmissions.size() == 1 && transmissions[0].slot == slot, "actual activation loop sends exact eligible slot");
        const auto& sent = transmissions[0];
        Check(sent.payload.size() == 12 && sent.payload[0] == svc_stufftext &&
            std::strcmp(reinterpret_cast<const char*>(sent.payload.data() + 1), "reconnect\n") == 0,
            "original BuildReconnect payload is preserved");
        Check(sent.deadline == 0, "load: no deadline exists while transmit executes");
        Check(gameClients[slot].GetReconnectDeadline() == sent.after && sent.after == 1032.25,
              "load: request begins at transmit completion using advancing clock");
        Check(realtime == 100 && fragments == 1 && host_client->hasusrmsgs, "activation keeps stale realtime and ordinary user-message path");
        At(32.25, false); SV_CheckTimeouts();
        Check(!drops, "load: first frame after 32-second load does not kick freshly requested client");
        std::printf("load slot=%d sendStart=%.2f sendEnd=%.2f deadline=%.2f realtime=%.2f drops=%d reason=%s\n",
            slot + 1, sent.before, sent.after, gameClients[slot].GetReconnectDeadline(), realtime, drops, dropReason.c_str());
    }
}
void ResponseAfterSend() {
    for (int slot : {0, 31}) {
        Reset(slot); SV_InactivateClients(); systemTime = 1032; Test_ActivateReconnect();
        At(61.75); SV_New_f();
        Check(gameClients[slot].GetReconnectDeadline() == 0 && host_client->m_bSentNewResponse && gameConnectCalls == 1,
              "new just before request expires satisfies handshake");
        At(100); SV_CheckTimeouts(); Check(!drops, "accepted new permits long resource download with traffic");
    }
    Reset(); SV_InactivateClients(); systemTime = 1032; Test_ActivateReconnect();
    At(62); SV_CheckTimeouts(); Check(!drops, "send: full 30 seconds remain after long server loading");
    Reset(); SV_InactivateClients(); systemTime = 1032; Test_ActivateReconnect();
    At(62.25); SV_CheckTimeouts();
    Check(drops == 1 && dropReason == "Failed to reconnect after level change", "unanswered real request still expires with live netchan");
}
void ClockBoundary() {
    Reset(); SV_InactivateClients(); Test_ActivateReconnect();
    realtime = 100; systemTime = 1030; SV_CheckTimeouts();
    Check(!drops, "clock: equality does not expire while frame clock is unchanged");
    systemTime = 1030.25; SV_CheckTimeouts();
    Check(drops == 1 && dropReason == "Failed to reconnect after level change", "clock: same advancing send clock expires stalled-frame request");
    Reset(); SV_InactivateClients(); Test_ActivateReconnect();
    realtime = 10000; host_client->netchan.last_received = realtime; systemTime = 1001;
    SV_CheckTimeouts(); Check(!drops, "clock: frame-clock discontinuity cannot consume wall-clock request allowance");
}
void Eligibility() {
    Reset(); host_client->connected = FALSE; gameClients[0].SetReconnectDeadline(0);
    clients[1].connected = TRUE; clients[1].fakeclient = TRUE;
    clients[2].active = TRUE; clients[2].netchan.last_received = 100;
    clients[3].connected = TRUE; clients[3].netchan.last_received = 100;
    clients[31].spawned = TRUE; clients[31].netchan.last_received = 100;
    SV_InactivateClients();
    Check(drops == 1 && dropReason == "Dropping fakeclient on level change", "map change retains fake-client removal");
    Test_ActivateReconnect();
    Check(transmissions.size() == 3 && transmissions[0].slot == 2 && transmissions[1].slot == 3 && transmissions[2].slot == 31,
          "actual activation excludes empty and fake slots while preserving active connected spawned transitions");
    Check(gameClients[0].GetReconnectDeadline() == 0 && gameClients[1].GetReconnectDeadline() == 0,
          "excluded slots never gain a reconnect request");
    Reset(); g_psvs.maxclients = 1; gameClients[0].SetReconnectDeadline(0); Test_ActivateReconnect();
    Check(transmissions.empty() && newResponses == 1 && fragments == 1 && gameClients[0].GetReconnectDeadline() == 0,
          "single-player activation retains serverinfo branch without reconnect deadline");
}
void RepeatedMaps() {
    Reset(31); SV_InactivateClients(); systemTime = 1032; Test_ActivateReconnect();
    At(34); SV_New_f(); Check(gameClients[31].GetReconnectDeadline() == 0, "first map accepted new clears request");
    SV_InactivateClients(); Check(gameClients[31].GetReconnectDeadline() == 0, "repeat: second map has no request during loading");
    systemTime += 32; Test_ActivateReconnect();
    Check(transmissions.size() == 2 && gameClients[31].GetReconnectDeadline() == 1066,
          "repeat: second actual send establishes a fresh deadline");
    At(66); SV_CheckTimeouts(); Check(!drops, "repeat: previous map and second load cannot expire new request");
    Reset(); SV_InactivateClients(); Test_ActivateReconnect(); At(5);
    SV_InactivateClients(); Check(gameClients[0].GetReconnectDeadline() == 0, "repeat: abandoned pending request retires before new load");
    systemTime += 32; Test_ActivateReconnect(); At(37); SV_CheckTimeouts();
    Check(!drops, "repeat: abandoned request cannot kick client after replacement send");
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string group = argv[1];
    if (group == "baseline") Baseline();
    else if (group == "load-delay") LoadDelay();
    else if (group == "response") ResponseAfterSend();
    else if (group == "clock") ClockBoundary();
    else if (group == "eligibility") Eligibility();
    else if (group == "repeat") RepeatedMaps();
    else return 2;
    std::printf("group=%s checks=%d failures=%d client_t=%zu netchan_t=%zu\n", group.c_str(), checks, failures, sizeof(client_t), sizeof(netchan_t));
    return failures ? 1 : 0;
}
