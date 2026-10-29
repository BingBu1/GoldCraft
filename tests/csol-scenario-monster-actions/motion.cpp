// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace actions_test
{
namespace
{
std::uint32_t Bits(float value){return std::bit_cast<std::uint32_t>(value);}
bool Near(float a,float b,float tolerance=0.000001f){return std::abs(a-b)<=tolerance;}
}
void HeadingCases()
{
    float c=12,s=13;
    Check(CSOL_MonsterHeading(0,c,s)&&c==1&&Near(s,-0.00615973165f),"CSO denominator255 LUT has nonzero sine at zero yaw");
    const float c0=c,s0=s;
    Check(CSOL_MonsterHeading(360,c,s)&&c==c0&&s==s0,"positive full turn retains table bins");
    Check(CSOL_MonsterHeading(-360,c,s)&&c==c0&&s==s0,"negative full turn retains table bins");
    Check(CSOL_MonsterHeading(90,c,s)&&Near(c,0.0184789f)&&s==1,"quarter turn uses63 cosine bin and zero sine lookup");
    bool facing=false;
    Check(CSOL_MonsterFacingTarget(0,100,0,facing)&&facing,"front target accepted");
    Check(CSOL_MonsterFacingTarget(0,-100,0,facing)&&!facing,"behind target rejected");
    Check(CSOL_MonsterFacingTarget(0,0,100,facing)&&!facing,"side target rejected");
    Check(CSOL_MonsterFacingTarget(0,0,0,facing)&&!facing,"coincident target remains below facing threshold");
    // Angles straddling the roughly36.87-degree cone distinguish >0.8
    // from positive-dot/hemisphere rules without reproducing lookup code.
    Check(CSOL_MonsterFacingTarget(0,100,70,facing)&&facing,"inside narrow facing cone");
    Check(CSOL_MonsterFacingTarget(0,100,90,facing)&&!facing,"outside narrow facing cone");
    c=12;s=13;
    Check(!CSOL_MonsterHeading(std::numeric_limits<float>::quiet_NaN(),c,s)&&c==12&&s==13,"invalid heading preserves outputs");
    Check(!CSOL_MonsterHeading(3e7f,c,s)&&c==12&&s==13,"bounded malformed reducible yaw rejected");
    facing=true;
    Check(!CSOL_MonsterFacingTarget(0,std::numeric_limits<float>::max(),1,facing)&&facing,"overflow delta rejected without output mutation");
}
void MotionCases()
{
    for(int mode=0;mode<5;++mode)
    {
        Fixture f;Vector2D velocity(0,0),facingDirection(0,0);float yaw=0,side=7;Vector jump(9,9,9);
        const int move=mode==0?0:mode==1?1:2;const bool crouch=mode==3;
        if(mode==4)f.monster->pev->maxspeed=300;
        Check(CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(100,0,50),.1f,move,crouch,false,0,velocity,yaw,side,jump,facingDirection),"ordinary TrackPath accepts explicit motion case");
        const std::array<std::uint32_t,5> expected{0x00000000,0x4247ac19,0x42c7ac19,0x41c7ac19,0x42ef9b51};
        Check(Bits(velocity.x)==expected[mode]&&velocity.y==0,"independent float32 acceleration reference incl inverse-sqrt approximation");
        Check(yaw==0&&side==7&&Equal(jump,Vector(100,0,50)),"forward deadzone preserves heading state and records original jump goal");
        Check(facingDirection.x==1&&facingDirection.y==0,"forward deadzone still records planar goal direction for contact");
    }
    {
        Fixture f;Vector2D velocity(3,4),facingDirection(0,0);float yaw=0,side=0;Vector goal;
        Check(CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(-100,0,0),.1f,2,false,true,0,velocity,yaw,side,goal,facingDirection),"jumping still faces goal");
        Check(yaw==-30&&side==-1&&velocity.x==3&&velocity.y==4&&f.monster->pev->angles.y==-30,"behind forces turn sign, jumping skips acceleration");
        Check(facingDirection.x==-1&&facingDirection.y==0,"contact direction retains rear goal instead of partially turned yaw");
    }
    {
        Fixture f;f.monster->pev->maxspeed=300;Vector2D velocity(0,0),facingDirection(0,0);float yaw=0,side=0;Vector goal;
        Check(CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(100,0,0),.1f,0,false,false,0,velocity,yaw,side,goal,facingDirection)&&Bits(velocity.x)==0x42ef9b51,"fast maxspeed override applies even to moveType0");
        velocity=Vector2D(0,0);
        Check(CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(100,0,0),.1f,0,false,false,1,velocity,yaw,side,goal,facingDirection)&&velocity.x==0,"skill1 prevents maxspeed override");
    }
    {
        Fixture f;Vector2D velocity(1,2),facingDirection(7,8);float yaw=0,side=3;Vector jump(4,5,6);
        Check(!CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(100,0,0),-1,2,false,false,0,velocity,yaw,side,jump,facingDirection)&&velocity.x==1&&velocity.y==2&&yaw==0&&side==3&&Equal(jump,Vector(4,5,6))&&facingDirection.x==7&&facingDirection.y==8,"invalid deltaTime leaves all outputs untouched");
        f.ReborrowSameSlot();
        Check(!CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(100,0,0),.1f,2,false,false,0,velocity,yaw,side,jump,facingDirection)&&velocity.x==1&&facingDirection.x==7&&facingDirection.y==8,"old lease cannot steer reborrowed monster or overwrite saved contact direction");
    }
    {
        Fixture f;Vector2D velocity(3,4),facingDirection(0,0);float yaw=25,side=0;Vector goal;
        Check(CSOL_TrackMonsterPath(*f.call,Vector(0,0,0),Vector(30,40,777),0,2,false,true,0,velocity,yaw,side,goal,facingDirection)&&
            facingDirection.x==.6f&&facingDirection.y==.8f&&yaw==25&&velocity.x==3&&velocity.y==4,
            "contact facing is normalized XY goal independent of height yaw and jumping acceleration");
        Check(CSOL_TrackMonsterPath(*f.call,Vector(10,20,30),Vector(10,20,999),0,2,false,true,0,velocity,yaw,side,goal,facingDirection)&&
            facingDirection.x==1&&facingDirection.y==0,"coincident XY goal replaces saved contact direction with original fallback");
    }
}
}
