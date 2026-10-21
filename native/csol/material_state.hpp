// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "model_json.hpp"
#include <array>
#include <algorithm>
#include <string_view>
namespace csol
{
enum class Blend { Opaque, Masked, Alpha, Add, AddAlpha };
struct MaterialPolicy
{
    Blend blend=Blend::Opaque,fadeBlend=Blend::Alpha;
    bool special{},cull=true,depth=true,commonAlpha{},commonAdd{};
    float commonOpacity=1;
    std::array<float,3> commonColor{1,1,1};
};
inline MaterialPolicy ParseMaterialPolicy(std::string_view name)
{
    MaterialPolicy policy;
    if(name.size()>2 && name[0]=='$')
    {
        if(name[2]=='b')policy.fadeBlend=name.size()>3 && name[3]=='a'?Blend::AddAlpha:Blend::Add;
        if(name[1]>='0' && name[1]<='3' && (name[2]=='0' || name[2]=='a' || name[2]=='b'))
        {
            policy.special=true;policy.cull=(name[1]-'0')&1;policy.depth=(name[1]-'0')&2;
            policy.blend=name[2]=='0'?Blend::Opaque:name[2]=='a'?Blend::Alpha:policy.fadeBlend;
        }
    }
    return policy;
}
struct MaterialDraw
{
    Blend blend=Blend::Opaque;bool cull=true,depth=true,fullbright{},masked{};
    std::array<float,4> color{1,1,1,1};
};
inline MaterialDraw EvaluateMaterial(const MaterialPolicy& policy,int flags,const FadeResult& fade,float& alpha)
{
    MaterialDraw draw;draw.masked=(flags&64)!=0;draw.fullbright=(flags&(4|32))!=0;
    if(fade.Active)alpha=std::min(alpha,fade.Alpha);
    draw.blend=(flags&32)?Blend::AddAlpha:alpha<1?Blend::Alpha:Blend::Opaque;draw.depth=(flags&32)==0;
    const bool special=policy.special || fade.Active;
    const Blend materialBlend=fade.Active?policy.fadeBlend:policy.blend;
    const bool additiveColor=special && (materialBlend==Blend::Add || materialBlend==Blend::AddAlpha);
    if(special)
    {
        draw.blend=materialBlend;draw.cull=policy.cull && !fade.Active;draw.depth=policy.depth && !fade.Active;draw.fullbright=true;
    }
    if(policy.commonAlpha){draw.blend=Blend::Alpha;alpha*=policy.commonOpacity;}
    else if(policy.commonAdd)draw.blend=Blend::Add;
    // CSOL modulates RGB for $ additive materials. GL_ONE does not use alpha.
    draw.color=additiveColor?std::array<float,4>{alpha,alpha,alpha,1}:std::array<float,4>{1,1,1,alpha};
    if(policy.commonAlpha && !special)for(size_t i=0;i<3;++i)draw.color[i]*=policy.commonColor[i];
    return draw;
}
}
