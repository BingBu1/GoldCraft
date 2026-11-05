// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "weapons.hpp"
#include "crosshair.hpp"
#include "../weapon_slots.hpp"
#include "../hud_style.hpp"

namespace csol::client
{
// A compiled weapon owns its callbacks and presentation metadata. The shared
// controller needs this interface, not the headers of individual weapons.
struct WeaponModule
{
    std::span<const int> ids;
    // Preserve the established predictor chain, including inactive cleanup.
    int order{};
    void (*initialize)(){};
    void (*reset)(){};
    void (*afterReset)(){};
    void (*videoInit)(){};
    void (*shutdown)(){};
    // Release extended presentation before stock CurWeapon installs its own.
    void (*nativeWeaponChanged)(int id){};
    void (*receive)(const WeaponCurrent&){};
    bool (*predict)(local_state_t*,local_state_t*,usercmd_t*,int,double,unsigned){};
    void (*diagnostics)(std::ostream&){};

    hud_style::AmmoKind ammoKind{hud_style::AmmoKind::None};
    const char* ammoSprite{};
    wrect_t ammoRect{};
    std::array<int,3> ammoColor{255,255,255};
    bool dynamicCrosshair{true};
    crosshair::Parameters crosshair{};
    bool (*crosshairShot)(){};
    int (*predictedClip)(){};
    int (*predictedReserve)(){};
    int (*predictedSecondary)(){};
    void (*hudState)(hud_style::WeaponState&){};
    bool (*secondaryVisible)(int hidden,int intermission){};
    void (*drawHud)(int hidden,int intermission,bool drawAmmo){};
};

// Construct only at static module registration, never while playing. No engine
// API is called here; the descriptor must outlive the client DLL.
class WeaponModuleRegistration
{
public:
    explicit WeaponModuleRegistration(const WeaponModule& module) noexcept;
};
std::span<const WeaponModule* const> WeaponModules() noexcept;
const WeaponModule* FindWeaponModule(int id) noexcept;
void InitializeWeaponModules();
void ResetWeaponModules();
void WeaponModulesVideoInit();
void ShutdownWeaponModules();
}
