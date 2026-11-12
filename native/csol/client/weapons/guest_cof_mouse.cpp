// SPDX-License-Identifier: GPL-3.0-or-later
#include "../runtime.hpp"
#include "../resource_refresh.hpp"
#include "guest_cof_mouse.hpp"
#include "../../../guest/cof/client/mouse_input.hpp"
#include "../../../include/goldcraft/connection_api.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace csol::client
{
namespace
{
hook_t* hook{};
void *nativeStage{}, *resume{};
const GoldCraftConnectionAPI* connection{};
DWORD ownerThread{};
std::uint64_t serial{1}, frameGeneration{};
guest::cof::AuthorityKey authority{};
std::int32_t* history{};
std::int32_t* accumulated{};
float* deltas{};
cvar_t *filter{}, *sensitivity{}, *yaw{};
int wholeFov{}, defaultFov{};
float hudSensitivity{};
bool enabled{}, retiring{}, inHook{}, installing{};
bool Owner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
void Advance() noexcept
{
    if (serial) serial = serial == std::numeric_limits<std::uint64_t>::max() ? 0 : serial + 1;
}
bool Range(const void* pointer, std::size_t size) noexcept
{
    if (!pointer || !api || !api->GetClientBase || !api->GetClientSize) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(api->GetClientBase());
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto bytes = api->GetClientSize();
    return address >= base && address - base <= bytes && size <= bytes - (address - base);
}
void* Resolve(const char* name) noexcept
{
    void* result{};
    return api->ResolveGameSymbol(api->GetClientBase(), name, MH_GAMESYMBOL_KIND_PATCH, &result) == MH_GAMESYMBOL_OK &&
        Range(result, 24) ? result : nullptr;
}
template<class T> T* Operand(const void* code, std::size_t offset, std::size_t bytes) noexcept
{
    T* pointer{};
    std::memcpy(&pointer, static_cast<const std::byte*>(code) + offset, sizeof(pointer));
    return Range(pointer, bytes) && reinterpret_cast<std::uintptr_t>(pointer) % alignof(T) == 0 ? pointer : nullptr;
}
bool Prefix(const void* code, std::initializer_list<unsigned char> bytes) noexcept
{
    return code && std::memcmp(code, bytes.begin(), bytes.size()) == 0;
}
void Remove() noexcept
{
    if (hook) RemoveHook(hook);
    hook = nullptr; nativeStage = resume = nullptr;
    history = nullptr; accumulated = nullptr; deltas = nullptr;
    filter = sensitivity = yaw = nullptr; connection = nullptr;
    retiring = enabled = false; ownerThread = 0;
}
bool __cdecl Process(int x, int y) noexcept
{
    if (!Owner() || !enabled || retiring || inHook || !serial || !guest::cof::Valid(authority) ||
        !ResourceReadsAllowed() || !connection || view.spectator || view.intermission || view.paused ||
        view.viewentity != view.playernum+1) return false;
    struct Call final { Call() noexcept { inHook=true; } ~Call() { inHook=false; } } call;
    const auto revision = serial;
    const auto generation = connection->generation();
    if (!enabled || retiring || serial != revision || !generation || generation != frameGeneration) return false;
    guest::cof::client::MouseHistory previous;
    std::memcpy(&previous, history, sizeof(previous));
    const auto result = guest::cof::client::ScaleMouse(previous,
        {x, y, filter->value, hudSensitivity, sensitivity->value, wholeFov, defaultFov});
    // generation() is the only external callback. Finish authentication before
    // committing any actual native input field; a cancelled call stays native.
    if (connection->generation() != generation || !enabled || retiring || serial != revision) return false;
    std::memcpy(history, &result.history, sizeof(result.history));
    constexpr std::array<std::int32_t,2> cleared{};
    std::memcpy(accumulated, cleared.data(), sizeof(cleared));
    const std::array<float,2> scaled{static_cast<float>(result.x), static_cast<float>(result.y)};
    std::memcpy(deltas, scaled.data(), sizeof(scaled));
    return true;
}
// Exact native1003605D entry: ESI/EDI are already acquired int32 deltas.
// Native100362A9 consumes xmm1=x,xmm2=y,xmm3=0 and EDX=m_yaw. The
// ordinary trampoline preserves native filtering/acceleration and all devices.
__declspec(naked) void AtMouseStage()
{
    __asm {
        pushfd
        cmp byte ptr [enabled], 0
        je native
        pushad
        mov eax, esp
        sub esp, 528
        and esp, -16
        mov dword ptr [esp+512], eax
        fxsave [esp]
        push edi
        push esi
        call Process
        add esp, 8
        mov byte ptr [esp+516], al
        fxrstor [esp]
        cmp byte ptr [esp+516], 0
        je restore_native
        mov esp, dword ptr [esp+512]
        popad
        popfd
        mov eax, dword ptr [deltas]
        movss xmm1, dword ptr [eax]
        movss xmm2, dword ptr [eax+4]
        xorps xmm3, xmm3
        mov edx, dword ptr [yaw]
        jmp dword ptr [resume]
    restore_native:
        mov esp, dword ptr [esp+512]
        popad
    native:
        popfd
        jmp dword ptr [nativeStage]
    }
}
}
bool InitializeGuestCofMouse() { return InitializeGuestCofMouse(GoldCraftConnectionInterface()); }
bool InitializeGuestCofMouse(const GoldCraftConnectionAPI* provider)
{
    if (ownerThread && !Owner()) return false;
    if (installing) return false;
    if (hook) return provider == connection && GuestCofMouseReady();
    if (!serial || !provider || provider->size < sizeof(*provider) || provider->version != 1 || !provider->generation ||
        !api || !api->GetClientBase || !api->GetClientSize || !api->ResolveGameSymbol || !engine.pfnGetCvarPointer) return false;
    auto* stage = Resolve("GoldCraftCof_MouseStage");
    auto* next = Resolve("GoldCraftCof_MouseResume");
    auto* old = Resolve("GoldCraftCof_MouseHistoryReference");
    auto* output = Resolve("GoldCraftCof_MouseDeltasReference");
    auto* accum = Resolve("GoldCraftCof_MouseAccumReference");
    auto* yawReference = Resolve("GoldCraftCof_MouseYawReference");
    if (!Prefix(stage, {0xa1}) || !Prefix(next, {0x8b,0x0d}) ||
        !Prefix(old, {0x89,0x35}) || !Prefix(static_cast<std::byte*>(old)+6, {0x89,0x3d}) ||
        !Prefix(output, {0xf3,0x0f,0x11,0x05}) || !Prefix(static_cast<std::byte*>(output)+8, {0xf3,0x0f,0x11,0x15}) ||
        !Prefix(accum, {0xc7,0x05}) || !Prefix(static_cast<std::byte*>(accum)+10, {0xc7,0x05}) ||
        !Prefix(yawReference, {0x8b,0x15})) return false;
    auto* previous = Operand<std::int32_t>(old, 2, 8);
    auto* previousY = Operand<std::int32_t>(old, 8, 4);
    auto* result = Operand<float>(output, 4, 8);
    auto* resultY = Operand<float>(output, 12, 4);
    auto* pending = Operand<std::int32_t>(accum, 2, 8);
    auto* pendingY = Operand<std::int32_t>(accum, 12, 4);
    auto* filterSlot = Operand<cvar_t*>(stage, 1, sizeof(cvar_t*));
    auto* yawSlot = Operand<cvar_t*>(yawReference, 2, sizeof(cvar_t*));
    std::uint32_t zero1{}, zero2{};
    std::memcpy(&zero1, static_cast<std::byte*>(accum)+6, 4);
    std::memcpy(&zero2, static_cast<std::byte*>(accum)+16, 4);
    if (!previous || previousY != previous+1 || !result || resultY != result+1 ||
        !pending || pendingY != pending+1 || !filterSlot || !yawSlot || zero1 || zero2) return false;
    auto* f = engine.pfnGetCvarPointer("m_filter");
    auto* s = engine.pfnGetCvarPointer("sensitivity");
    auto* y = engine.pfnGetCvarPointer("m_yaw");
    if (!f || !s || !y || *filterSlot != f || *yawSlot != y) return false;
    ownerThread = GetCurrentThreadId(); connection = provider;
    history = previous;
    accumulated = pending; deltas = result; filter = f; sensitivity = s; yaw = y; resume = next;
    enabled = retiring = false;
    const auto revision = serial;
    installing = true;
    try { hook = Hook(stage, reinterpret_cast<void*>(AtMouseStage), &nativeStage); }
    catch (...) { installing=false; Remove(); return false; }
    installing = false;
    if (!hook || !nativeStage || retiring || serial != revision) { Remove(); return false; }
    return true;
}
bool GuestCofMouseReady() noexcept { return Owner() && hook && !retiring && serial; }
bool SetGuestCofMouseFrame(guest::cof::AuthorityKey key, int current, int normal, float value) noexcept
{
    if (!GuestCofMouseReady() || !guest::cof::Valid(key) || normal < 1 || normal > 179 ||
        !std::isfinite(value)) return false;
    const auto revision = serial;
    const auto generation = connection->generation();
    if (!GuestCofMouseReady() || serial != revision || !generation) return false;
    Advance();
    if (!serial) { enabled=false; return false; }
    authority = key; frameGeneration = generation;
    wholeFov = current; defaultFov = normal; hudSensitivity = value;
    enabled = true;
    return true;
}
void ResetGuestCofMouse() noexcept
{
    if (!Owner()) return;
    Advance(); enabled = false; authority = {}; frameGeneration = 0;
}
void GuestCofMouseFrame() noexcept { if (Owner() && retiring && !inHook && !installing) Remove(); }
void ShutdownGuestCofMouse() noexcept
{
    if (!Owner()) return;
    ResetGuestCofMouse(); retiring = true;
    // A provider may synchronously retire the callback. Keep the trampoline
    // alive through the executing detour; HUD_Frame completes deferred removal.
    if (!inHook && !installing) Remove();
}
}
