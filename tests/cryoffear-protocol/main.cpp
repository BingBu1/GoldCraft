// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/presentation_protocol.hpp"
#include "../../native/guest/cof/catalog.hpp"
#include "../../native/guest/cof/particle_protocol.hpp"
#include "../../native/guest/cof/client/presentation.hpp"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace guest::cof;
namespace
{
unsigned checks{}, failures{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "%s\n", message); }
}

void StateLifecycle()
{
    const PresentationState first{{0x01020304, 5, 6}, 7, 7, true, 50, 2800};
    StatePacket packet{};
    Check(EncodeState(first, packet), "encode original P345 aim snapshot");
    // Literal independent wire fixture: version/ID/flags/reserved, then LE
    // session/life/instance/revision/FOV/Coord16. No native struct memcpy.
    constexpr std::array<unsigned char, StateBytes> expected{
        1,7,1,0,4,3,2,1,5,0,0,0,6,0,0,0,7,0,0,0,50,0,240,10};
    Check(std::memcmp(packet.data(), expected.data(), expected.size()) == 0, "stable wire fixture");
    PresentationState output{};
    Check(DecodeState(packet, output) && output == first, "decode literal packet");
    PresentationReceiver receiver;
    Check(receiver.Receive(packet, output) == StateAcceptance::Accepted, "initial state");
    const auto unchanged = output;
    Check(receiver.Receive(packet, output) == StateAcceptance::Replayed && output == unchanged,
        "duplicate does not repeat effects or mutate output");

    client::Presentation presentation({});
    presentation.BeginAuthority(output.key, {-1, 0, 0});
    Check(presentation.Receive({output.key, client::Weapon::P345, output.aimEnabled,
        output.fovMode, static_cast<float>(output.zoomSpeedEighths) * .125f}) == client::Result::Applied,
        "wire feeds real presentation service");
    AimFovOutput fov{};
    Check(presentation.UpdateClientData({90, 12, 350, 3, 1.2f, 1}, fov) && fov.wholeFov == 50,
        "aim state drives service FOV");

    auto next = first;
    next.revision = 8; next.aimEnabled = false; next.fovMode = 90;
    Check(EncodeState(next, packet) && receiver.Receive(packet, output) == StateAcceptance::Accepted,
        "new revision exits aim");
    const auto send = [&](PresentationState value, StateAcceptance result, const char* name)
    {
        StatePacket data{}; PresentationState old = output;
        Check(EncodeState(value, data), "test fixture encodes");
        Check(receiver.Receive(data, output) == result, name);
        if (result != StateAcceptance::Accepted) Check(output == old, "rejection is transactional");
    };
    next.key.generation = 7; next.revision = 1; next.originalWeapon = 3;
    send(next, StateAcceptance::Accepted, "new weapon instance resets revision");
    send(first, StateAcceptance::PreviousInstance, "late old weapon cannot restore aim");
    next.key.lifeEpoch = 6; next.key.generation = 1; next.originalWeapon = 0;
    send(next, StateAcceptance::Accepted, "death/respawn invalidates weapon scope");
    auto oldLife = next; oldLife.key.lifeEpoch = 5; oldLife.key.generation = 1000;
    send(oldLife, StateAcceptance::PreviousLife, "large old-instance ID cannot cross life");
    next.key.sessionEpoch++; next.key.lifeEpoch = 1;
    send(next, StateAcceptance::Accepted, "new map session permits low life counter");
    send(first, StateAcceptance::PreviousSession, "old map snapshot cannot resurrect presentation");
    receiver.Reset();
    Check(!receiver.Current(), "connection teardown clears only transport cache");
    send(first, StateAcceptance::Accepted, "new server connection may restart counters");
}

