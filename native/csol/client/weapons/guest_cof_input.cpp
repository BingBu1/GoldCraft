// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "guest_cof_input.hpp"
#include "weapon_inventory.hpp"
#include "../../../guest/cof/catalog.hpp"
#include <charconv>
#include <cstring>
#include <usercmd.h>
#include <in_buttons.h>

namespace csol::client
{
namespace
{
DWORD ownerThread{};
std::array<int, 2> keys{};
bool installed{}, pressed{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
bool Held() noexcept { return keys[0] || keys[1]; }
int Key() noexcept
{
    const char* text = engine.Cmd_Argv ? engine.Cmd_Argv(1) : nullptr;
    if (!text || !*text) return -1; // Manual console press/release.
    int key{};
    const auto end = text + std::strlen(text);
    const auto parsed = std::from_chars(text, end, key);
    return parsed.ec == std::errc{} && parsed.ptr == end && key > 0 ? key : 0;
}
void Down()
{
    if (!Owner() || !installed) return;
    const int key = Key();
    if (!key || keys[0] == key || keys[1] == key) return;
    const bool held = Held();
    if (!keys[0]) keys[0] = key;
    else if (!keys[1]) keys[1] = key;
    else return;
    if (!held) pressed = true;
}
void Up()
{
    if (!Owner() || !installed) return;
    const int key = Key();
    if (key == -1) keys = {};
    else for (auto& held : keys) if (held == key) held = 0;
}
bool Command(const char* name, xcommand_t callback) noexcept
{
    const auto* current = api->FindCmd(name);
    if (current) return current->function == callback;
    engine.pfnAddCommand(name, callback);
    current = api->FindCmd(name);
    return current && current->function == callback;
}
}
bool InitializeGuestCofInput() noexcept
{
    if (ownerThread && !Owner()) return false;
    ownerThread = GetCurrentThreadId();
    installed = false; keys = {}; pressed = false;
    if (!api || !api->FindCmd || !engine.pfnAddCommand || !engine.Cmd_Argv) return false;
    installed = Command("+cof_aim", Down) && Command("-cof_aim", Up);
    return installed;
}
void ResetGuestCofInput() noexcept { if (Owner()) { keys = {}; pressed = false; } }
void ShutdownGuestCofInput() noexcept
{
    if (!Owner()) return;
    ResetGuestCofInput(); installed = false; ownerThread = 0;
}
bool GuestCofInputReady() noexcept { return Owner() && installed; }
void GuestCofCreateMove(usercmd_s* command, int active, bool admitted) noexcept
{
    if (!GuestCofInputReady()) return;
    if (!active || !admitted || view.health <= 0 || view.spectator)
    { ResetGuestCofInput(); return; }
    if (!command) return;
    const auto& current = CurrentExtendedWeapon();
    if (!current.state || current.id != guest::cof::Pistols[0].hostId)
    { ResetGuestCofInput(); return; }
    // CoF10078670 maps +attack3 to0x40, +use to0x20. The host transports
    // only the extra aim bit; native Use/right-click and key bindings survive.
    if (Held() || pressed)
        command->buttons |= IN_CANCEL;
    pressed = false;
}
}
