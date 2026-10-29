// SPDX-License-Identifier: GPL-3.0-or-later
#include "beamgun_prediction.hpp"
#include "beamgun_effects.hpp"
#include "beamgun_scope.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <source_location>
struct Data { int m_iId{},m_iClip{},iuser1{},m_fInReload{},m_iWeaponState{};
    float m_flNextPrimaryAttack{},m_flNextSecondaryAttack{},m_flTimeWeaponIdle{},fuser1{},fuser2{},fuser3{}; };
int checks;
void Check(bool ok,const std::source_location at=std::source_location::current())
{ ++checks;if(!ok){std::cerr<<"FAIL beam wire line "<<at.line()<<'\n';std::exit(1);} }
int main()
{
    using namespace csol;
    for(int id:{725,726})for(int energy:{0,5,95,100})for(auto charge:{BeamCharge::None,BeamCharge::Alpha,BeamCharge::Beta})
    {
        BeamWeaponState source;source.energy=energy;source.clip=73;source.charge=charge;source.dive=BeamDive::Start;
        source.pending=BeamPending::Second;source.chargeTime=1.4f;source.comboTime=source.delayedTime=-.001f;
        Data data;data.m_iId=id;WriteBeamPrediction(source,data);
        // Actual CS delta multipliers, including the easily lost negative sentinel.
        data.fuser1=std::trunc(data.fuser1*1000)/1000;
        data.fuser2=std::trunc(data.fuser2*128)/128;data.fuser3=std::trunc(data.fuser3*128)/128;
        auto read=ReadBeamPrediction(data);
        Check(data.m_iId==id&&read.energy==energy&&read.clip==73&&read.charge==charge);
        Check(read.comboTime<0&&read.delayedTime<0&&read.pending==BeamPending::Second);
        Check(read.UpdateDive(true,3).stopVelocity);
    }
    BeamEffect original{726,1,5,1,4,{2,3,4,5},{1,2,3},{4,5,6}},decoded;
    const auto bytes=EncodeBeamEffect(original);
    Check(DecodeBeamEffect(bytes,decoded)&&decoded.targets==original.targets&&decoded.end==original.end);
    for(int length=0;length<40;++length)Check(!DecodeBeamEffect(std::span(bytes).first(length),decoded));
    for(int field:{0,1,3,5,6,7,8})
    {
        auto bad=bytes;bad[field]=std::byte{255};decoded=original;
        Check(!DecodeBeamEffect(bad,decoded));Check(decoded.targets==original.targets&&decoded.weapon==original.weapon);
    }
    for(float f:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),131073.f})
    {auto value=original;value.origin[0]=f;Check(EncodeBeamEffect(value)[0]==std::byte{0});}
    for(int kind=0;kind<=6;++kind){auto value=original;value.mode=kind;Check(DecodeBeamEffect(EncodeBeamEffect(value),decoded));}
    std::array<BeamScopeQuad,5> quads;
    Check(BeamScopeLayout(1280,720,0,quads)==3&&quads[0].left==595&&quads[0].top==315&&quads[0].right==685);
    Check(BeamScopeLayout(1280,720,.5f,quads)==5&&quads[4].left==574&&quads[4].top==360&&quads[4].bottom==440);
    Check(quads[4].v0==.5f&&quads[4].u1==1.f&&quads[4].v1==1.f);
    Check(BeamScopeLayout(1920,1080,1,quads)==5&&quads[0].left==896&&quads[0].right==1023);
    Check(BeamScopeLayout(0,720,1,quads)==0);
    std::cout<<"PASS beam wire/scope "<<checks<<" checks\n";
}
