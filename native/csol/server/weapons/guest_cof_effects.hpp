// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "guest_cof_runtime.hpp"
#include "ehandle.h"
#include "native/guest/cof/authority.hpp"
#include "native/guest/cof/server/sound_dispatch.hpp"
#include <array>

namespace csol::server::cof
{
// Explicit scope of the native-CS integration. CoF map-authored clothing and
// single-player tutorials are not represented by unrelated CS entvars.
struct GlockEffectPolicy
{
    bool nativeMultiplayer{}, noCustomClothing{};
    // Supplied only with the actual native SetAnimation reload hook and typed
    // runtime stage consumer installed. Other weapon families do not require it.
    bool nativeRevolverReload{};
};

// One instance per retained map, on the authoritative thread. It must outlive
// all native entities/callbacks of that map, just like GlockRuntime's host.
class GlockEffects final
{
public:
    bool Prepare(ResourceAuthority, const GlockResources&, GlockEffectPolicy) noexcept;
    void EndMap() noexcept;
    bool Current() const noexcept;
    std::uint32_t Capabilities() const noexcept;
    std::uint32_t P345Capabilities() const noexcept;
    std::uint32_t VP70Capabilities() const noexcept;
    std::uint32_t RevolverCapabilities(const RevolverResources&) const noexcept;
    bool FillPrepared(const GlockResources&, guest::cof::server::GlockPrepared&) noexcept;
    bool FillPrepared(const P345Resources&, guest::cof::server::P345Prepared&) noexcept;
    bool FillPrepared(const VP70Resources&, guest::cof::server::VP70Prepared&) noexcept;
    bool FillPrepared(const RevolverResources&,guest::cof::server::RevolverPrepared&) noexcept;
    bool Dispatch(const GlockResources&, const OwnerBinding&, CBasePlayer&,
        CBasePlayerWeapon&, const guest::cof::server::GlockEvent&) noexcept;
    bool Dispatch(const P345Resources&,const OwnerBinding&,CBasePlayer&,
        CBasePlayerWeapon&,const guest::cof::server::P345Event&) noexcept;
    bool Dispatch(const VP70Resources&,const OwnerBinding&,CBasePlayer&,
        CBasePlayerWeapon&,const guest::cof::server::VP70Event&,bool firstDrawn) noexcept;
    bool Dispatch(const RevolverResources&,const OwnerBinding&,CBasePlayer&,
        CBasePlayerWeapon&,const guest::cof::server::RevolverEvent&,RevolverOwnerState) noexcept;
    // Call after real native item processing, and on life/ownership retirement.
    // Ordinary CS weapons receive an explicit release of CoF FOV/view ownership.
    bool SynchronizeSelection(CBasePlayer&) noexcept;
    bool RetirePlayer(CBasePlayer&) noexcept;
    // Actual local outgoing UpdateClientData snapshot; repeated independently
    // of scalar changes so unreliable packet loss recovers on the next one.
    bool CrosshairSnapshot(CBasePlayer&,const guest::cof::PlaySfxCall&) noexcept;

private:
    struct Client
    {
        guest::cof::AuthorityKey key{};
        std::uint32_t revision{}, nextInstance{}, snapshotSequence{};
        std::uint64_t model{};
        EHANDLE weapon;
        int ownerSerial{};
        bool active{}, clothingInitialized{};
        bool aimEnabled{};
        std::uint16_t fovMode{};
        std::int16_t zoomSpeedEighths{};
        std::optional<float> revolverAngle{};
    };
    struct Lease;
    struct WeaponResources
    {
        std::uint64_t generation{};
        int viewModelString{};
        std::array<unsigned short,2> events{};
        int shellModel{};
    };
    bool DispatchWeapon(const WeaponResources&,const OwnerBinding&,CBasePlayer&,CBasePlayerWeapon&,
        const guest::cof::server::GlockEvent&,unsigned originalWeapon,const char* fireSound,std::uint32_t capabilities,
        std::optional<float> revolverAngle={}) noexcept;
    static bool Active(void*) noexcept;
    static void MeleeRecoil(void*, CBasePlayer&, const Vector&) noexcept;
    bool Presentation(Lease&, bool enabled, int fov, float speed,bool skipIdentical=false) noexcept;
    bool Release(CBasePlayer&, const guest::cof::PlaySfxCall&) noexcept;
    ResourceAuthority authority_{};
    GlockEffectPolicy policy_{};
    GlockResources resources_{};
    int presentation_{}, sound_{}, control_{}, particle_{}, decal_{}, shake_{}, crosshair_{},ammoX_{};
    std::array<Client, GlockOwnerRuntime::SlotCount> clients_{};
};
}
