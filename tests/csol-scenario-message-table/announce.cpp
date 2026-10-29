// SPDX-License-Identifier: GPL-3.0-or-later
// Authored boundary cases in the existing table-test executable. These test
// production parsing/counting, not original-resource or gameplay equivalence.
#include "announce.hpp"
#include <iostream>
#include <vector>

int ScenarioAnnounceTableCases()
{
    using namespace csol::scenario;
    int checks{},failures{};
    const auto check=[&](bool value,const char* label){++checks;if(!value){++failures;std::cerr<<"FAIL: "<<label<<'\n';}};
    AnnounceTable table;ResourceError error;
    const std::string ribbons="id,name,unused,unused,priority,category\r\n"
        "999,first,0,0,1,1\r\n999,duplicate,0,0,0,99\r\n-4,third,0,0,1,2\r\n";
    const std::string categories="header\r\n15,mode,1,0\r\n15,repeated,,2\r\n61,outside,1,1\r\n-1,outside,1,1\r\n";
    check(ReadAnnounceTable(ribbons,categories,table,error),"original position tables parse repeated rows");
    check(table.ribbons.size()==3&&table.ribbons[0].id==999&&table.ribbons[1].id==999&&table.ribbons[2].id==-4,
        "duplicate and non-index ID values retain source row order");
    check(table.Allowed(15,1)&&table.Allowed(15,2)&&table.Allowed(15,3)&&!table.Allowed(15,999),
        "lookup uses one-based physical position, never ID-field map");
    check(table.categories.at(1)[15]&&table.categories.at(2)[15],"empty repeated bool retains prior bit and nonzero integer enables");
    check(!table.Allowed(14,1)&&!table.Allowed(14,3)&&table.Allowed(14,2),"priority zero bypasses category while enabled priorities use exact mode bit");
    check(!table.Allowed(-1,1)&&!table.Allowed(61,1)&&!table.Allowed(15,0)&&!table.Allowed(15,4),"mode and ribbon bounds reject without indexing");
    const auto prior=table.ribbonSource.sourceBytes;
    for(const std::string bad:{"1,a,0,0,,1\r\n","1,a,0,0,1,\r\n","1,a,0,0,2147483648,1\r\n","\r\n2,a,0,0,1,1\r\n"})
    {
        check(!ReadAnnounceTable("header\r\n"+bad,categories,table,error),"invalid physical ribbon row rejected without renumbering");
        check(table.ribbonSource.sourceBytes==prior&&table.ribbons.size()==3&&!error.path.empty(),"failed parse preserves prior table and reports source path");
    }
    check(ReadAnnounceTable(ribbons,"header\r\n15,mode,-1,0\r\n15,again,0,\r\n",table,error),"repeated explicit zero boolean parsed");
    check(!table.Allowed(15,1)&&!table.Allowed(15,3),"explicit zero overwrites previous true, empty preserves false");

    const auto values=[](const AnnounceCandidates& result){return std::vector<std::uint8_t>(result.Values().begin(),result.Values().end());};
    AnnounceCounts counts;
    for(int i=1;i<8;++i)check(values(counts.Record(0)).empty(),"ordinary kills below eighth have no ribbon");
    check(values(counts.Record(0))==std::vector<std::uint8_t>{29},"eighth ordinary count emits 29");
    check(values(counts.Record(1))==std::vector<std::uint8_t>{29},"existing divisible A emits before current B classification");
    check(values(counts.Record(1))==std::vector<std::uint8_t>({29,31})&&counts.mixed,"mixed threshold emits once after ordinary candidate");
    check(values(counts.Record(1))==std::vector<std::uint8_t>{29},"mixed flag suppresses repeated mixed ribbon");
    counts={8,6,100,1,false};
    check(values(counts.Record(9))==std::vector<std::uint8_t>({29,30,31,39,42,57}),"maximum six original candidates keep ordered IDs");
    check(counts.c==0&&counts.d==0&&counts.mixed,"C100 and D1 are consumed independently of streak counters");
    counts={0xffffffffu,0,0,0,false};check(values(counts.Record(0)).empty()&&counts.a==0,"A wraps from UINT32_MAX to zero without signed UB");
    counts={0x7fffffffu,0,0,0,false};check(values(counts.Record(0)).empty()&&counts.a==0x80000000u,"signed-negative A does not qualify for positive divisible ribbon");
    counts={0,0xffffffffu,0xffffffffu,0,false};check(values(counts.Record(1)).empty()&&counts.b==0&&counts.c==0,"B and C wrap independently on kind1");
    counts={0,0,99,0,false};check(values(counts.Record(1))==std::vector<std::uint8_t>{39}&&counts.c==0,"C hundredth event resets its independent total");
    counts={0,0,0,0xffffffffu,false};check(values(counts.Record(2)).empty()&&counts.d==0,"D wraps to zero without false first-event reward");
    check(values(counts.Record(2))==std::vector<std::uint8_t>{42}&&counts.d==0,"D next event emits42 and resets");
    counts={9,7,88,23,true};counts.ResetStreak();
    check(counts.a==0&&counts.b==0&&!counts.mixed&&counts.c==88&&counts.d==23,"partial reset preserves C/D original state");
    const std::array<std::uint8_t,7> kindIds{45,46,47,50,55,56,57};
    for(int kind=3;kind<=9;++kind){counts={};check(values(counts.Record(kind))==std::vector<std::uint8_t>{kindIds[kind-3]},"direct classification retains original candidate ID");}
    std::cout<<"scenario_announce_table checks="<<checks<<" failures="<<failures<<'\n';
    return failures?1:0;
}
