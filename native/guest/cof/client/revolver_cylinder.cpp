// SPDX-License-Identifier: GPL-3.0-or-later
#include "revolver_cylinder.hpp"
#include <metahook.h>
#include <xmmintrin.h>
#include <cstdint>
#pragma clang fp contract(off)
namespace guest::cof::client
{
bool ApplyRevolverCylinder(cl_entity_s& entity,const cl_entity_s* currentViewModel,
    unsigned originalWeapon,float angle) noexcept
{
    if(&entity!=currentViewModel||originalWeapon!=21)return false;
    // Preserve the original two float operations and x86 truncation, including
    // unordered/out-of-range conversion, before complementing the low byte.
    const auto turns=_mm_div_ss(_mm_set_ss(angle),_mm_set_ss(360.0F));
    const auto scaled=_mm_mul_ss(turns,_mm_set_ss(255.0F));
    const auto value=static_cast<std::uint8_t>(~_mm_cvtt_ss2si(scaled));
    entity.curstate.controller[0]=value;
    entity.curstate.controller[1]=value;
    entity.curstate.controller[2]=value;
    entity.latched.prevcontroller[0]=entity.curstate.controller[0];
    entity.latched.prevcontroller[1]=entity.curstate.controller[1];
    entity.latched.prevcontroller[2]=entity.curstate.controller[2];
    return true;
}
}
