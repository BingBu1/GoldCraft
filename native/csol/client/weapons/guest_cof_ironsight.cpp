// SPDX-License-Identifier: GPL-3.0-or-later
#include "guest_cof_ironsight.hpp"
#include "../runtime.hpp"

#include <cstring>

namespace csol::client
{
namespace
{
hook_t* bobHook{};
void* nativeBob{};
void* bobContinue{};
DWORD ownerThread{};
bool aimedGuest{};
bool suppressBob{};

// Matched host client.dll b434b1c09b10b011be6c42e4154955da0c3ca46e95746988ebea1aa316a2a9f2:
// normal refdef 1006AB20, VM bob load 1006B67C. Only this scalar is
// replaced; the host continues its original shake, FOV, stair and smoothing
// code. In particular, the camera bob and bob clock are not suppressed.
__declspec(naked) void ViewmodelBob()
{
    __asm {
        pushfd
        cmp byte ptr [suppressBob], 0
        jne aimed
        popfd
        jmp dword ptr [nativeBob]
    aimed:
        popfd
        xorps xmm6, xmm6
        jmp dword ptr [bobContinue]
    }
}

bool OnOwner() noexcept { return ownerThread && ownerThread == GetCurrentThreadId(); }
}

bool InitializeGuestCofIronsight()
{
    if (bobHook) return OnOwner();
    if (!api || !api->ResolveGameSymbol || !api->GetClientBase) return false;
    void* address{};
    if (api->ResolveGameSymbol(api->GetClientBase(), "GoldCraftCof_ViewmodelBobLoad",
        MH_GAMESYMBOL_KIND_PATCH, &address) != MH_GAMESYMBOL_OK || !address)
        return false;
    constexpr unsigned char expected[]{0xf3,0x0f,0x10,0x74,0x24,0x2c};
    if (std::memcmp(address, expected, sizeof(expected))) return false;
    bobContinue = static_cast<unsigned char*>(address) + sizeof(expected);
    ownerThread = GetCurrentThreadId();
    aimedGuest = suppressBob = false;
    bobHook = Hook(address, reinterpret_cast<void*>(ViewmodelBob), &nativeBob);
    return true;
}

void ResetGuestCofIronsight() noexcept
{
    if (OnOwner()) aimedGuest = suppressBob = false;
}

bool GuestCofIronsightReady() noexcept { return OnOwner() && bobHook; }

void SetGuestCofIronsight(bool authenticatedGuest, bool aimed) noexcept
{
    if (!OnOwner()) return;
    aimedGuest = authenticatedGuest && aimed;
    if (!aimedGuest) suppressBob = false;
}

void ShutdownGuestCofIronsight() noexcept
{
    if (!OnOwner()) return;
    ResetGuestCofIronsight();
    if (bobHook) RemoveHook(bobHook);
    bobHook = nullptr;
    nativeBob = bobContinue = nullptr;
    ownerThread = 0;
}

GuestCofIronsightViewScope::GuestCofIronsightViewScope(const ref_params_s& ref) noexcept
{
    if (!OnOwner()) return;
    entered_ = true;
    previous_ = suppressBob;
    suppressBob = bobHook && aimedGuest && !ref.spectator && !ref.intermission && !ref.paused &&
        ref.viewentity == ref.playernum + 1;
}

GuestCofIronsightViewScope::~GuestCofIronsightViewScope()
{
    if (entered_) suppressBob = previous_ && aimedGuest && bobHook;
}
}
