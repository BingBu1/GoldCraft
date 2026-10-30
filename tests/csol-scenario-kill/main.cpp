// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/scenario_kill.hpp"
#include "original-expectations.hpp"
#include <cstdio>
#include <limits>
using namespace csol::scenario_kill;
int checks{},failures{};
void check(bool value,const char* label){++checks;if(!value){++failures;std::printf("FAIL %s\n",label);}}
bool contains(const Frame& f,Image image){for(unsigned i=0;i<f.count;++i)if(f.quads[i].image==image)return true;return false;}
int main()
{
    for(const auto& row:states)
    {
        State s;s.Apply({Kind::Frags,1,row[0]});s.Advance(1,1);s.ClearPresentation();
        s.Apply({Kind::Frags,1,row[1]});if(row[2])s.Apply({Kind::Combo,0,5});if(row[3])s.Apply({Kind::HeadHits,0,1});
        s.Advance(1,10);const auto f=s.Layout(1024,768);
        check(contains(f,Image::Kill)==bool(row[4]),"original plain trigger");
        check(contains(f,Image::Combo)==bool(row[5]),"original combo trigger");
        check(contains(f,Image::Perfect)==bool(row[6]),"original perfect trigger");
        s.Advance(1,10.5f);check(s.Layout(1024,768).count==0,"expiry");
    }
    for(const auto& row:animations)
    {
        const auto animation=Animate(float(row[1]),0,int(row[0]));
        check(animation.alpha==int(row[2]),"original alpha");check(animation.dy==int(row[3]),"original slide");
    }
    for(const auto& row:layouts)
    {
        State s;s.Apply({Kind::Frags,1,1});s.Advance(1,2);const auto f=s.Layout(row[0],row[1]);
        check(f.count==1,"one plain quad");const auto& q=f.quads[0];
        check(q.left==row[2]&&q.top==row[3]&&q.right-q.left==row[4]&&q.bottom-q.top==row[5],"original layout");
    }
    for(const int count:{0,9,10,19,20,29,30,2147483647})
    {
        State s;s.Apply({Kind::Combo,0,count});s.Advance(1,0);const auto f=s.Layout(1024,768);
        check(contains(f,count>=30?Image::Combo30:count>=20?Image::Combo20:count>=10?Image::Combo10:Image::Combo),"combo thresholds");
        check(f.count<=11,"bounded digits");
        for(unsigned i=1;i<f.count;++i)check(f.quads[i].u0>=0&&f.quads[i].u1<=1&&f.quads[i].right<=400,"atlas bounds/right alignment");
    }
    Message m{};
    const std::byte frags[]{std::byte{27},std::byte{2},std::byte{32},std::byte{0xe8},std::byte{0xfd}};
    check(DecodeFrags(frags,m)==ParseResult::Accepted&&m.player==32&&m.value==65000,"32-slot unsigned frags");
    for(unsigned n=0;n<5;++n)check(DecodeFrags({frags,n},m)!=ParseResult::Accepted,"truncated frags");
    const std::byte wrong[]{std::byte{206}};check(DecodeFrags(wrong,m)==ParseResult::Unrelated,"damage owner preserved");
    const std::byte negative[]{std::byte{1},std::byte{0xff},std::byte{0xff},std::byte{0xff},std::byte{0xff}};
    check(DecodeCombo(negative,m)==ParseResult::Malformed,"negative combo rejected");
    State s;s.Apply({Kind::Frags,2,1});s.Advance(1,0);check(!s.Layout(1024,768).count,"remote kill not local");
    s.Apply({Kind::Frags,1,1});s.Advance(1,0);s.Reset();s.Advance(1,0);check(!s.Layout(1024,768).count,"session reset");
    check(Animate(std::numeric_limits<float>::quiet_NaN(),0,768).alpha==0,"finite clock");
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
