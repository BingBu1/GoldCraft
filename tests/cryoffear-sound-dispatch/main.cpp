// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "native/guest/cof/server/sound_dispatch.hpp"
#include "native/guest/cof/server/effects_dispatch.hpp"
#include "native/guest/cof/particle_protocol.hpp"
#include "native/guest/cof/sound_protocol.hpp"

#include <array>
#include <bit>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

enginefuncs_t g_engfuncs{};
globalvars_t globals{};
globalvars_t* gpGlobals = &globals;
bool g_bAllowedCSBot{};
const Vector g_vecZero{};

namespace
{
using namespace guest::cof;
constexpr std::array<const char*, 8> Samples{
    "weapons_new/p345/p345_fire.wav", "player/voice.wav", "xweaponsfoo", "WEAPONS/foo.wav",
    "weapons", "guest/cof/weapons/p345/p345_click.wav", "a/weapons-b.wav", "new/Weapons/x.wav"
};
std::array<edict_t, 33> edicts;
std::vector<std::uint32_t> events;
std::vector<std::byte> packet;
std::array<std::uint32_t, 3> rawCoordinates;
Vector gun;
unsigned checks{}, coordinateCount{}, packetCount{};
int destination{}, target{}, cancelEdge{}, recycleEdge{}, recycleTarget{}, edge{};
bool valid{}, began{}, cleared{}, nestedBusy{};
PlaySfxRequest* mutate{};
std::array<char, 180>* mutateText{};
void (*duringPacket)(){};
std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }
float Float(std::uint32_t value) { return std::bit_cast<float>(value); }
void Check(bool value)
{
    ++checks;
    if (!value) { std::cerr << "SDK check " << checks << " failed edge " << edge << '\n'; std::exit(2); }
}
int Index(const edict_t* value) { return static_cast<int>(value - edicts.data()); }
bool Current(void*) noexcept { return valid; }
template<class... T> void Event(std::uint32_t kind, T... values)
{
    events.insert(events.end(), {kind, sizeof...(T), static_cast<std::uint32_t>(values)...});
}
void Edge()
{
    ++edge;
    if (edge == cancelEdge) valid = false;
    if (edge == recycleEdge) ++edicts[recycleTarget].serialnumber;
}
struct Person final : CBasePlayer
{
    Vector GetGunPosition() override { Event(21); Edge(); return gun; }
};
std::array<Person, 33> players;
void Byte(int value) { packet.push_back(std::byte(value & 255)); Edge(); }
void Short(int value)
{
    packet.push_back(std::byte(value & 255)); packet.push_back(std::byte((value >> 8) & 255)); Edge();
}
void Coord(float value)
{
    Check(coordinateCount < 3);
    rawCoordinates[coordinateCount++] = Bits(value);
    const auto word = static_cast<std::uint16_t>(static_cast<std::int32_t>(static_cast<double>(value) * 8.0));
    packet.push_back(std::byte(word & 255)); packet.push_back(std::byte(word >> 8)); Edge();
}
void String(const char* value)
{
    do { packet.push_back(std::byte(static_cast<unsigned char>(*value))); } while (*value++);
    Edge();
}
void Begin(int kind, int message, const float* origin, edict_t* entity)
{
    Check(!began && message == 77 && !origin);
    began = true; destination = kind; target = entity ? Index(entity) : 0;
    packet.clear(); coordinateCount = 0; Edge();
    if (mutate) { mutate->origin = {9, 8, 7}; mutate->mode = 19; mutate->enabled = 29; mutate->broadcast = !mutate->broadcast; }
    if (mutateText) { mutateText->fill(0); (*mutateText)[0] = 'x'; }
    if (duringPacket) duringPacket();
}
void End()
{
    Check(began && coordinateCount == 3);
    SoundMessageData decoded{};
    Check(DecodeSound(packet, decoded));
    unsigned sample = 0;
    while (sample < Samples.size() && decoded.sample != Samples[sample]) ++sample;
    Check(sample < Samples.size());
    Event(22, destination, target, sample, decoded.mode, decoded.enabled,
          rawCoordinates[0], rawCoordinates[1], rawCoordinates[2], decoded.remote,
          std::bit_cast<std::uint16_t>(decoded.recipientIndex));
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto word = static_cast<std::uint16_t>(static_cast<std::int32_t>(double(Float(rawCoordinates[i])) * 8.0));
        Check(Bits(decoded.origin[i]) == Bits(float(std::bit_cast<std::int16_t>(word)) * 0.125f));
    }
    Check(packet.size() <= SoundPacketLimit);
    ++packetCount; began = false; Edge();
}
edict_t* Entity(int index) { Edge(); return &edicts[index]; }
int EntityIndex(const edict_t* value) { const auto index = Index(value); Edge(); return index; }
int Register(const char* name, int size)
{
    Check(std::string_view(name) == "CoFSFX" && size == -1);
    Edge(); return 77;
}
void Reset()
{
    events.clear(); packet.clear(); coordinateCount = packetCount = 0;
    valid = true; began = cleared = nestedBusy = false; edge = cancelEdge = recycleEdge = recycleTarget = 0;
    mutate = nullptr; mutateText = nullptr; duringPacket = nullptr;
    globals = {}; globals.maxClients = 5; gun = {10.5f, -50000.25f, 30.125f};
    for (int i = 0; i <= 32; ++i)
    {
        edicts[i] = {}; edicts[i].serialnumber = 8;
        edicts[i].v.pContainingEntity = &edicts[i]; edicts[i].pvPrivateData = &players[i];
        players[i].pev = &edicts[i].v;
    }
    g_engfuncs = {};
    g_engfuncs.pfnMessageBegin = Begin; g_engfuncs.pfnMessageEnd = End;
    g_engfuncs.pfnWriteByte = Byte; g_engfuncs.pfnWriteShort = Short;
    g_engfuncs.pfnWriteCoord = Coord; g_engfuncs.pfnWriteString = String;
    g_engfuncs.pfnPEntityOfEntIndex = Entity; g_engfuncs.pfnIndexOfEdict = EntityIndex;
    g_engfuncs.pfnRegUserMsg = Register;
}
PlaySfxRequest Request()
{
    return {Samples[0], {100.25f, -23.125f, 45000.5f}, 0, 1, true, PlaySfxRules::Cooperative};
}
PlaySfxResult Send(const PlaySfxRequest& request)
{
    return SendPlaySfx(players[1], request, 77, {nullptr, Current});
}
void Busy()
{
    const auto before = packet;
    nestedBusy = Send(Request()).status == PlaySfxStatus::Busy;
    Check(packet == before);
    Check(RegisterPlaySfxMessage({nullptr, Current}) == 0);
}
void Guards()
{
    Reset(); Check(RegisterPlaySfxMessage({nullptr, Current}) == 77);
    Reset(); cancelEdge = 1; Check(RegisterPlaySfxMessage({nullptr, Current}) == 0);
    Reset(); g_engfuncs.pfnRegUserMsg = nullptr; Check(RegisterPlaySfxMessage({nullptr, Current}) == 0 && !edge);
    Reset(); valid = false; Check(Send(Request()).status == PlaySfxStatus::Cancelled && !edge);
    Reset(); Check(SendPlaySfx(players[1], Request(), 77, {}) .status == PlaySfxStatus::InvalidInput && !edge);
    Reset(); Check(SendPlaySfx(players[1], Request(), 0, {nullptr, Current}).status == PlaySfxStatus::MissingMessage && !edge);
    Reset(); auto request = Request(); request.rules = PlaySfxRules::Unsupported;
    Check(Send(request).status == PlaySfxStatus::UnsupportedRules && !edge);
    Reset(); request = Request(); request.sample = nullptr; Check(Send(request).status == PlaySfxStatus::InvalidInput && !edge);
    Reset(); request.sample = ""; Check(Send(request).status == PlaySfxStatus::InvalidInput && !edge);
    Reset(); std::array<char, 181> longSample; longSample.fill('w'); longSample.back() = 0;
    request = Request(); request.sample = longSample.data(); Check(Send(request).status == PlaySfxStatus::InvalidInput && !edge);
    Reset(); g_engfuncs.pfnWriteCoord = nullptr; Check(Send(Request()).status == PlaySfxStatus::MissingEngine && !edge);
    for (const float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 268435456.0f})
    {
        Reset(); request = Request(); request.rules = PlaySfxRules::Direct; request.origin.x = bad;
        Check(Send(request).status == PlaySfxStatus::InvalidInput && !edge && !packetCount);
        Reset(); request = Request(); request.origin.x = bad;
        Check(Send(request).status == PlaySfxStatus::Sent && packetCount == 5); // Original overwrites unused origin.
        Reset(); gun.x = bad; Check(Send(Request()).status == PlaySfxStatus::InvalidInput && packetCount == 0);
    }
    Reset(); const auto reference = Send(Request()); const auto expected = events; const int allEdges = edge;
    Check(reference.status == PlaySfxStatus::Sent && reference.packets == 5 && !began);
    for (int position = 1; position <= allEdges; ++position)
    {
        Reset(); cancelEdge = position;
        const auto result = Send(Request());
        Check(result.status == PlaySfxStatus::Cancelled && !began);
        Check(result.packets == packetCount && packetCount <= 5);
    }
    for (const int position : {1, 2, 3, 15})
    {
        Reset(); recycleEdge = position; recycleTarget = 1;
        const auto result = Send(Request()); Check(result.status == PlaySfxStatus::Cancelled && !began);
    }
    Reset(); recycleEdge = 16; recycleTarget = 2;
    Check(Send(Request()).status == PlaySfxStatus::Cancelled && packetCount == 1 && !began);
    Reset(); duringPacket = Busy; Check(Send(Request()).status == PlaySfxStatus::Sent && nestedBusy && !began);
    Reset(); request = Request(); mutate = &request;
    Check(Send(request).status == PlaySfxStatus::Sent && events == expected);
    Reset(); std::array<char, 180> text{}; std::copy_n(Samples[0], std::strlen(Samples[0])+1, text.begin());
    request = Request(); request.sample = text.data(); mutateText = &text;
    Check(Send(request).status == PlaySfxStatus::Sent && events == expected);
    Reset(); globals.maxClients = 32; Check(Send(Request()).packets == 32);
    Reset(); for (int i = 1; i <= 5; ++i) edicts[i].free = i != 1;
    Check(Send(Request()).packets == 1);
    Reset(); request = Request(); request.rules = PlaySfxRules::Direct;
    const auto direct = Send(request); Check(direct.packets == 1 && destination == MSG_ALL && target == 0);
}