void InvalidPackets()
{
    const PresentationState value{{1,2,3}, 4, 19, false, 65535,
        std::numeric_limits<std::int16_t>::min()};
    StatePacket packet{};
    Check(EncodeState(value, packet), "full-width fields encode");
    PresentationState output{};
    Check(DecodeState(packet, output) && output == value, "signed Coord16 and full FOV preserved");
    for (std::size_t length = 0; length < StateBytes; ++length)
    {
        const auto before = output;
        Check(!DecodeState(std::span(packet).first(length), output) && output == before,
            "every truncation rejects without modifying output");
    }
    std::array<std::byte, StateBytes + 1> trailing{};
    std::memcpy(trailing.data(), packet.data(), packet.size());
    Check(!DecodeState(trailing, output), "trailing byte rejects");
    for (auto index : {0U, 1U, 2U, 3U})
    {
        auto corrupt = packet; corrupt[index] = std::byte{255};
        const auto before = output;
        Check(!DecodeState(corrupt, output) && output == before, "unknown version/id/flags/reserved rejects");
    }
    for (auto offset : {4U, 8U, 12U, 16U})
    {
        auto corrupt = packet;
        for (unsigned i = 0; i < 4; ++i) corrupt[offset + i] = std::byte{};
        Check(!DecodeState(corrupt, output), "zero scope or revision rejects");
    }
    auto corrupt = packet; corrupt[1] = std::byte{}; corrupt[2] = std::byte{1};
    Check(!DecodeState(corrupt, output), "no weapon cannot request aim");
    StatePacket sentinel; sentinel.fill(std::byte{0xA5});
    auto invalid = value; invalid.key.generation = 0;
    auto before = sentinel;
    Check(!EncodeState(invalid, sentinel) && sentinel == before, "invalid encode is transactional");
}

void RevolverTransport()
{
    PresentationState state{{0x01020304,5,6},7,21,true,50,2800,72.0F};
    RevolverStatePacket packet{};
    constexpr std::array<unsigned char, RevolverStateBytes> expected{
        2,21,1,0,4,3,2,1,5,0,0,0,6,0,0,0,7,0,0,0,50,0,240,10,0,0,144,66};
    Check(EncodeState(state,packet) && !std::memcmp(packet.data(),expected.data(),expected.size()),
        "literal 28-byte revolver packet contains the original float angle");
    PresentationState output{};
    Check(DecodeState(packet,output) && output==state,"revolver snapshot roundtrip");
    for (std::size_t length=0; length<packet.size(); ++length)
    {
        const auto before=output;
        Check(!DecodeState(std::span(packet).first(length),output) && output==before,
            "all revolver truncations reject transactionally, including legacy length");
    }
    for (unsigned index : {0U,1U,2U,3U})
    {
        auto corrupt=packet;corrupt[index]=std::byte{255};const auto before=output;
        Check(!DecodeState(corrupt,output) && output==before,"invalid extended header preserves state");
    }
    auto wrong=packet;wrong[0]=std::byte{1};
    Check(!DecodeState(wrong,output),"legacy protocol cannot carry an unannounced trailer");
    wrong=packet;wrong[1]=std::byte{3};
    Check(!DecodeState(wrong,output),"another pistol cannot own revolver angle");
    wrong=packet;wrong[1]=std::byte{};wrong[2]=std::byte{};
    Check(!DecodeState(wrong,output),"release cannot leave a cylinder extension");
    StatePacket legacy{};legacy.fill(std::byte{0xA5});const auto oldLegacy=legacy;
    Check(!EncodeState(state,legacy) && legacy==oldLegacy,"revolver cannot silently lose its angle");
    RevolverStatePacket sentinel{};sentinel.fill(std::byte{0xA5});const auto oldSentinel=sentinel;
    auto invalid=state;invalid.revolverAngle.reset();
    Check(!EncodeState(invalid,sentinel) && sentinel==oldSentinel,"missing explicit seed is invalid");
    invalid=state;invalid.originalWeapon=3;
    Check(!EncodeState(invalid,sentinel) && sentinel==oldSentinel,"angle for another pistol is invalid");

    // Raw bit patterns preserve signed zero, subnormal, NaN payload and x86
    // conversion inputs; transport does not clamp original controller math.
    std::uint32_t bits=0;
    for (unsigned i=0;i<2048;++i)
    {
        if (i<8) bits=std::array<std::uint32_t,8>{0,0x80000000,1,0x7F800000,
            0xFF800000,0x7FC12345,0xFFC12345,0x7F7FFFFF}[i];
        else bits=bits*1664525U+1013904223U;
        state.revolverAngle=std::bit_cast<float>(bits);
        Check(EncodeState(state,packet) && DecodeState(packet,output) && output.revolverAngle &&
            std::bit_cast<std::uint32_t>(*output.revolverAngle)==bits,"original float bits survive the wire");
    }
    state.revolverAngle=144.0F;
    PresentationReceiver receiver;
    Check(EncodeState(state,packet) && receiver.Receive(packet,output)==StateAcceptance::Accepted,
        "revolver uses existing authority stream");
    const auto accepted=output;
    Check(receiver.Receive(packet,output)==StateAcceptance::Replayed && output==accepted,
        "duplicate extended packet cannot replay or mutate");
    const auto reject=[&](PresentationState next,StateAcceptance expectedResult)
    {
        const auto before=output;
        Check(EncodeState(next,packet) && receiver.Receive(packet,output)==expectedResult && output==before,
            "old extended scope preserves current state");
    };
    auto stale=state;--stale.key.sessionEpoch;stale.key.lifeEpoch=1000;
    reject(stale,StateAcceptance::PreviousSession);
    stale=state;--stale.key.lifeEpoch;stale.key.generation=1000;
    reject(stale,StateAcceptance::PreviousLife);
    stale=state;--stale.key.generation;stale.revision=1000;
    reject(stale,StateAcceptance::PreviousInstance);
    PresentationState release{state.key,state.revision+1,0,false,0,0};
    Check(EncodeState(release,legacy) && receiver.Receive(legacy,output)==StateAcceptance::Accepted &&
        !output.revolverAngle,"legacy release clears the cylinder within the same revision stream");
    reject(state,StateAcceptance::Replayed);
    state.key.generation++;state.revision=1;
    Check(EncodeState(state,packet) && receiver.Receive(packet,output)==StateAcceptance::Accepted,
        "new instance accepts authenticated explicit cylinder state");
    receiver.Reset();Check(!receiver.Current(),"connection teardown removes cylinder authority");
}

