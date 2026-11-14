// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// Include the matching interface.h first. Local, main-server-thread ABI only.
namespace goldcraft
{
inline constexpr char MapLightStyleServiceVersion[] = "GoldCraftMapLightStyle001";
struct MapLightStyleStatus
{
    std::uint64_t owner{};
    std::uint32_t map{};
    bool known{}, applied{};
    char level{}, baseline[64]{};
};
class IMapLightStyleService : public IBaseInterface
{
public:
    virtual std::uint64_t Acquire() = 0;
    virtual bool Set(std::uint64_t owner, char level) = 0;
    virtual bool Release(std::uint64_t owner) = 0;
    virtual bool Status(MapLightStyleStatus& output) = 0;
};
}