ShellRequest* mutateShell{};
void FxByte(int value) { Event(32, value & 255); packet.push_back(std::byte(value & 255)); Edge(); }
void FxCoord(float value) { Event(33, Bits(value)); Edge(); }
void FxAngle(float value) { Event(34, Bits(value)); Edge(); }
void FxShort(int value) { Event(35, value & 65535); Edge(); }
void FxBegin(int kind, int message, const float* origin, edict_t* entity)
{
    Check(!began); began = true; packet.clear(); destination = message;
    Event(31, kind, message, origin != nullptr,
          origin ? Bits(origin[0]) : 0, origin ? Bits(origin[1]) : 0,
          origin ? Bits(origin[2]) : 0, entity ? Index(entity) : 0);
    Edge();
    if (mutateShell) *mutateShell = {};
    if (duringPacket) duringPacket();
}
void FxEnd()
{
    Check(began); Event(36); began = false; ++packetCount; Edge();
    if (destination == 78)
    {
        ParticleMessageData message{};
        Check(DecodeParticle(packet, message));
        Check(message.operation == ParticleOperation::Add && message.originalWeapon == 7 && message.owner == 200);
    }
}
void FxPlayback(int flags, const edict_t* source, unsigned short index, float delay,
    float* origin, float* angles, float a, float b, int c, int d, int e, int f)
{
    Check(!began);
    Event(37, flags, Index(source), index, Bits(delay),
          Bits(origin[0]), Bits(origin[1]), Bits(origin[2]), Bits(angles[0]), Bits(angles[1]), Bits(angles[2]),
          Bits(a), Bits(b), c, d, e, f);
    Edge();
    if (duringPacket) duringPacket();
}
void FxReset()
{
    Reset(); mutateShell = nullptr;
    g_engfuncs.pfnMessageBegin = FxBegin; g_engfuncs.pfnMessageEnd = FxEnd;
    g_engfuncs.pfnWriteByte = FxByte; g_engfuncs.pfnWriteCoord = FxCoord;
    g_engfuncs.pfnWriteShort = FxShort; g_engfuncs.pfnWriteAngle = FxAngle;
    g_engfuncs.pfnPlaybackEvent = FxPlayback;
}
ShellRequest Shell() { return {{1, 2, 50000}, {-1, -2, -50000}, 1e20f, 17, 1}; }
void FxBusy()
{
    Check(EjectWeaponShell(Shell(), {nullptr, Current}) == WeaponEffectStatus::Busy);
    Check(CooperativeViewRecoil(players[1], {}, 1, {nullptr, Current}) == WeaponEffectStatus::Busy);
    Check(SendLocalGunsmoke(players[1], 7, 200, 78, {nullptr, Current}) == WeaponEffectStatus::Busy);
    Check(Send(Request()).status == PlaySfxStatus::Busy);
    Check(RegisterPlaySfxMessage({nullptr, Current}) == 0);
}
void EffectGuards()
{
    const PlaySfxCall call{nullptr, Current};
    FxReset(); Check(EjectWeaponShell(Shell(), {}) == WeaponEffectStatus::InvalidInput && events.empty());
    valid = false; Check(EjectWeaponShell(Shell(), call) == WeaponEffectStatus::Cancelled && events.empty());
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()})
    {
        FxReset(); auto value = Shell(); value.angle = bad;
        Check(EjectWeaponShell(value, call) == WeaponEffectStatus::InvalidInput && events.empty());
        value = Shell(); value.origin.x = bad;
        Check(EjectWeaponShell(value, call) == WeaponEffectStatus::InvalidInput && events.empty());
        Check(CooperativeViewRecoil(players[1], {bad, 0, 0}, 1, call) == WeaponEffectStatus::InvalidInput);
    }
    FxReset(); auto value = Shell(); value.velocity.z = 1e20f;
    Check(EjectWeaponShell(value, call) == WeaponEffectStatus::InvalidInput && events.empty());
    for (int model : {-1, 0, 65536})
    {
        FxReset(); value = Shell(); value.model = model;
        Check(EjectWeaponShell(value, call) == WeaponEffectStatus::InvalidInput && events.empty());
    }
    FxReset(); g_engfuncs.pfnWriteAngle = nullptr;
    Check(EjectWeaponShell(Shell(), call) == WeaponEffectStatus::MissingEngine && events.empty());
    FxReset(); g_engfuncs.pfnPlaybackEvent = nullptr;
    Check(CooperativeViewRecoil(players[1], {}, 1, call) == WeaponEffectStatus::MissingEngine);
    FxReset(); Check(CooperativeViewRecoil(players[1], {}, 0, call) == WeaponEffectStatus::MissingMessage);
    Check(SendLocalGunsmoke(players[1], 7, 200, 0, call) == WeaponEffectStatus::MissingMessage);
    Check(SendLocalGunsmoke(players[1], 255, 200, 78, call) == WeaponEffectStatus::InvalidInput);
    FxReset(); Check(EjectWeaponShell(Shell(), call) == WeaponEffectStatus::Issued && !began);
    const int shellEdges = edge;
    for (int cut = 1; cut <= shellEdges; ++cut)
    {
        FxReset(); cancelEdge = cut;
        Check(EjectWeaponShell(Shell(), call) == WeaponEffectStatus::Cancelled && !began && packetCount == 1);
    }
    FxReset(); value = Shell(); const auto saved = value; mutateShell = &value;
    Check(EjectWeaponShell(value, call) == WeaponEffectStatus::Issued && value.model == 0);
    const auto snapshotEvents = events;
    FxReset(); Check(EjectWeaponShell(saved, call) == WeaponEffectStatus::Issued && events == snapshotEvents);
    FxReset(); duringPacket = FxBusy;
    Check(EjectWeaponShell(Shell(), call) == WeaponEffectStatus::Issued && !began);
    FxReset(); duringPacket = FxBusy;
    Check(CooperativeViewRecoil(players[1], {}, 1, call) == WeaponEffectStatus::Issued);
    FxReset(); duringPacket = FxBusy;
    Check(SendLocalGunsmoke(players[1], 7, 200, 78, call) == WeaponEffectStatus::Issued && !began);
    const int smokeEdges = edge;
    for (int cut = 1; cut <= smokeEdges; ++cut)
    {
        FxReset(); cancelEdge = cut;
        Check(SendLocalGunsmoke(players[1], 7, 200, 78, call) == WeaponEffectStatus::Cancelled && !began && packetCount == 1);
    }
    FxReset(); recycleEdge = 1; recycleTarget = 1;
    Check(CooperativeViewRecoil(players[1], {}, 1, call) == WeaponEffectStatus::Cancelled);
    FxReset(); recycleEdge = 1; recycleTarget = 1;
    Check(SendLocalGunsmoke(players[1], 7, 200, 78, call) == WeaponEffectStatus::Cancelled && !began);
}