void Catalog()
{
    for (const auto& definition : Pistols)
    {
        Check(FindPistol(definition.hostId) == &definition && FindPistol(definition.identity) == &definition,
            "constant-time independent identity lookup");
        Check(FindOriginalPistol(definition.originalId)==&definition,"explicit original-to-host identity mapping");
        Check(definition.hostId >= 32 && definition.hostId < 1022 && OriginalPistolId(definition.originalId),
            "guest IDs do not alias legacy or reserved knives");
        Check(definition.displayName.starts_with("客串武器·") &&
            definition.classname.starts_with("weapon_cof_") && definition.ammunition.starts_with("cof_"),
            "guest purchase/resource identity is namespaced");
    }
    for (int invalid : {std::numeric_limits<int>::min(), -1, 0, 3, 7, 31, 895, 900, 1023,
                        std::numeric_limits<int>::max()})
        Check(!FindPistol(invalid), "invalid host IDs cannot index the catalog");
    Check(!FindPistol(Pistol::Count), "invalid typed identity rejects");
    for(int invalid:{-1,0,1,4,18,20,22,896,897,898,899,std::numeric_limits<int>::max()})
        Check(!FindOriginalPistol(invalid),"host and unrelated identities cannot alias original pistol IDs");
}

void Particles()
{
    const ParticleMessageData add{ParticleOperation::Add, 7, false, -32768};
    ParticlePacket packet{};
    Check(EncodeParticle(add, packet), "local gunsmoke transport encodes");
    constexpr std::array<unsigned char, ParticleBytes> expected{1,1,7,0,0,128,0,0};
    Check(!std::memcmp(packet.data(), expected.data(), packet.size()), "literal particle wire fixture");
    ParticleMessageData decoded{};
    Check(DecodeParticle(packet, decoded) && decoded == add, "particle round trip");
    for (const auto& value : {
        ParticleMessageData{ParticleOperation::Add, 3, false, 0},
        ParticleMessageData{ParticleOperation::Add, 19, false, 32767},
        ParticleMessageData{ParticleOperation::Add, 21, false, 1},
        ParticleMessageData{ParticleOperation::DeleteOwner, 0, false, 0},
        ParticleMessageData{ParticleOperation::KillLocal, 0, false, 0},
        ParticleMessageData{ParticleOperation::KillLocal, 0, true, 0}})
        Check(EncodeParticle(value, packet) && DecodeParticle(packet, decoded) && decoded == value,
            "supported commands include viewmodel owner zero and both kill modes");
    for (int word = -32768; word <= 32767; ++word)
    {
        const ParticleMessageData value{ParticleOperation::DeleteOwner, 0, false, std::int16_t(word)};
        Check(EncodeParticle(value, packet) && DecodeParticle(packet, decoded) && decoded == value,
            "all original owner short words preserved");
        const auto camera = EncodeViewEntity(std::int16_t(word));
        std::int16_t cameraEntity{};
        Check(DecodeViewEntity(camera, cameraEntity) && cameraEntity == word,
            "all original ViewEntity signed short words preserved");
    }
    auto camera = EncodeViewEntity(-123);
    std::int16_t cameraEntity{19};
    for (std::size_t size = 0; size < camera.size(); ++size)
        Check(!DecodeViewEntity(std::span(camera).first(size), cameraEntity) && cameraEntity == 19,
            "truncated ViewEntity preserves output");
    camera[0] = std::byte{2};
    Check(!DecodeViewEntity(camera, cameraEntity) && cameraEntity == 19,
        "unknown ViewEntity version preserves output");
    camera = EncodeViewEntity(-123); camera[1] = std::byte{1};
    Check(!DecodeViewEntity(camera, cameraEntity), "ViewEntity reserved field rejected");
    Check(EncodeParticle(add, packet), "restore add fixture");
    for (std::size_t size = 0; size < packet.size(); ++size)
    {
        const auto before = decoded;
        Check(!DecodeParticle(std::span(packet).first(size), decoded) && decoded == before,
            "particle truncation is transactional");
    }
    for (const auto index : {0U,1U,2U,3U,6U,7U})
    {
        auto corrupted = packet;
        corrupted[index] = std::byte{255};
        const auto before = decoded;
        Check(!DecodeParticle(corrupted, decoded) && decoded == before,
            "unknown particle version/operation/model/flags/attachment rejected");
    }
    std::array<std::byte, ParticleBytes + 1> oversized{};
    Check(!DecodeParticle(oversized, decoded), "trailing particle byte rejected");
    for (const auto& value : {
        ParticleMessageData{ParticleOperation(255), 0, false, 0},
        ParticleMessageData{ParticleOperation::Add, 0, false, 0},
        ParticleMessageData{ParticleOperation::Add, 7, true, 0},
        ParticleMessageData{ParticleOperation::DeleteOwner, 7, false, 1},
        ParticleMessageData{ParticleOperation::DeleteOwner, 0, true, 1},
        ParticleMessageData{ParticleOperation::KillLocal, 0, true, 1},
        ParticleMessageData{ParticleOperation::KillLocal, 3, false, 0}})
    {
        const auto before = packet;
        Check(!EncodeParticle(value, packet) && packet == before, "invalid particle command preserves output");
    }
}

