// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "goldcraft/lightstyle_api.hpp"
#include "goldcraft/lightstyle_owner.hpp"
#include "lightstyles.h"
#include <cstdio>
#include <new>

ServerBoundary g_psv;
StaticBoundary g_psvs;
RehldsBoundary g_rehlds_sv;
InterfaceReg* InterfaceReg::s_pInterfaceRegs{};
InterfaceReg::InterfaceReg(InstantiateInterfaceFn factory, const char* name)
    : m_CreateFn(factory), m_pName(name), m_pNext(s_pInterfaceRegs) { s_pInterfaceRegs = this; }
extern "C" IBaseInterface* CreateInterface(const char* name, int* result)
{
    for (auto* row = InterfaceReg::s_pInterfaceRegs; row; row = row->m_pNext)
        if (!std::strcmp(row->m_pName, name))
        { if (result) *result = IFACE_OK; return row->m_CreateFn(); }
    if (result) *result = IFACE_FAILED;
    return nullptr;
}
#include "lightstyle_actual.inc"

namespace
{
unsigned checks{}, failures{};
std::size_t allocations{};
void Check(bool value, const char* name)
{
    ++checks;
    if (!value) { ++failures; std::printf("FAIL %s\n", name); }
}
bool Pattern(const char* expected) { return g_psv.lightstyles[0] && !std::strcmp(g_psv.lightstyles[0], expected); }
void Clear(std::array<client_t, 4>& clients) { for (auto& client : clients) client.netchan.message = {}; }
}
void* operator new(std::size_t bytes)
{ ++allocations; if (auto* p = std::malloc(bytes ? bytes : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main()
{
    auto* service = static_cast<goldcraft::IMapLightStyleService*>(CreateInterface(goldcraft::MapLightStyleServiceVersion, nullptr));
    Check(service != nullptr, "actual interface registration");
    if (!service) return 1;
    std::array<client_t, 4> clients{};
    clients[0].active = true; clients[1].spawned = true;
    clients[2].active = clients[2].fakeclient = true;
    g_psvs = {1, 4, clients.data()};
    goldcraft::MapLightStyleStatus status;
    Check(!service->Status(status) && !status.known, "no map has no baseline");
    Check(!service->Acquire(), "no map cannot acquire");
    g_psv.state = ss_loading;
    PF_lightstyle_I(0, "m");
    Check(Pattern("m") && !clients[0].netchan.message.size, "loading stores without broadcast");
    Check(!service->Acquire(), "loading cannot acquire");
    g_psv.state = ss_active;
    const auto first = service->Acquire();
    Check(first && !service->Acquire(), "exclusive owner");
    Check(service->Set(first, 'i') && Pattern("i"), "apply original dusk level");
    Check(service->Status(status) && status.applied && !std::strcmp(status.baseline, "m"), "own write preserves actual baseline");
    for (unsigned i = 0; i != clients.size(); ++i)
        Check(clients[i].netchan.message.size == (i < 2 ? 4u : 0u), "original human broadcast selection");
    Check(clients[0].netchan.message.bytes[0] == svc_lightstyle &&
        clients[0].netchan.message.bytes[1] == 0 && clients[0].netchan.message.bytes[2] == 'i', "original lightstyle wire");
    Clear(clients);
    Check(service->Set(first, 'i') && !clients[0].netchan.message.size, "unchanged level sends nothing");
    Check(!service->Set(first + 1, 'c') && !service->Release(first + 1) && Pattern("i"), "stale owner cannot edit or restore");
    Check(!service->Set(first, 0) && !service->Set(first, '{') && Pattern("i"), "invalid level leaves current state");
    PF_lightstyle_I(0, "klm");
    Check(Pattern("i"), "external map write cannot replace current override");
    Check(service->Status(status) && !std::strcmp(status.baseline, "klm"), "external write updates restoration baseline");
    PF_lightstyle_I(3, "abc");
    Check(!std::strcmp(g_psv.lightstyles[3], "abc") && Pattern("i"), "other light animations retain native path");
    Check(service->Set(first, 'c') && Pattern("c"), "apply original night level");
    Check(service->Release(first) && Pattern("klm"), "release restores latest external baseline");
    Check(!service->Release(first), "duplicate release inert");
    const auto second = service->Acquire();
    Check(second > first && service->Release(second) && Pattern("klm"), "unused acquisition preserves animation");
    PF_lightstyle_I(0, "");
    const auto empty = service->Acquire();
    Check(empty && service->Set(empty, 'c') && service->Release(empty) && Pattern(""), "empty native style is a known baseline");
    std::array<char, 90> longPattern{}; longPattern.fill('n'); longPattern.back() = 0;
    Clear(clients); PF_lightstyle_I(0, longPattern.data());
    const auto longOwner = service->Acquire();
    Check(service->Status(status) && std::strlen(status.baseline) == MAX_LIGHTSTYLE_SIZE - 1 && Pattern(status.baseline), "baseline matches native bounded storage");
    Clear(clients);
    Check(service->Set(longOwner, 'c'), "long baseline override");
    ++g_psvs.spawncount; g_psv.state = ss_loading;
    Check(!service->Release(longOwner), "old map cannot restore into loading map");
    PF_lightstyle_I(0, "o"); g_psv.state = ss_active;
    Check(Pattern("o") && !service->Set(longOwner, 'c'), "old token cannot own next map");
    const auto next = service->Acquire();
    Check(next > longOwner && service->Set(next, 'i'), "next map gets distinct owner");
    g_psv.state = ss_dead;
    Check(!service->Status(status) && !status.known && !status.owner && !service->Release(next), "shutdown forgets old ownership");
    ++g_psvs.spawncount; g_psv.state = ss_active;
    Check(!service->Acquire(), "active map without observed style cannot fabricate baseline");
    PF_lightstyle_I(0, "m");
    const auto hot = service->Acquire();
    Clear(clients); g_psvs.maxclients = 0;
    const auto before = allocations;
    for (unsigned i = 0; i != 10000; ++i)
    {
        PF_lightstyle_I(0, i & 1 ? "ijk" : "nop");
        Check(service->Set(hot, i & 1 ? 'i' : 'c'), "repeat actual service write");
    }
    Check(allocations == before, "10000 observer and service updates allocate nothing");
    Check(service->Release(hot) && Pattern("ijk"), "stress restoration retains final external pattern");
    std::printf("checks=%u failures=%u hotAllocations=%zu\n", checks, failures, allocations - before);
    return failures ? 1 : 0;
}