PresentationState expectedState, *mutateState{};
int registeredStateMessage{79};
int StateRegister(const char* name, int size)
{
    Check(std::string_view(name) == StateMessage && size == -1);
    Edge();
    return registeredStateMessage;
}
void StateBegin(int kind, int message, const float* origin, edict_t* entity)
{
    Check(!began && kind == MSG_ONE && message == 79 && !origin && entity == &edicts[1]);
    began = true; packet.clear(); Edge();
    if (mutateState) *mutateState = {};
    if (duringPacket) duringPacket();
}
void StateEnd()
{
    PresentationState decoded{};
    Check(began && DecodeState(packet, decoded) && decoded == expectedState);
    began = false; ++packetCount; Edge();
}
void StateReset()
{
    Reset();
    mutateState = nullptr; registeredStateMessage = 79;
    expectedState = {{0x12345678, 0x23456789, 0x3456789a}, 0x456789ab, 3, true, 50, 2800};
    g_engfuncs.pfnRegUserMsg = StateRegister;
    g_engfuncs.pfnMessageBegin = StateBegin;
    g_engfuncs.pfnMessageEnd = StateEnd;
}
void StateBusy()
{
    const PlaySfxCall call{nullptr, Current};
    Check(RegisterPresentationMessage(call) == 0);
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Busy);
}
void PresentationGuards()
{
    const PlaySfxCall call{nullptr, Current};
    StateReset(); Check(RegisterPresentationMessage(call) == 79);
    for (int id : {0, 1, 63, 256})
    {
        StateReset(); registeredStateMessage = id;
        Check(RegisterPresentationMessage(call) == 0);
        Check(SendPresentationState(players[1], expectedState, id, call) == WeaponEffectStatus::MissingMessage);
    }
    StateReset(); cancelEdge = 1; Check(RegisterPresentationMessage(call) == 0);
    StateReset(); g_engfuncs.pfnWriteByte = nullptr;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::MissingEngine);
    StateReset(); auto bad = expectedState; bad.key.lifeEpoch = 0;
    Check(SendPresentationState(players[1], bad, 79, call) == WeaponEffectStatus::InvalidInput && !began);
    bad = expectedState; bad.originalWeapon = 255;
    Check(SendPresentationState(players[1], bad, 79, call) == WeaponEffectStatus::InvalidInput && !began);
    StateReset();
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Issued && packetCount == 1);
    const int packetEdges = edge;
    for (int cut = 1; cut <= packetEdges; ++cut)
    {
        StateReset(); cancelEdge = cut;
        Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Cancelled &&
            !began && packetCount == 1);
    }
    StateReset(); auto changed = expectedState; mutateState = &changed;
    Check(SendPresentationState(players[1], changed, 79, call) == WeaponEffectStatus::Issued && changed.revision == 0);
    StateReset(); duringPacket = StateBusy;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Issued && !began);
    StateReset(); recycleEdge = 1; recycleTarget = 1;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Cancelled && !began);
    StateReset(); expectedState.originalWeapon = 0; expectedState.aimEnabled = false;
    expectedState.fovMode = 0; expectedState.zoomSpeedEighths = 0;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Issued);
    StateReset(); expectedState.originalWeapon = 21; expectedState.revolverAngle = 72.0F;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Issued &&
        packet.size() == RevolverStateBytes && packetCount == 1);
    const int revolverEdges = edge;
    for (int cut = 1; cut <= revolverEdges; ++cut)
    {
        StateReset(); expectedState.originalWeapon = 21; expectedState.revolverAngle = 144.0F;
        cancelEdge = cut;
        Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::Cancelled &&
            !began && packetCount == 1);
    }
    StateReset(); expectedState.originalWeapon = 21;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::InvalidInput && !began);
    StateReset(); expectedState.revolverAngle = 72.0F;
    Check(SendPresentationState(players[1], expectedState, 79, call) == WeaponEffectStatus::InvalidInput && !began);
}

