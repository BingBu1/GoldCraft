// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/presentation.hpp"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

using namespace guest::cof;
using namespace guest::cof::client;
namespace
{
void Check(bool pass, int line)
{
    if (!pass) { std::fprintf(stderr, "failure at line %d\n", line); std::exit(1); }
}
#define CHECK(...) Check((__VA_ARGS__), __LINE__)
constexpr AuthorityKey Key{10, 20, 30};
struct Calls
{
    std::array<char, 300> sample{};
    RemoteSound remote{};
    LocalSound local{};
    int remoteCount{}, localCount{}, animations{}, sequence{}, body{};
    bool succeed{true};
    void Save(std::string_view text) noexcept
    {
        std::memcpy(sample.data(), text.data(), text.size()); sample[text.size()] = 0;
    }
    static bool Remote(void* context, const RemoteSound& request) noexcept
    {
        auto& self = *static_cast<Calls*>(context); self.remote = request;
        self.Save(request.sample); ++self.remoteCount; return self.succeed;
    }
    static bool Local(void* context, const LocalSound& request) noexcept
    {
        auto& self = *static_cast<Calls*>(context); self.local = request;
        self.Save(request.sample); ++self.localCount; return self.succeed;
    }
    static bool Animation(void* context, int sequence, int body) noexcept
    {
        auto& self = *static_cast<Calls*>(context); ++self.animations;
        self.sequence = sequence; self.body = body; return self.succeed;
    }
};
}

