// SPDX-License-Identifier: GPL-3.0-or-later
#include "aurora_render.hpp"
#include <metahook.h>
#include <com_model.h>
#include <triangleapi.h>
#include <algorithm>
#include <cmath>
#pragma clang fp contract(off)
namespace guest::cof::client
{
namespace
{
template<std::size_t N> bool Finite(const std::array<float,N>& a) noexcept
{
    for (float v : a) if (!std::isfinite(v)) return false;
    return true;
}
std::array<float,3> Corner(const std::array<float,3>& position,
    float upScale, const std::array<float,3>& up,
    float rightScale, const std::array<float,3>& right) noexcept
{
    std::array<float,3> out;
    for (unsigned i=0;i<3;++i) out[i]=position[i]+up[i]*upScale;
    for (unsigned i=0;i<3;++i) out[i]+=right[i]*rightScale;
    return out;
}
}
void PrepareAuroraDegreeTable(AuroraDegreeTable& table) noexcept
{
    constexpr double pi=3.141592653589793;
    for (unsigned i=0;i<table.values.size();++i)
        table.values[i]=static_cast<float>(std::sin(static_cast<double>(i)*pi/180.0));
    table.prepared=true;
}
AuroraDrawResult DrawAuroraParticle(AuroraRenderParticle& particle, const AuroraDegreeTable& table,
    const AuroraRenderSeed& seed, const AuroraRenderFade& fade, const std::array<float,3>& right,
    const std::array<float,3>& up, const model_s* sprite, triangleapi_s* tri) noexcept
{
    if (!seed.resolved) return AuroraDrawResult::UnresolvedSeed;
    if (seed.drawCondition!=0 || seed.waterOnly!=0) return AuroraDrawResult::UnsupportedControl;
    if (!Finite(particle.position) || !Finite(particle.color) || !Finite(right) || !Finite(up) ||
        !std::isfinite(particle.size) || !std::isfinite(particle.angle) || !std::isfinite(particle.frame) ||
        !std::isfinite(fade.now) || !std::isfinite(fade.start) || !std::isfinite(fade.duration) ||
        (fade.state!=0 && fade.state!=1) || (fade.state==1 && fade.duration<=0.0F) ||
        particle.angle<=-360.0F || particle.angle>=361.0F) return AuroraDrawResult::InvalidInput;
    if (particle.size==0.0F) return AuroraDrawResult::SizeZero;
    if (!table.prepared || !sprite || sprite->numframes<=0 || !tri || !tri->SpriteTexture ||
        !tri->RenderMode || !tri->Color4f || !tri->Begin || !tri->End || !tri->TexCoord2f ||
        !tri->Vertex3fv) return AuroraDrawResult::MissingBackend;
    const int angle=static_cast<int>(particle.angle);
    const float sinSize=table.values[angle<0?angle+360:angle]*particle.size;
    const float cosSize=table.values[angle<-90?angle+450:angle+90]*particle.size;
    std::array<std::array<float,3>,4> corners{{
        Corner(particle.position,cosSize,up,-sinSize,right),
        Corner(particle.position,sinSize,up,cosSize,right),
        Corner(particle.position,-cosSize,up,sinSize,right),
        Corner(particle.position,-sinSize,up,-cosSize,right)}};
    for (const auto& corner:corners) if(!Finite(corner)) return AuroraDrawResult::InvalidInput;
    float frame=particle.frame;
    const float count=static_cast<float>(sprite->numframes);
    unsigned wraps=0;
    while(frame>count)
    {
        if(++wraps>4096) return AuroraDrawResult::FrameRangeExceeded;
        frame-=count;
    }
    while(frame<0.0F)
    {
        if(++wraps>4096) return AuroraDrawResult::FrameRangeExceeded;
        frame+=count;
    }
    float alpha=particle.color[3];
    if(fade.state==1)
    {
        float progress=(fade.now-fade.start)/fade.duration;
        if(progress>1.0F || fade.now>fade.start+fade.duration) progress=1.0F;
        alpha-=progress*5.0F;
    }
    if(!std::isfinite(alpha)) return AuroraDrawResult::InvalidInput;
    particle.frame=frame;
    if(!tri->SpriteTexture(const_cast<model_s*>(sprite),static_cast<int>(frame))) return AuroraDrawResult::TextureUnavailable;
    tri->RenderMode(5);
    tri->Color4f(particle.color[0],particle.color[1],particle.color[2],std::max(0.0F,alpha));
    tri->Begin(TRI_QUADS);
    constexpr std::array<std::array<float,2>,4> uv{{{0,0},{1,0},{1,1},{0,1}}};
    for(unsigned i=0;i<4;++i)
    {
        tri->TexCoord2f(uv[i][0],uv[i][1]);
        tri->Vertex3fv(corners[i].data());
    }
    tri->End();
    return AuroraDrawResult::Drawn;
}
bool FinishAuroraFrame(triangleapi_s* tri) noexcept
{
    if(!tri || !tri->RenderMode) return false;
    tri->RenderMode(0);
    return true;
}
}
