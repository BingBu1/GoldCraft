// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "../aim_fov.hpp"
#include "../authority.hpp"
#include "../sound_protocol.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace guest::cof::client
{
// Host transport identity, not original CoF object fields. The root bridge
// authenticates this identity and begins each ownership interval explicitly.
using ::guest::cof::AuthorityKey;

enum class Weapon : std::uint8_t { None = 0, Glock = 3, P345 = 7, Vp70 = 19, Revolver = 21 };

struct DecodedState
{
    AuthorityKey key;
    Weapon weapon;
    bool aimEnabled;
    std::int32_t fovMode;
    float customZoomSpeed; // Original CameraZoom coord16 already decoded.
};

struct FrameSettings
{
    std::int32_t defaultFov;
    float clientTime;
    float zoomSpeed;
    float sensitivity;
    float zoomSensitivityRatio;
    float fovMultiplier;
};

struct ViewModelPolicy
{
    bool suppressBob;
    bool suppressIdle;
    bool suppressLag;
};

struct DecodedSound
{
    AuthorityKey key;
    std::string_view sample;
    std::uint8_t mode;
    std::uint8_t enabled;
    std::array<float, 3> origin;
    std::uint8_t remote;
    std::int16_t recipientIndex; // Original consumes this field but never uses it.
};

struct RemoteSound
{
    std::string_view sample;
    std::array<float, 3> origin;
    int entity;
    int channel;
    float volume;
    float attenuation;
    int flags;
    int pitch;
};

struct LocalSound
{
    std::string_view sample;
    std::uint8_t mode;
    bool enabled;
    std::array<float, 3> origin;
};

enum class Result { Applied, Inactive, StaleAuthority, InvalidWeapon, InvalidSample, MissingBackend, BackendFailed };

// Callbacks consume string_views synchronously; never retain borrowed memory.
// The local backend must implement original10080410's independent voice/slot
// semantics. Supplying EV_PlaySound in its place requires a documented host
// deviation. Missing local support is reported, never silently downgraded.
struct Host
{
    void* context{};
    bool (*remoteSound)(void*, const RemoteSound&) noexcept {};
    bool (*localSound)(void*, const LocalSound&) noexcept {};
    bool (*weaponAnimation)(void*, int sequence, int body) noexcept {};
};

// Global original PlaySFX consumer. The caller validates the actual connection
// and packet; playback does not depend on the local equipped weapon or life.
Result DispatchSound(Host host, const SoundMessageData& sound) noexcept;

class Presentation
{
public:
    explicit Presentation(Host host) noexcept : host_(host) {}
    void BeginAuthority(AuthorityKey key, AimFovState initialFov) noexcept;
    void EndAuthority() noexcept;
    Result Receive(const DecodedState& state) noexcept;
    // Call once at HUD_UpdateClientData, after the downstream CS callback.
    // Returns false without modifying output when this service has no state.
    bool UpdateClientData(const FrameSettings& settings, AimFovOutput& output) noexcept;
    bool Sensitivity(float& output) const noexcept;
    ViewModelPolicy ViewModel() const noexcept;
    void VideoInit() noexcept;
    Result Sound(const DecodedSound& sound) const noexcept;
    // Reliable server event only; caller owns replay/delivery policy. No local
    // attack, reload, idle timer or sequence selection exists in this service.
    Result Animation(AuthorityKey key, std::uint8_t sequence, int body) const noexcept;
    const AimFovState& FovState() const noexcept { return fov_; }
    Weapon ActiveWeapon() const noexcept { return received_ ? state_.weapon : Weapon::None; }

private:
    Host host_;
    AuthorityKey key_{};
    DecodedState state_{};
    AimFovState fov_{};
    float sensitivity_{};
    bool authority_{}, received_{}, sensitivityReady_{};
};
}