int EffectReplay(const char* input, const char* result)
{
    std::ifstream cases(input), expected(result);
    std::string line, comparison; unsigned count{};
    while (std::getline(cases, line))
    {
        std::istringstream row(line); row >> std::hex;
        std::array<std::uint32_t, 11> values{};
        for (auto& value : values) if (!(row >> value)) return 8;
        std::string trailing; if (row >> trailing || values[0] > 1) return 8;
        if (!std::getline(expected, comparison)) return 9;
        FxReset();
        const ShellRequest request{{Float(values[1]), Float(values[2]), Float(values[3])},
            {Float(values[4]), Float(values[5]), Float(values[6])}, Float(values[7]), int(values[8]),
            std::bit_cast<std::int32_t>(values[9])};
        const auto status = values[0] == 0 ? EjectWeaponShell(request, {nullptr, Current}) :
            CooperativeViewRecoil(players[1], request.origin, static_cast<unsigned short>(values[10]), {nullptr, Current});
        Check(status == WeaponEffectStatus::Issued && !began);
        std::istringstream words(comparison); words >> std::hex;
        std::vector<std::uint32_t> wanted; std::uint32_t word;
        while (words >> word) wanted.push_back(word);
        if (events != wanted) { std::cerr << "Original effect mismatch " << count << '\n'; return 10; }
        ++count;
    }
    if (count != 128 || std::getline(expected, comparison)) return 11;
    EffectGuards();
    return 0;
}
}