int main()
{
    Calls calls;
    Presentation service({&calls, Calls::Remote, Calls::Local, Calls::Animation});
    const DecodedState aimed{Key, Weapon::Glock, true, 50, 50.0f};
    FrameSettings frame{90, 10.01f, 100.0f, 2.5f, 1.2f, 1.0f};
    AimFovOutput output{123, 456.0f, 789.0f}; float sensitivity = 1000.0f;
    CHECK(service.Receive(aimed) == Result::Inactive);
    CHECK(!service.UpdateClientData(frame, output) && output.wholeFov == 123);
    CHECK(!service.Sensitivity(sensitivity) && sensitivity == 1000.0f);
    service.BeginAuthority(Key, {90.0f, 10.0f, 90.0f});
    CHECK(!service.UpdateClientData(frame, output));
    CHECK(service.Receive(aimed) == Result::Applied);
    CHECK(service.ViewModel().suppressBob && service.ViewModel().suppressIdle && service.ViewModel().suppressLag);
    CHECK(service.UpdateClientData(frame, output));
    CHECK(output.clientFov < 90.0f && output.clientFov > 50.0f);
    CHECK(service.Sensitivity(sensitivity) && sensitivity == output.sensitivity);
    const auto beforeVideo = service.FovState();
    service.VideoInit();
    CHECK(!service.ViewModel().suppressBob && !service.ViewModel().suppressIdle && !service.ViewModel().suppressLag);
    CHECK(service.FovState().current == beforeVideo.current);
    CHECK(service.FovState().previousTime == beforeVideo.previousTime);

    // The original CurWeapon does not reset smooth FOV on a switch. Only the
    // authoritative replacement mode/aim state changes; no new predictor.
    CHECK(service.Receive({Key, Weapon::P345, false, 90, 50.0f}) == Result::Applied);
    CHECK(service.FovState().current == beforeVideo.current);
    frame.clientTime += 0.01f;
    CHECK(service.UpdateClientData(frame, output) && output.clientFov > beforeVideo.current);
    const float beforeDeath = service.FovState().current;
    CHECK(service.Receive({Key, Weapon::None, false, 90, 50.0f}) == Result::Applied);
    CHECK(service.Animation(Key, 4, 0) == Result::Inactive);
    CHECK(service.FovState().current == beforeDeath);
    frame.clientTime += 0.02f; CHECK(service.UpdateClientData(frame, output));
    CHECK(output.clientFov >= beforeDeath);

    // Epoch/generation authorization is host policy, not guessed CoF fields.
    const AuthorityKey newLife{10, 21, 31};
    service.BeginAuthority(newLife, {-1.0f, 0.0f, 50.0f});
    CHECK(service.Receive(aimed) == Result::StaleAuthority);
    CHECK(!service.UpdateClientData(frame, output));
    CHECK(service.Receive({newLife, Weapon::Revolver, false, 90, 0.0f}) == Result::Applied);
    CHECK(service.UpdateClientData(frame, output) && output.clientFov == 90.0f);
    CHECK(service.Receive({newLife, static_cast<Weapon>(255), true, 50, 50}) == Result::InvalidWeapon);
    CHECK(service.ActiveWeapon() == Weapon::Revolver && !service.ViewModel().suppressBob);
    CHECK(service.Animation(Key, 38, 0) == Result::StaleAuthority);
    CHECK(service.Animation(newLife, 38, 2) == Result::Applied);
    CHECK(calls.animations == 1 && calls.sequence == 38 && calls.body == 2);

    DecodedSound sound{newLife, "weapons_new/glock/fire.wav", 2, 255, {1, 2, 3}, 1, 31};
    CHECK(service.Sound(sound) == Result::Applied);
    CHECK(std::string_view(calls.sample.data()) == "weapons/glock/fire.wav");
    CHECK(calls.remoteCount == 1 && calls.localCount == 0);
    CHECK(calls.remote.entity == 0 && calls.remote.channel == 1 && calls.remote.volume == 1.0f);
    CHECK(calls.remote.attenuation == 0.8f && calls.remote.flags == 0 && calls.remote.pitch == 100);
    CHECK(calls.remote.origin == sound.origin);
    sound.sample = "weapons_new/glock/magin.wav";
    CHECK(service.Sound(sound) == Result::Applied && calls.remote.channel == 0);
    sound.sample = "prefix/weapons_new/glock/shoot.wav";
    CHECK(service.Sound(sound) == Result::Applied);
    CHECK(std::string_view(calls.sample.data()) == "weapons/weapons_new/glock/shoot.wav");
    sound.remote = 0; sound.sample = "weapons/glock/fire.wav"; sound.mode = 1;
    CHECK(service.Sound(sound) == Result::Applied);
    CHECK(std::string_view(calls.sample.data()) == "weapons_new/glock/fire.wav");
    CHECK(calls.local.mode == 1 && calls.local.enabled && calls.local.origin == sound.origin);
    sound.mode = 2; sound.enabled = 0; CHECK(service.Sound(sound) == Result::Applied);
    CHECK(calls.local.mode == 2 && !calls.local.enabled);
    calls.succeed = false; CHECK(service.Sound(sound) == Result::BackendFailed);
    CHECK(service.Animation(newLife, 3, 0) == Result::BackendFailed); calls.succeed = true;
    const int previousLocalCount = calls.localCount;
    sound.key = Key; CHECK(service.Sound(sound) == Result::StaleAuthority);
    CHECK(calls.localCount == previousLocalCount); sound.key = newLife;
    sound.sample = {}; CHECK(service.Sound(sound) == Result::InvalidSample);
    sound.sample = std::string_view("a\0b", 3); CHECK(service.Sound(sound) == Result::InvalidSample);
    const std::array<char, 256> longName{};
    sound.sample = {longName.data(), longName.size()}; CHECK(service.Sound(sound) == Result::InvalidSample);
    sound.sample = "weapons/glock/fire.wav";
    Presentation missing({}); missing.BeginAuthority(newLife, {90, 0, 90});
    CHECK(missing.Sound(sound) == Result::MissingBackend);
    sound.remote = 1; CHECK(missing.Sound(sound) == Result::MissingBackend);
    CHECK(calls.localCount == previousLocalCount);
    service.EndAuthority(); CHECK(service.Sound(sound) == Result::Inactive);
    CHECK(service.Receive({newLife, Weapon::Vp70, true, 30, 50}) == Result::Inactive);
    CHECK(!service.Sensitivity(sensitivity) && !service.ViewModel().suppressBob);
    CHECK(service.ActiveWeapon() == Weapon::None);
    std::puts("presentation lifecycle, FOV timing, sound routing and backend failures passed");
}
