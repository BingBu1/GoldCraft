// SPDX-License-Identifier: GPL-3.0-or-later
#include "presentation.hpp"

#include <cstring>

namespace guest::cof::client
{
namespace
{
bool Supported(Weapon weapon) noexcept
{
    switch (weapon)
    {
    case Weapon::None: case Weapon::Glock: case Weapon::P345:
    case Weapon::Vp70: case Weapon::Revolver: return true;
    }
    return false;
}

// Original consumers have 256-byte suffix buffers. Reject host wire overflow
// instead of reproducing original stack corruption. This is transport safety,
// not an alternate playback default. The protocol codec separately bounds its
// message; a string_view containing NUL cannot represent the original string.
bool Rewrite(std::string_view source, std::string_view match, std::string_view prefix,
             std::array<char, 272>& buffer, std::string_view& output) noexcept
{
    if (source.empty() || source.size() > 255 || source.find('\0') != source.npos)
        return false;
    output = source;
    if (source.find(match) == source.npos)
        return true;
    const auto slash = source.find_first_of("/\\");
    if (slash == source.npos)
        return true;
    const auto suffix = source.substr(slash + 1);
    if (suffix.size() >= 256)
        return false;
    std::memcpy(buffer.data(), prefix.data(), prefix.size());
    std::memcpy(buffer.data() + prefix.size(), suffix.data(), suffix.size());
    output = {buffer.data(), prefix.size() + suffix.size()};
    return true;
}
}

void Presentation::BeginAuthority(AuthorityKey key, AimFovState initialFov) noexcept
{
    key_ = key;
    fov_ = initialFov;
    authority_ = true;
    received_ = sensitivityReady_ = false;
}

void Presentation::EndAuthority() noexcept
{
    authority_ = received_ = sensitivityReady_ = false;
}

Result Presentation::Receive(const DecodedState& state) noexcept
{
    if (!authority_)
        return Result::Inactive;
    if (!(state.key == key_))
        return Result::StaleAuthority;
    if (!Supported(state.weapon))
        return Result::InvalidWeapon;
    state_ = state;
    received_ = true;
    return Result::Applied;
}

bool Presentation::UpdateClientData(const FrameSettings& settings, AimFovOutput& output) noexcept
{
    if (!authority_ || !received_)
        return false;
    const AimFovFrame frame{state_.fovMode, settings.defaultFov, settings.clientTime,
        state_.customZoomSpeed, settings.zoomSpeed, settings.sensitivity,
        settings.zoomSensitivityRatio, settings.fovMultiplier};
    output = UpdateAimFov(fov_, frame);
    sensitivity_ = output.sensitivity;
    sensitivityReady_ = true;
    return true;
}

bool Presentation::Sensitivity(float& output) const noexcept
{
    if (!authority_ || !received_ || !sensitivityReady_)
        return false;
    output = sensitivity_;
    return true;
}

ViewModelPolicy Presentation::ViewModel() const noexcept
{
    const bool suppress = authority_ && received_ && state_.aimEnabled;
    return {suppress, suppress, suppress};
}

void Presentation::VideoInit() noexcept
{
    state_.aimEnabled = false;
}

Result Presentation::Sound(const DecodedSound& sound) const noexcept
{
    if (!authority_)
        return Result::Inactive;
    if (!(sound.key == key_))
        return Result::StaleAuthority;
    return DispatchSound(host_, {sound.sample, sound.mode, sound.enabled, sound.remote,
        sound.origin, sound.recipientIndex});
}

Result DispatchSound(Host host, const SoundMessageData& sound) noexcept
{
    std::array<char, 272> buffer;
    std::string_view sample;
    if (sound.remote)
    {
        if (!Rewrite(sound.sample, "weapons_new/", "weapons/", buffer, sample))
            return Result::InvalidSample;
        if (!host.remoteSound)
            return Result::MissingBackend;
        const int channel = sample.find("fire") != sample.npos || sample.find("shoot") != sample.npos ? 1 : 0;
        const RemoteSound request{sample, sound.origin, 0, channel, 1.0f, 0.8f, 0, 100};
        return host.remoteSound(host.context, request) ? Result::Applied : Result::BackendFailed;
    }
    if (!Rewrite(sound.sample, "weapons/", "weapons_new/", buffer, sample))
        return Result::InvalidSample;
    if (!host.localSound)
        return Result::MissingBackend;
    const LocalSound request{sample, sound.mode, sound.enabled != 0, sound.origin};
    return host.localSound(host.context, request) ? Result::Applied : Result::BackendFailed;
}

Result Presentation::Animation(AuthorityKey key, std::uint8_t sequence, int body) const noexcept
{
    if (!authority_ || !received_ || state_.weapon == Weapon::None)
        return Result::Inactive;
    if (!(key == key_))
        return Result::StaleAuthority;
    if (!host_.weaponAnimation)
        return Result::MissingBackend;
    return host_.weaponAnimation(host_.context, sequence, body) ? Result::Applied : Result::BackendFailed;
}
}
