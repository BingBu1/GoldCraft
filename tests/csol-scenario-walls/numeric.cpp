// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <limits>

namespace wall_test
{
void NumericCases()
{
    using namespace csol::scenario;
    float value=777;std::uint8_t level=0;
    Check(ResolvePlayerWallDamage(125,.3f,level,false,0,value)&&value==33.75f&&level==0,"MP preserves level zero and 0.9 factor");
    level=255;Check(ResolvePlayerWallDamage(10,1,level,true,0,value)&&value==20&&level==11,"mode six clamps unsigned attack byte to 11");
    level=255;Check(ResolvePlayerWallDamage(10,1,level,false,0x44,value)&&value==150&&level==41,"other mode caps 41 and character 0x44 triples");
    level=1;Check(ResolvePlayerWallDamage(10,1,level,false,0x43,value)&&value==10,"adjacent character feature has no multiplier");
    level=41;Check(ResolvePlayerWallDamage(10,.3f,level,false,0,value)&&difficulty_test::Bits(value,0x41700001),"two float32 stages preserve ratio rounding");
    const auto nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    for(float bad:{nan,inf,-inf})
    {
        value=777;level=255;
        Check(!ResolvePlayerWallDamage(bad,1,level,false,0,value)&&value==777&&level==255,"invalid incoming does not commit level or output");
        Check(!ResolvePlayerWallDamage(1,bad,level,false,0,value)&&value==777,"invalid player ratio rejected");
        Check(!ResolveMonsterWallDamage(1,bad,1,value)&&value==777,"invalid monster ratio rejected");
    }
    level=255;value=777;
    Check(!ResolvePlayerWallDamage(std::numeric_limits<float>::max(),2,level,false,0x44,value)&&value==777&&level==255,"overflow preserves state");
    Check(ResolveMonsterWallDamage(10,1,1,value)&&value==10,"monster manager constructor level one");
    Check(ResolveMonsterWallDamage(10,1,6,value)&&value==5,"monster signed manager deduction");
    Check(ResolveMonsterWallDamage(10,1,-1,value)&&value==12,"negative signed manager increases factor");
    Check(ResolveMonsterWallDamage(10,1,11,value)&&value==0,"zero result is represented before entity rejects damage");
    struct Sum {std::int16_t before;float input;std::int16_t expected;};
    for(const auto& x:std::vector<Sum>{{0,124.9f,124},{124,.9f,124},{124,1.9f,125},{32767,1,-32768},{-32768,65535,32767},{0,2147483648.0f,0},{0,-2147483648.0f,0},{-1,.5f,0}})
    {std::int16_t result=888;Check(AccumulateWallDamage(x.before,x.input,result)&&result==x.expected,"MP cvttss2si then low signed16 accumulator");}
    std::int16_t result=888;Check(!AccumulateWallDamage(1,inf,result)&&result==888,"nonfinite accumulation preserves old output");
    std::int32_t money=888;
    Check(ScaleWallReward(200,1.25f,money)&&money==250,"difficulty reward scale truncates after float32");
    Check(ScaleWallReward(-3,.5f,money)&&money==-1,"negative reward truncates toward zero");
    Check(ScaleWallReward(INT32_MAX,1,money)&&money==INT32_MIN,"float32 overflow follows original cvtt result");
    money=888;Check(!ScaleWallReward(1,nan,money)&&money==888,"nonfinite reward ratio rejected");
}
void MetadataCases()
{
    Engine e;auto& p=e.New<Player>("player");RewardFixture f(p,1.25f,2);
    csol::scenario::WallRewardSettings s;csol::scenario::ConfigError error;
    Check(csol::scenario::ReadWallRewardSettings(f.rounds,f.difficulty,s,error)&&s.threshold==125&&s.money==200&&s.experience==50&&s.moneyRatio==1.25f&&s.experienceRatio==2,"physical round rows and difficulty columns, independent of labels");
    f.rounds.source.records[2].fields[1]=" \t+125trailing";
    Check(csol::scenario::ReadWallRewardSettings(f.rounds,f.difficulty,s,error)&&s.threshold==125,"original atoi prefix metadata semantics");
    for(const char* bad:{"0","-1","garbage","2147483648",""})
    {
        f.rounds.source.records[2].fields[1]=bad;s.threshold=999;
        Check(!csol::scenario::ReadWallRewardSettings(f.rounds,f.difficulty,s,error)&&s.threshold==999&&!error.message.empty(),"invalid threshold rejects atomically");
    }
    f.rounds.source.records.resize(3);
    Check(!csol::scenario::ReadWallRewardSettings(f.rounds,f.difficulty,s,error),"missing physical metadata rejects");
    Check(!CCSOLScenarioWallRewards::Create(f.rounds,f.difficulty,nullptr,error),"no implicit successful provider");
}
}