#include "sdk-abort-virtuals.inc"

int main(int argc, char** argv)
{
    if (argc != 5) return 1;
    std::ifstream cases(argv[1]), expected(argv[2]);
    std::string line, comparison;
    unsigned count{};
    while (std::getline(cases, line))
    {
        std::istringstream row(line); row >> std::hex;
        std::array<std::uint32_t, 14> values{};
        for (auto& value : values) if (!(row >> value)) return 3;
        std::string trailing; if (row >> trailing) return 3;
        if (values[0] >= Samples.size() || values[1] > 1 || values[2] > 1 || values[5] < 1 || values[5] > 5) return 3;
        if (!std::getline(expected, comparison)) return 4;
        Reset(); globals.maxClients = values[5];
        for (int i = 1; i <= 5; ++i)
        {
            edicts[i].free = (values[7] >> i) & 1;
            if (values[6] & (1u << i)) edicts[i].pvPrivateData = nullptr;
        }
        gun = {Float(values[11]), Float(values[12]), Float(values[13])};
        const PlaySfxRequest request{Samples[values[0]], {Float(values[8]), Float(values[9]), Float(values[10])},
            std::bit_cast<std::int32_t>(values[3]), static_cast<std::uint8_t>(values[4]), values[2] != 0,
            values[1] ? PlaySfxRules::Cooperative : PlaySfxRules::Direct};
        Check(Send(request).status == PlaySfxStatus::Sent && !began);
        std::istringstream words(comparison); words >> std::hex;
        std::vector<std::uint32_t> wanted;
        std::uint32_t word; while (words >> word) wanted.push_back(word);
        if (events != wanted)
        {
            std::cerr << "Original mismatch case " << count << '\n';
            for (auto e : events) std::cerr << std::hex << e << ' '; std::cerr << '\n' << comparison << '\n';
            return 5;
        }
        ++count;
    }
    if (count != 256) return 6;
    if (std::getline(expected, comparison)) return 7;
    Guards();
    const int effects = EffectReplay(argv[3], argv[4]);
    if (effects) return effects;
    PresentationGuards();
    std::cout << count << " original sound sender cases; 128 original shell/recoil cases; "
              << checks << " SDK checks; 0 failures\n";
}
