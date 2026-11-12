// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "teddy_spark_particle.hpp"

namespace csol::teddy_meteor
{
using teddy_sparks::Vec3;
inline constexpr std::array<std::string_view,3> Sprites{
    "sprites/burn5.spr", "sprites/burn6.spr", "sprites/burn7.spr"};
inline constexpr std::string_view StopSprite = "sprites/dot.spr";
inline constexpr std::size_t MessageBytes=85, StopBytes=47;
inline constexpr std::uint32_t Group=2;
struct Emitter
{
    std::uint16_t owner{};
    std::uint8_t attachment{};
    float spread{}, size{}, growth{};
};
inline bool Identify(std::span<const std::uint8_t> bytes, std::string_view name)
{
    return bytes.size()>name.size() && bytes[name.size()]==0 &&
        std::string_view(reinterpret_cast<const char*>(bytes.data()),name.size())==name;
}
inline std::optional<Emitter> Read(std::span<const std::uint8_t> bytes)
{
    if(bytes.size()!=MessageBytes || !Identify(bytes,Sprites[0]))return {};
    std::size_t at=Sprites[0].size()+1;
    const auto byte=[&]{return bytes[at++];};
    const auto word=[&]{const unsigned low=byte();return low|unsigned(byte())<<8;};
    const auto integer=[&]{const std::uint32_t low=word();return low|std::uint32_t(word())<<16;};
    const auto real=[&]{return std::bit_cast<float>(integer());};
    Emitter result;
    result.owner=static_cast<std::uint16_t>(word());
    if(!result.owner || result.owner>32767)return {};
    for(unsigned i=0;i!=6;++i)if(word()!=0)return {};
    if(word()!=65535 || real()!=.01f || real()!=.2f || integer()!=0x1c30)return {};
    result.attachment=byte();
    if(result.attachment>1 || word()!=Group || byte()!=1 || byte()!=0 || byte()!=15 ||
        byte()!=5 || byte()!=250)return {};
    for(unsigned i=0;i!=3;++i)if(byte()!=255)return {};
    result.spread=real();result.size=real();
    if(real()!=0 || real()!=0)return {};
    result.growth=real();
    if(real()!=0 || real()!=0 || at!=bytes.size() ||
        !std::isfinite(result.spread) || result.spread<12 || result.spread>32 ||
        !std::isfinite(result.size) || result.size<20 || result.size>23 ||
        !std::isfinite(result.growth) || result.growth<2 || result.growth>5)return {};
    return result;
}
// Exact MP11493730 group2/owner0 command; parser returns before Add/resource I/O.
inline bool ReadStop(std::span<const std::uint8_t> bytes)
{
    if(bytes.size()!=StopBytes || !Identify(bytes,StopSprite))return false;
    std::size_t at=StopSprite.size()+1;
    for(unsigned i=0;i!=24;++i)if(bytes[at++]!=0)return false; // owner/coords/count/times.
    constexpr std::uint8_t suffix[]{0,4,0,0,2,0,0}; // flags400, short2, command0.
    for(auto value:suffix)if(bytes[at++]!=value)return false;
    return at==bytes.size();
}
struct Prepared
{
    Vec3 origin;
    float size;
    int sprite;
};
template<class FloatRandom,class IntegerRandom>
Prepared Prepare(const Emitter& emitter,const Vec3& attachment,FloatRandom& random,IntegerRandom& integer)
{
    Prepared result;
    result.size=random(0,3)+emitter.size;
    result.sprite=integer(0,2);
    const float z=random(5,10),y=random(-10,10),x=random(-10,10);
    result.origin={x+attachment[0],y+attachment[1],z+attachment[2]};
    return result;
}
struct Particle
{
    teddy_sparks::Particle value;
    float initialSize{},mass{};
};
template<class Random>
Particle Spawn(const Emitter& emitter,const Prepared& prepared,float now,float born,Random& random)
{
    Particle result;
    auto& p=result.value;
    p.origin=prepared.origin;
    for(float& v:p.velocity)v=random(-1,1)*emitter.spread+0.f;
    result.mass=random(1,1.5f);
    p.born=born;p.die=now+.2f;
    p.size=result.initialSize=prepared.size;p.growth=emitter.growth;p.alpha=250;
    p.roll=1;p.rotationVelocity=0;p.color={255,255,255};
    p.sprite=static_cast<std::uint8_t>(prepared.sprite);
    return result;
}
inline void Tick(Particle& particle,float now,float previous,float gravity,int frames)
{
    auto& p=particle.value;
    const float age=now-p.born;
    if(p.alpha>0)p.alpha-=age*.1f;
    if(p.alpha<0){p.alpha=0;p.die=now;}
    p.frame=static_cast<int>(age*15.f+0.f)%frames;
    p.size=particle.initialSize+age*(p.growth*30.f);
    if(p.size<.0001f)p.die=now;
    const float life=p.die-p.born;
    p.alpha=(1.f-age/life)*250.f;
    if(p.alpha<1)p.die=now;
    const float delta=now-previous;
    for(unsigned axis=0;axis!=3;++axis)p.origin[axis]+=p.velocity[axis]*delta;
    p.velocity[2]+=((-delta)*gravity)*0.f;
    if(p.alpha<0)p.alpha=0;
}
}