struct SoundCapture
{
    std::array<char, 272> sample{};
    client::RemoteSound remote{};
    client::LocalSound local{};
    unsigned localCalls{}, remoteCalls{};
    bool succeeds{true};
    void Save(std::string_view path)
    {
        sample.fill(0);
        std::memcpy(sample.data(), path.data(), path.size());
    }
};

bool Remote(void* context, const client::RemoteSound& sound) noexcept
{
    auto& capture = *static_cast<SoundCapture*>(context);
    capture.Save(sound.sample);
    capture.remote = sound;
    ++capture.remoteCalls;
    return capture.succeeds;
}
bool Local(void* context, const client::LocalSound& sound) noexcept
{
    auto& capture = *static_cast<SoundCapture*>(context);
    capture.Save(sound.sample);
    capture.local = sound;
    ++capture.localCalls;
    return capture.succeeds;
}

void GlobalSound()
{
    SoundPacket packet{};
    const SoundWire input{"weapons/p345_fire.wav", 2, 255, 9, {-32768, 32767, -1}, -32768};
    Check(EncodeSound(input, packet), "global sound encodes");
    constexpr std::array<unsigned char, SoundHeaderBytes> header{1,2,255,9,0,128,255,127,255,255,0,128};
    Check(std::memcmp(packet.bytes.data(), header.data(), header.size()) == 0,
        "independent literal sound header");
    SoundMessageData output{};
    Check(DecodeSound(packet.View(), output) && output.sample == input.sample && output.mode == 2 &&
        output.enabled == 255 && output.remote == 9 && output.recipientIndex == -32768 &&
        output.origin == std::array<float,3>{-4096.0f,4095.875f,-0.125f}, "original Coord16 consumer fields");
    Check(output.sample.data() == reinterpret_cast<const char*>(packet.bytes.data() + SoundHeaderBytes),
        "decoder borrows sample without allocation");

    SoundCapture capture;
    const client::Host host{&capture, Remote, Local, nullptr};
    client::Presentation inactive(host);
    Check(inactive.Sound({{},input.sample,2,255,{},9,-32768}) == client::Result::Inactive,
        "scoped presentation remains explicitly inactive");
    Check(client::DispatchSound(host, output) == client::Result::Applied && capture.remoteCalls == 1 &&
        capture.localCalls == 0 && !std::strcmp(capture.sample.data(), "weapons/p345_fire.wav"),
        "global remote sound works with no local weapon authority");
    Check(capture.remote.entity == 0 && capture.remote.channel == 1 && capture.remote.volume == 1.0f &&
        capture.remote.attenuation == 0.8f && capture.remote.flags == 0 && capture.remote.pitch == 100 &&
        capture.remote.origin == output.origin, "original remote SDK arguments; recipient is not entity");

    output.sample = "weapons_new/p345_reload.wav";
    Check(client::DispatchSound(host, output) == client::Result::Applied && capture.remote.channel == 0 &&
        !std::strcmp(capture.sample.data(), "weapons/p345_reload.wav"), "remote path rewrite");
    output.remote = 0;
    output.sample = input.sample;
    Check(client::DispatchSound(host, output) == client::Result::Applied && capture.localCalls == 1 &&
        capture.local.mode == 2 && capture.local.enabled && capture.local.origin == output.origin &&
        !std::strcmp(capture.sample.data(), "weapons_new/p345_fire.wav"), "global local mode and rewrite");
    capture.succeeds = false;
    Check(client::DispatchSound(host, output) == client::Result::BackendFailed,
        "backend failure is not successful playback");
    Check(client::DispatchSound({}, output) == client::Result::MissingBackend,
        "absent local backend cannot fall back to CS audio");
    output.remote = 1;
    Check(client::DispatchSound({}, output) == client::Result::MissingBackend,
        "absent remote backend is explicit");

    std::array<char, SoundSampleLimit + 1> limit{};
    limit.fill('a');
    SoundWire boundary{std::string_view(limit.data(), SoundSampleLimit), 255, 255, 255, {}, 32767};
    Check(EncodeSound(boundary, packet) && packet.size == SoundPacketLimit &&
        DecodeSound(packet.View(), output), "maximum actual ReHLDS message size");
    const auto beforePacket = packet;
    boundary.sample = std::string_view(limit.data(), limit.size());
    Check(!EncodeSound(boundary, packet) && packet.bytes == beforePacket.bytes && packet.size == beforePacket.size,
        "oversize encode preserves output");
    boundary.sample = {};
    Check(!EncodeSound(boundary, packet), "empty sample rejects");
    constexpr char embedded[]{'a',0,'b'};
    boundary.sample = std::string_view(embedded, sizeof(embedded));
    Check(!EncodeSound(boundary, packet), "embedded NUL rejects");
    Check(EncodeSound(input, packet), "restore literal fixture");
    for (std::size_t length = 0; length < packet.size; ++length)
    {
        const auto before = output;
        Check(!DecodeSound(packet.View().first(length), output) && output.sample == before.sample &&
            output.origin == before.origin, "all packet truncations reject transactionally");
    }
    auto corrupt = packet;
    corrupt.bytes[0] = std::byte{99};
    Check(!DecodeSound(corrupt.View(), output), "unknown sound protocol rejects");
    corrupt = packet;
    corrupt.bytes[SoundHeaderBytes + 2] = std::byte{};
    Check(!DecodeSound(corrupt.View(), output), "early terminator plus hidden trailing payload rejects");
    corrupt = packet;
    ++corrupt.size;
    Check(!DecodeSound(corrupt.View(), output), "extra trailing terminator rejects");

    // Exhaust the signed coordinate domain; every word must survive transport
    // and reach exactly the short->float eighths read by the original client.
    SoundWire coordinate{"x",0,0,0,{},0};
    for (int word = -32768; word <= 32767; ++word)
    {
        coordinate.originEighths = {std::int16_t(word), std::int16_t(word), std::int16_t(word)};
        const float expected = float(double(word) / 8.0);
        Check(EncodeSound(coordinate, packet) && DecodeSound(packet.View(), output) &&
            std::bit_cast<std::uint32_t>(output.origin[0]) == std::bit_cast<std::uint32_t>(expected) &&
            output.origin[1] == expected && output.origin[2] == expected,
            "complete original coordinate representation");
    }
}
}

int main()
{
    StateLifecycle(); InvalidPackets(); RevolverTransport(); Catalog(); GlobalSound(); Particles();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
