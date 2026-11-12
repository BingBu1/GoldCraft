// SPDX-License-Identifier: GPL-3.0-or-later
#include "forward_displacement.hpp"
#pragma clang fp contract(off)
namespace guest::cof::client
{
void ResetForwardDisplacement(ForwardDisplacementState& state) noexcept {state.value=0.0F;}
void LimitForwardDisplacement(ForwardDisplacementState& state) noexcept
{
    if(state.value>5.0F)state.value=5.0F;
    else if(state.value<0.0F)state.value=0.0F;
}
void ApplyForwardDisplacement(const ForwardDisplacementState& state,std::span<float,3> origin,
                              const std::array<float,3>& forward) noexcept
{
    for(unsigned i=0;i<3;++i)origin[i]=origin[i]-forward[i]*state.value;
}
}
